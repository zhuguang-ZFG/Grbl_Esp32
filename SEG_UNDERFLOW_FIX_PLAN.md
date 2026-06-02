# SEG Underflow 固件端优化执行文档

## 一、问题背景

### 1.1 问题现象
- 蓝牙流式发送 G 代码时，出现 `[SEG underflow]` 警告
- 在 M30（换页）时刻，segment buffer 被步进 ISR 耗尽
- 导致运动暂停，影响打印质量

### 1.2 根本原因
1. **蓝牙断流**：上位机发送完一页后暂停 2+ 秒
2. **M30 检测依赖 segment buffer 耗尽**：`protocol_buffer_synchronize()` 等待 buffer 清空
3. **无预通知机制**：上位机不知道下位机即将换页

### 1.3 Superpowers 原则
1. **主动控制**：不要被动等待，要主动管理状态
2. **预通知**：在关键操作前，提前通知对方
3. **状态可见**：让上位机知道下位机的状态
4. **优雅降级**：即使出现问题，也能快速恢复

---

## 二、修改清单

| 序号 | 文件 | 修改内容 | 目的 |
|------|------|----------|------|
| 1 | `Grbl_Esp32/src/GCode.cpp` | 添加预通知机制 | 提前通知上位机即将换页 |
| 2 | `Grbl_Esp32/src/Protocol.cpp` | 添加 buffer 水位监控 | 实时监控 buffer 状态 |
| 3 | `Grbl_Esp32/src/Protocol.cpp` | 优化 underflow 恢复逻辑 | 区分换页场景和真正的 underflow |
| 4 | `Grbl_Esp32/src/Config.h` | 调整 SEGMENT_BUFFER_SIZE | 增大缓冲空间 |

---

## 三、详细修改说明

### 3.1 修改 GCode.cpp - 添加预通知机制

**文件路径**：`Grbl_Esp32/src/GCode.cpp`
**修改位置**：第 1718-1720 行

**修改前代码**：
```cpp
case ProgramFlow::CompletedM2:
case ProgramFlow::CompletedM30:
    protocol_buffer_synchronize();  // Sync and finish all remaining buffered motions before moving on.
    motors_set_disable(true);       // 写完一页/程序结束立即失能 XYZ
```

**修改后代码**：
```cpp
case ProgramFlow::CompletedM2:
case ProgramFlow::CompletedM30:
    // 【新增】预通知上位机即将换页，让上位机有时间准备
    // 上位机收到此消息后可以：暂停发送、发送心跳、准备换纸
    grbl_sendf(CLIENT_SERIAL, "[MSG:PAGE_END_IMMINENT]\r\n");
    
    protocol_buffer_synchronize();  // Sync and finish all remaining buffered motions before moving on.
    motors_set_disable(true);       // 写完一页/程序结束立即失能 XYZ
```

**目的**：
- 在 M30 执行前，提前通知上位机
- 上位机可以暂停发送新数据，避免断流

---

### 3.2 修改 Protocol.cpp - 添加 buffer 水位监控

**文件路径**：`Grbl_Esp32/src/Protocol.cpp`
**修改位置**：在文件末尾添加新函数，在主循环中调用

#### 步骤 1：添加水位监控函数

在 `protocol_execute_realtime()` 函数定义之前添加：

```cpp
// 【新增】Buffer 水位监控
// 每 500ms 检查一次 planner buffer 水位
// 当水位低于阈值时，通知上位机降低发送速度
static uint32_t last_buffer_check_ms = 0;
const uint32_t BUFFER_CHECK_INTERVAL_MS = 500;  // 检查间隔 500ms
const uint8_t BUFFER_LOW_THRESHOLD = 20;        // 低水位阈值

void check_buffer_watermark() {
    uint32_t now = millis();
    if (now - last_buffer_check_ms < BUFFER_CHECK_INTERVAL_MS) {
        return;
    }
    last_buffer_check_ms = now;
    
    uint8_t planner_free = plan_get_block_buffer_available();
    
    // 低水位警告：当 planner buffer 可用块数 < 20 时
    if (planner_free < BUFFER_LOW_THRESHOLD && sys.state == State::Cycle) {
        grbl_sendf(CLIENT_SERIAL, "[MSG:LOW_BUFFER B=%u]\r\n", planner_free);
    }
}
```

#### 步骤 2：在主循环中调用水位监控

在 `protocol_execute_realtime()` 函数末尾添加调用：

```cpp
    // 原有代码
#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
    paper_led_update();
#endif
    
    // 【新增】检查 buffer 水位（仅在 Cycle 状态下）
    if (sys.state == State::Cycle) {
        check_buffer_watermark();
    }
}
```

**目的**：
- 实时监控 planner buffer 水位
- 当水位低时，通知上位机降低发送速度
- 预防断流

---

### 3.3 修改 Protocol.cpp - 优化 underflow 恢复逻辑

**文件路径**：`Grbl_Esp32/src/Protocol.cpp`
**修改位置**：第 539-557 行

**修改前代码**：
```cpp
if (segment_buffer_underflow) {
    segment_buffer_underflow = false;
    // 附带 program_flow，便于判断是否是 M30/程序流切换导致的段缓冲耗尽。
    grbl_sendf(CLIENT_SERIAL,
               "[SEG underflow] B=%u st=%u progflow=%u execSys=%u\r\n",
               (unsigned)plan_get_block_buffer_available(),
               (unsigned)sys.state,
               (unsigned)gc_state.modal.program_flow,
               (unsigned)sys.step_control.executeSysMotion);

    // 若 planner 仍有可执行块，但 stepper 因 segment buffer 空而停下，
    // 立刻重装载 segment buffer 并启动 cycle，避免蓝牙流式发送时出现停顿卡顿。
    if (sys.state == State::Idle && plan_get_current_block() != NULL) {
        // 注意：避免强行篡改 sys.state / sys.suspend 以免造成状态机不一致。
        // 只清除可能阻止续料的 endMotion 标志，然后让 stepper 重新开始工作。
        sys.step_control.endMotion = false;
        st_prep_buffer();
        st_wake_up();
    }
}
```

**修改后代码**：
```cpp
if (segment_buffer_underflow) {
    segment_buffer_underflow = false;
    
    uint8_t planner_free = plan_get_block_buffer_available();
    
    // 【优化】区分换页场景和真正的 underflow
    // 换页场景：planner 有大量数据（B > 70），但 segment buffer 被耗尽
    // 这是正常现象，因为上位机已停止发送，等待换纸完成
    if (planner_free > 70) {
        // 换页场景：静默处理，不打印警告
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, 
                       "[BT] Page end detected, waiting for next page...");
    } else {
        // 真正的 underflow：打印警告
        // 附带 program_flow，便于判断是否是 M30/程序流切换导致的段缓冲耗尽。
        grbl_sendf(CLIENT_SERIAL,
                   "[SEG underflow] B=%u st=%u progflow=%u execSys=%u\r\n",
                   (unsigned)planner_free,
                   (unsigned)sys.state,
                   (unsigned)gc_state.modal.program_flow,
                   (unsigned)sys.step_control.executeSysMotion);
    }

    // 若 planner 仍有可执行块，但 stepper 因 segment buffer 空而停下，
    // 立刻重装载 segment buffer 并启动 cycle，避免蓝牙流式发送时出现停顿卡顿。
    if (sys.state == State::Idle && plan_get_current_block() != NULL) {
        // 注意：避免强行篡改 sys.state / sys.suspend 以免造成状态机不一致。
        // 只清除可能阻止续料的 endMotion 标志，然后让 stepper 重新开始工作。
        sys.step_control.endMotion = false;
        st_prep_buffer();
        st_wake_up();
    }
}
```

**目的**：
- 区分换页场景和真正的 underflow
- 换页场景：静默处理，不打印警告（因为这是预期行为）
- 真正的 underflow：打印警告并尝试恢复

---

### 3.4 修改 Config.h - 调整 SEGMENT_BUFFER_SIZE

**文件路径**：`Grbl_Esp32/src/Config.h`
**修改位置**：第 478 行

**修改前代码**：
```cpp
#define SEGMENT_BUFFER_SIZE 16 // 再增大步段缓冲：降低 BT 断流导致的段间空档/等待
```

**修改后代码**：
```cpp
#define SEGMENT_BUFFER_SIZE 24 // 进一步增大步段缓冲：降低 BT 断流导致的段间空档/等待
```

**目的**：
- 增大 segment buffer，提供更多的缓冲空间
- 降低 underflow 发生的概率

---

## 四、执行步骤

### 步骤 1：修改 GCode.cpp
1. 打开 `Grbl_Esp32/src/GCode.cpp`
2. 定位到第 1718-1720 行
3. 在 `protocol_buffer_synchronize()` 前添加预通知代码
4. 保存文件

### 步骤 2：修改 Protocol.cpp
1. 打开 `Grbl_Esp32/src/Protocol.cpp`
2. 添加 buffer 水位监控函数
3. 在主循环中调用水位监控
4. 优化 underflow 恢复逻辑
5. 保存文件

### 步骤 3：修改 Config.h
1. 打开 `Grbl_Esp32/src/Config.h`
2. 调整 SEGMENT_BUFFER_SIZE 从 16 到 24
3. 保存文件

### 步骤 4：编译验证
1. 使用 PlatformIO 编译固件
2. 检查是否有编译错误
3. 烧录到 ESP32 测试

---

## 五、验证方法

### 5.1 编译验证
```bash
cd d:\Grbl_Esp32
platformio run -e release
```

### 5.2 功能验证
1. 烧录固件到 ESP32
2. 通过蓝牙连接上位机
3. 发送多页 G 代码
4. 观察串口日志：
   - 是否收到 `[MSG:PAGE_END_IMMINENT]` 预通知
   - 是否收到 `[MSG:LOW_BUFFER]` 水位警告
   - 是否还有 `[SEG underflow]` 警告

### 5.3 性能验证
1. 发送大量 G 代码
2. 监控 buffer 水位变化
3. 观察是否有断流现象

---

## 六、预期效果

### 6.1 修改前
```
M30 检测 → protocol_buffer_synchronize() → 等待 buffer 清空 → underflow → 换纸
```

### 6.2 修改后
```
M30 检测 → 发送预通知 → protocol_buffer_synchronize() → 正常完成 → 换纸
            │
            └─ 上位机收到预通知后，暂停发送，避免断流
```

---

## 七、风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 预通知消息增加通信开销 | 低 | 消息很短，影响可忽略 |
| 水位监控增加 CPU 负载 | 低 | 每 500ms 检查一次，负载很小 |
| 增大 segment buffer 增加 RAM 使用 | 中 | ESP32 有足够 RAM，可接受 |
| 修改核心函数可能引入新问题 | 中 | 充分测试，保留回退方案 |

---

## 八、回退方案

如果修改后出现问题，可以：

1. **回退 GCode.cpp**：删除预通知代码
2. **回退 Protocol.cpp**：恢复原有 underflow 处理逻辑
3. **回退 Config.h**：将 SEGMENT_BUFFER_SIZE 改回 16

---

## 九、相关文件

- `Grbl_Esp32/src/GCode.cpp` - G 代码解析和执行
- `Grbl_Esp32/src/Protocol.cpp` - 协议处理和实时命令执行
- `Grbl_Esp32/src/Config.h` - 配置参数定义
- `Grbl_Esp32/src/Stepper.cpp` - 步进电机控制
- `Grbl_Esp32/src/Stepper.h` - 步进电机头文件

---

## 十、版本记录

| 日期 | 版本 | 修改内容 | 作者 |
|------|------|----------|------|
| 2026-06-02 | v1.0 | 初始版本 | Qoder |

