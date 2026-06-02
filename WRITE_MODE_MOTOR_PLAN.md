# 写字模式电机保持方案

## 一、问题背景

### 1.1 问题现象
- 写字过程中，电机在运动间隙会自动失能（默认 250ms 后）
- 笔抬起/放下时电机失能，影响写字质量
- 电机失能后重新使能会有延迟，导致笔画不连续

### 1.2 根本原因
- Grbl 默认行为：运动结束 250ms 后自动失能电机
- 目的：节省电力、减少发热、允许手动移动
- 问题：写字机需要电机始终保持使能

### 1.3 Superpowers 原则应用
1. **主动控制**：添加"写字模式"标志，主动控制电机状态
2. **预通知**：在电机状态变化前通知上位机
3. **状态可见**：让上位机知道电机当前状态
4. **优雅降级**：即使出现问题也能快速恢复

---

## 二、解决方案

### 2.1 方案概述

添加"写字模式"（Write Mode）标志，在写字过程中强制电机保持使能状态。

### 2.2 修改清单

| 序号 | 文件 | 修改内容 | 目的 |
|------|------|----------|------|
| 1 | `Config.h` | 添加写字模式配置宏 | 定义功能开关 |
| 2 | `Stepper.cpp` | 修改 `st_go_idle()` 逻辑 | 写字模式下跳过失能 |
| 3 | `Protocol.cpp` | 修改主循环检查逻辑 | 写字模式下跳过失能检查 |
| 4 | `GCode.cpp` | 添加 M 代码控制写字模式 | 上位机可控制模式切换 |
| 5 | `Report.cpp` | 添加电机状态报告 | 状态可见性 |

---

## 三、详细修改说明

### 3.1 修改 Config.h - 添加配置宏

**文件路径**：`Grbl_Esp32/src/Config.h`
**修改位置**：在文件末尾添加

**新增代码**：
```cpp
// 【新增】写字模式配置
// 写字模式下，电机始终保持使能，不会因空闲而自动失能
// 适用于写字机、绘图机等需要电机持续保持位置的应用
#define ENABLE_WRITE_MODE 1  // 启用写字模式功能
#define DEFAULT_WRITE_MODE 0  // 默认关闭写字模式
```

**目的**：
- 定义功能开关
- 默认关闭，需要上位机主动开启

---

### 3.2 修改 Stepper.cpp - 跳过失能逻辑

**文件路径**：`Grbl_Esp32/src/Stepper.cpp`
**修改位置**：第 442-465 行 `st_go_idle()` 函数

**修改前代码**：
```cpp
void st_go_idle() {
    Stepper_Timer_Stop();

    if (((stepper_idle_lock_time->get() != 0xff) || sys_rt_exec_alarm != ExecAlarm::None || sys.state == State::Sleep) &&
        sys.state != State::Homing) {

        if (sys.state == State::Sleep || sys_rt_exec_alarm != ExecAlarm::None) {
            motors_set_disable(true);
        } else {
            stepper_idle         = true;
            stepper_idle_counter = esp_timer_get_time() + (stepper_idle_lock_time->get() * 1000);
        }
    } else {
        motors_set_disable(false);
    }

    motors_unstep();
    st.step_outbits = 0;
}
```

**修改后代码**：
```cpp
// 【新增】写字模式标志
#ifdef ENABLE_WRITE_MODE
bool write_mode_enabled = DEFAULT_WRITE_MODE;
#endif

void st_go_idle() {
    Stepper_Timer_Stop();

    // 【新增】写字模式下，强制保持电机使能
    #ifdef ENABLE_WRITE_MODE
    if (write_mode_enabled) {
        motors_set_disable(false);  // 保持使能
        motors_unstep();
        st.step_outbits = 0;
        return;  // 跳过后续失能逻辑
    }
    #endif

    // 原有逻辑
    if (((stepper_idle_lock_time->get() != 0xff) || sys_rt_exec_alarm != ExecAlarm::None || sys.state == State::Sleep) &&
        sys.state != State::Homing) {

        if (sys.state == State::Sleep || sys_rt_exec_alarm != ExecAlarm::None) {
            motors_set_disable(true);
        } else {
            stepper_idle         = true;
            stepper_idle_counter = esp_timer_get_time() + (stepper_idle_lock_time->get() * 1000);
        }
    } else {
        motors_set_disable(false);
    }

    motors_unstep();
    st.step_outbits = 0;
}
```

**目的**：
- 写字模式下，强制保持电机使能
- 跳过原有的失能逻辑

---

### 3.3 修改 Protocol.cpp - 跳过主循环失能检查

**文件路径**：`Grbl_Esp32/src/Protocol.cpp`
**修改位置**：第 217-222 行

**修改前代码**：
```cpp
        // check to see if we should disable the stepper drivers ... esp32 work around for disable in main loop.
        if (stepper_idle && stepper_idle_lock_time->get() != 0xff) {
            if (esp_timer_get_time() > stepper_idle_counter) {
                motors_set_disable(true);
            }
        }
```

**修改后代码**：
```cpp
        // check to see if we should disable the stepper drivers ... esp32 work around for disable in main loop.
        #ifdef ENABLE_WRITE_MODE
        if (!write_mode_enabled) {  // 写字模式下跳过失能检查
        #endif
            if (stepper_idle && stepper_idle_lock_time->get() != 0xff) {
                if (esp_timer_get_time() > stepper_idle_counter) {
                    motors_set_disable(true);
                }
            }
        #ifdef ENABLE_WRITE_MODE
        }
        #endif
```

**目的**：
- 写字模式下，跳过主循环中的失能检查
- 防止电机因超时而失能

---

### 3.4 修改 GCode.cpp - 添加 M 代码控制

**文件路径**：`Grbl_Esp32/src/GCode.cpp`
**修改位置**：在 M 代码处理部分添加

**新增代码**：
```cpp
// 在 M 代码 switch 语句中添加
case 900:
    // M900 - 启用写字模式
    #ifdef ENABLE_WRITE_MODE
    write_mode_enabled = true;
    grbl_sendf(CLIENT_SERIAL, "[MSG:WRITE_MODE_ON]\r\n");
    #endif
    break;
case 901:
    // M901 - 禁用写字模式
    #ifdef ENABLE_WRITE_MODE
    write_mode_enabled = false;
    grbl_sendf(CLIENT_SERIAL, "[MSG:WRITE_MODE_OFF]\r\n");
    #endif
    break;
```

**目的**：
- 上位机可以通过 M900/M901 控制写字模式
- 提供状态反馈

---

### 3.5 修改 Report.cpp - 添加状态报告

**文件路径**：`Grbl_Esp32/src/Report.cpp`
**修改位置**：在状态报告函数中添加

**新增代码**：
```cpp
// 在状态报告中添加写字模式状态
#ifdef ENABLE_WRITE_MODE
if (write_mode_enabled) {
    strcat(status, "|WM:1");  // 写字模式开启
}
#endif
```

**目的**：
- 上位机可以通过状态报告了解当前是否处于写字模式
- 状态可见性

---

## 四、执行步骤

### 步骤 1：修改 Config.h
1. 打开 `Grbl_Esp32/src/Config.h`
2. 在文件末尾添加写字模式配置宏
3. 保存文件

### 步骤 2：修改 Stepper.cpp
1. 打开 `Grbl_Esp32/src/Stepper.cpp`
2. 添加写字模式标志变量
3. 修改 `st_go_idle()` 函数
4. 保存文件

### 步骤 3：修改 Protocol.cpp
1. 打开 `Grbl_Esp32/src/Protocol.cpp`
2. 修改主循环中的失能检查逻辑
3. 保存文件

### 步骤 4：修改 GCode.cpp
1. 打开 `Grbl_Esp32/src/GCode.cpp`
2. 添加 M900/M901 代码处理
3. 保存文件

### 步骤 5：修改 Report.cpp
1. 打开 `Grbl_Esp32/src/Report.cpp`
2. 添加写字模式状态报告
3. 保存文件

### 步骤 6：编译验证
1. 使用 PlatformIO 编译固件
2. 检查是否有编译错误
3. 烧录到 ESP32 测试

---

## 五、使用方法

### 5.1 启用写字模式

发送命令：
```
M900
```

预期响应：
```
[MSG:WRITE_MODE_ON]
ok
```

### 5.2 禁用写字模式

发送命令：
```
M901
```

预期响应：
```
[MSG:WRITE_MODE_OFF]
ok
```

### 5.3 查询当前状态

发送命令：
```
?
```

预期响应（写字模式开启时）：
```
<Idle|MPos:0.000,0.000,0.000|FS:0,0|WM:1|Bf:79,511>
```

---

## 六、验证方法

### 6.1 功能验证
1. 启用写字模式：`M900`
2. 发送运动指令：`G1 X100 Y0 F8000`
3. 等待运动完成
4. 检查电机是否仍然使能（用手尝试移动轴，应该有阻力）

### 6.2 状态验证
1. 查询状态：`?`
2. 检查响应中是否包含 `WM:1`
3. 禁用写字模式：`M901`
4. 再次查询状态，确认 `WM:1` 消失

### 6.3 长时间测试
1. 启用写字模式
2. 发送大量运动指令
3. 观察电机是否始终保持使能
4. 检查是否有过热现象

---

## 七、风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 电机长时间使能导致发热 | 中 | 提供 M901 命令可随时禁用 |
| 增加功耗 | 低 | ESP32 有足够供电能力 |
| 代码复杂度增加 | 低 | 使用条件编译，易于维护 |

---

## 八、回退方案

如果修改后出现问题：

1. **禁用写字模式**：发送 `M901`
2. **恢复默认设置**：发送 `$RST=*`
3. **回退代码**：
```bash
git checkout Grbl_Esp32/src/Stepper.cpp
git checkout Grbl_Esp32/src/Protocol.cpp
git checkout Grbl_Esp32/src/GCode.cpp
git checkout Grbl_Esp32/src/Config.h
```

---

## 九、版本记录

| 日期 | 版本 | 修改内容 | 作者 |
|------|------|----------|------|
| 2026-06-02 | v1.0 | 初始版本 | Qoder |

