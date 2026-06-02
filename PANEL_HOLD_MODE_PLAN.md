# 面板电机写字保持方案

## 一、问题背景

### 1.1 问题现象
- 写字过程中，面板电机在换纸完成后被禁用
- 面板电机禁用后，纸张可能漂移，影响写字质量

### 1.2 根本原因
- 换纸流程完成后，调用 `paper_disable_drivers()` 禁用所有纸系统电机
- 包括面板电机（PANEL_MOTOR）

### 1.3 当前控制逻辑

| 函数 | PAPER_ENABLE_PIN | PAPER_DRIVER_ENABLE_PIN | 效果 |
|------|------------------|-------------------------|------|
| `paper_enable_drivers()` | LOW | LOW | 使能所有电机 |
| `paper_disable_drivers()` | HIGH | HIGH | 禁用所有电机 |
| `paper_enable_panel_only()` | LOW | HIGH | 仅使能面板电机 |
| `paper_enable_clamp_feeder_only()` | HIGH | LOW | 仅使能拾落+进纸器 |

---

## 二、解决方案

### 2.1 方案概述

在写字过程中，保持面板电机使能，只在换纸时临时禁用。

### 2.2 修改清单

| 序号 | 文件 | 修改内容 | 目的 |
|------|------|----------|------|
| 1 | `PaperSystem.cpp` | 添加写字模式标志 | 控制面板电机状态 |
| 2 | `PaperSystem.cpp` | 修改换纸完成后逻辑 | 写字模式下保持面板电机使能 |
| 3 | `PaperSystem.cpp` | 添加 M 代码控制 | 上位机可控制模式切换 |
| 4 | `Config.h` | 添加配置宏 | 定义功能开关 |

---

## 三、详细修改说明

### 3.1 修改 Config.h - 添加配置宏

**文件路径**：`Grbl_Esp32/src/Config.h`
**修改位置**：在文件末尾添加

**新增代码**：
```cpp
// 【新增】写字模式配置
// 写字模式下面板电机保持使能，防止纸张漂移
#define ENABLE_PANEL_HOLD_MODE 1  // 启用面板保持功能
```

---

### 3.2 修改 PaperSystem.cpp - 添加写字模式标志

**文件路径**：`Grbl_Esp32/src/PaperSystem.cpp`
**修改位置**：在文件开头添加

**新增代码**：
```cpp
// 【新增】写字模式标志
#ifdef ENABLE_PANEL_HOLD_MODE
static bool panel_hold_mode = false;  // 写字模式：面板电机保持使能
#endif
```

---

### 3.3 修改 PaperSystem.cpp - 修改换纸完成后逻辑

**文件路径**：`Grbl_Esp32/src/PaperSystem.cpp`
**修改位置**：第 765-766 行

**修改前代码**：
```cpp
    // 9. 换纸流程完成后，关闭换纸相关电机使能，防止长时间发热
    paper_disable_drivers();
```

**修改后代码**：
```cpp
    // 9. 换纸流程完成后，关闭换纸相关电机使能，防止长时间发热
    #ifdef ENABLE_PANEL_HOLD_MODE
    if (panel_hold_mode) {
        // 写字模式：仅禁用拾落+进纸器，保持面板电机使能
        paper_enable_panel_only();
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto] Panel motor kept enabled (write mode)");
    } else {
        paper_disable_drivers();
    }
    #else
    paper_disable_drivers();
    #endif
```

---

### 3.4 修改 PaperSystem.cpp - 添加 M 代码控制

**文件路径**：`Grbl_Esp32/src/PaperSystem.cpp`
**修改位置**：在 `paper_system_mcode()` 函数中添加

**新增代码**：
```cpp
#ifdef ENABLE_PANEL_HOLD_MODE
case 902:
    // M902 - 启用面板保持模式（写字模式）
    panel_hold_mode = true;
    paper_enable_panel_only();
    grbl_sendf(CLIENT_SERIAL, "[MSG:PANEL_HOLD_ON]\r\n");
    return Error::Ok;
case 903:
    // M903 - 禁用面板保持模式
    panel_hold_mode = false;
    paper_disable_drivers();
    grbl_sendf(CLIENT_SERIAL, "[MSG:PANEL_HOLD_OFF]\r\n");
    return Error::Ok;
case 904:
    // M904 - 查询面板保持模式状态
    grbl_sendf(CLIENT_SERIAL, "[MSG:PANEL_HOLD=%s]\r\n", panel_hold_mode ? "ON" : "OFF");
    return Error::Ok;
#endif
```

---

## 四、执行步骤

### 步骤 1：修改 Config.h
1. 打开 `Grbl_Esp32/src/Config.h`
2. 在文件末尾添加配置宏
3. 保存文件

### 步骤 2：修改 PaperSystem.cpp
1. 打开 `Grbl_Esp32/src/PaperSystem.cpp`
2. 添加写字模式标志变量
3. 修改换纸完成后逻辑
4. 添加 M902/M903/M904 代码处理
5. 保存文件

### 步骤 3：编译验证
1. 使用 PlatformIO 编译固件
2. 检查是否有编译错误
3. 烧录到 ESP32 测试

---

## 五、使用方法

### 5.1 开始写字前

发送命令启用面板保持模式：
```
M902
```

预期响应：
```
[MSG:PANEL_HOLD_ON]
[PaperEn] panel_only: Q1=LOW, DRV_EN=HIGH
ok
```

### 5.2 写字过程中

- 正常发送 G 代码
- 换纸时，面板电机会临时禁用，换纸完成后自动重新使能
- 纸张始终保持张紧，不会漂移

### 5.3 写字完成后

发送命令禁用面板保持模式：
```
M903
```

预期响应：
```
[MSG:PANEL_HOLD_OFF]
ok
```

### 5.4 查询当前状态

发送命令：
```
M904
```

预期响应：
```
[MSG:PANEL_HOLD=ON]
ok
```

---

## 六、验证方法

### 6.1 功能验证
1. 启用面板保持模式：`M902`
2. 发送运动指令：`G1 X100 Y0 F8000`
3. 等待运动完成
4. 检查面板电机是否仍然使能（用手尝试移动纸张，应该有阻力）

### 6.2 换纸验证
1. 启用面板保持模式
2. 触发换纸：`M30` 或发送完整页面
3. 换纸完成后，检查面板电机是否自动重新使能
4. 验证纸张是否保持张紧

### 6.3 状态验证
1. 查询状态：`M904`
2. 检查响应中是否包含 `PANEL_HOLD=ON`
3. 禁用面板保持模式：`M903`
4. 再次查询状态，确认 `PANEL_HOLD=OFF`

---

## 七、风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 面板电机长时间使能导致发热 | 中 | 提供 M903 命令可随时禁用 |
| 增加功耗 | 低 | ESP32 有足够供电能力 |
| 影响其他纸系统电机 | 无 | 只修改面板电机逻辑 |

---

## 八、回退方案

如果修改后出现问题：

1. **禁用面板保持模式**：发送 `M903`
2. **回退代码**：
```bash
git checkout Grbl_Esp32/src/PaperSystem.cpp
git checkout Grbl_Esp32/src/Config.h
```

---

## 九、版本记录

| 日期 | 版本 | 修改内容 | 作者 |
|------|------|----------|------|
| 2026-06-02 | v1.0 | 初始版本 | Qoder |

