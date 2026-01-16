# 74HC595D GPIO扩展IO详细说明

## 📋 概述

本项目使用 **74HC595D** 8位串入并出移位寄存器来扩展ESP32的GPIO输出能力，实现换纸系统的多电机控制。

**为什么需要扩展IO？**
- ESP32的GPIO数量有限
- 换纸系统需要控制3个步进电机（6个信号：3个DIR + 3个STEP）
- 还需要控制LED指示灯和电机使能信号
- 使用74HC595D可以用3个GPIO控制8个输出

---

## 🔌 硬件连接

### 1. 控制信号连接

| 功能 | ESP32引脚 | 74HC595D引脚 | 引脚名称 | 说明 |
|------|-----------|--------------|----------|------|
| 串行数据输入 | **GPIO32** | Pin 14 | SI (Serial Input) | 数据线，MSB优先 |
| 移位寄存器时钟 | **GPIO3** | Pin 11 | SRCLK (Shift Clock) | 移位时钟，上升沿移入数据 |
| 存储寄存器时钟 | **GPIO5** | Pin 12 | RCLK (Register Clock) | 锁存时钟，上升沿锁存到输出 |
| 复位信号 | **VCC** | Pin 10 | SRCLR (Clear) | 高电平不复位，保持输出 |
| 输出使能 | **GND** | Pin 13 | OE (Output Enable) | 低电平使能输出 |
| 电源 | **3.3V** | Pin 16 | VCC | 逻辑电源 |
| 地 | **GND** | Pin 8 | GND | 电源地 |

### 2. 输出信号映射

| 74HC595输出 | 物理引脚 | 代码位定义 | 连接目标 | 功能说明 | 控制逻辑 |
|-------------|----------|-----------|----------|----------|----------|
| **Q0** | Pin 15 | BIT_BUTTON_LED_CONTROL (0) | 按钮LED | 一键换纸按钮指示灯 | HIGH=灭, LOW=亮 |
| **Q1** | Pin 1 | BIT_PAPER_MOTORS_ENABLE (1) | HR4988使能 | 换纸电机使能控制 | LOW=使能, HIGH=禁用 |
| **Q2** | Pin 2 | BIT_PAPER_CLAMP_DIR (2) | HR4988 Pin 19 | 压纸抬落电机方向 | HIGH/LOW |
| **Q3** | Pin 3 | BIT_PAPER_CLAMP_STEP (3) | HR4988 Pin 16 | 压纸抬落电机步进 | 脉冲信号 |
| **Q4** | Pin 4 | BIT_PANEL_MOTOR_DIR (4) | HR4988 Pin 19 | 面板电机方向 | HIGH/LOW |
| **Q5** | Pin 5 | BIT_PANEL_MOTOR_STEP (5) | HR4988 Pin 16 | 面板电机步进 | 脉冲信号 |
| **Q6** | Pin 6 | BIT_FEED_MOTOR_DIR (6) | HR4988 Pin 19 | 进纸器电机方向 | HIGH/LOW |
| **Q7** | Pin 7 | BIT_FEED_MOTOR_STEP (7) | HR4988 Pin 16 | 进纸器电机步进 | 脉冲信号 |

---

## 💻 软件实现

### 1. 位映射定义

**文件**: `Grbl_Esp32/src/PaperChangeConfig.h`

```cpp
// HC595位映射定义 - 电机控制位分配
#define BIT_BUTTON_LED_CONTROL   0     // Q0 - 按钮指示灯控制位
#define BIT_PAPER_MOTORS_ENABLE  1     // Q1 - 换纸电机使能控制位
#define BIT_PAPER_CLAMP_DIR     2     // Q2 - 夹纸电机方向控制位
#define BIT_PAPER_CLAMP_STEP    3     // Q3 - 夹纸电机步进控制位
#define BIT_PANEL_MOTOR_DIR      4     // Q4 - 面板电机方向控制位
#define BIT_PANEL_MOTOR_STEP     5     // Q5 - 面板电机步进控制位
#define BIT_FEED_MOTOR_DIR       6     // Q6 - 进纸电机方向控制位
#define BIT_FEED_MOTOR_STEP      7     // Q7 - 进纸电机步进控制位
```

### 2. 初始化函数

**文件**: `Grbl_Esp32/src/PaperChangeHardware.cpp`

```cpp
void hc595_init() {
    // 1. 引脚冲突检测
    // 检查HC595引脚是否与XYZ轴STEP引脚冲突
    // 检查HC595引脚之间是否冲突
    
    // 2. 配置GPIO为输出模式
    pinMode(HC595_DATA_PIN, OUTPUT);    // GPIO32
    pinMode(HC595_CLOCK_PIN, OUTPUT);   // GPIO3
    pinMode(HC595_LATCH_PIN, OUTPUT);   // GPIO5
    
    // 3. 初始状态：所有输出为0
    hc595_write_fast(0);
}
```

### 3. 数据写入函数

#### 标准写入函数（可靠性优先）

```cpp
void hc595_write(uint8_t data) {
    // 步骤1：拉低锁存信号，准备接收数据
    digitalWrite(HC595_LATCH_PIN, LOW);
    
    // 步骤2：串行移入8位数据（MSB优先）
    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(HC595_CLOCK_PIN, LOW);  // 时钟下降沿
        
        // 设置数据位（从MSB到LSB）
        digitalWrite(HC595_DATA_PIN, (data & (0x80 >> i)) ? HIGH : LOW);
        
        delayMicroseconds(1);              // 数据建立时间
        digitalWrite(HC595_CLOCK_PIN, HIGH); // 时钟上升沿，移入数据
        delayMicroseconds(1);              // 数据保持时间
    }
    
    // 步骤3：拉高锁存信号，将数据锁存到输出寄存器
    digitalWrite(HC595_LATCH_PIN, HIGH);
    
    // 步骤4：等待输出稳定
    delayMicroseconds(HC595_UPDATE_DELAY_US);  // 10μs
}
```

#### 高性能写入函数（速度优先）

```cpp
void hc595_write_fast(uint8_t data) {
    // 使用ESP32寄存器直接操作，速度更快
    GPIO.out_w1tc = (1ULL << HC595_LATCH_PIN);  // 拉低锁存
    
    uint8_t mask = 0x80;  // MSB掩码
    for (uint8_t i = 0; i < 8; i++) {
        GPIO.out_w1tc = (1ULL << HC595_CLOCK_PIN);  // 时钟拉低
        
        // 设置数据位（GPIO32在GPIO1寄存器中）
        if (data & mask) {
            GPIO.out1.val = (1ULL << (HC595_DATA_PIN - 32));
        } else {
            GPIO.out1.val &= ~(1ULL << (HC595_DATA_PIN - 32));
        }
        
        GPIO.out_w1ts = (1ULL << HC595_CLOCK_PIN);  // 时钟拉高
        mask >>= 1;  // 移到下一位
        delayMicroseconds(1);
    }
    
    GPIO.out_w1ts = (1ULL << HC595_LATCH_PIN);  // 拉高锁存
    delayMicroseconds(HC595_UPDATE_DELAY_US);
}
```

### 4. 位操作函数

```cpp
static inline void hc595_set_bit(uint8_t bit_pos, bool value) {
    paper_change_ctrl_t* ctrl = get_paper_control();
    
    // 获取当前输出状态
    uint8_t new_output = ctrl->hc595_output;
    
    // 设置指定位
    if (value) {
        new_output |= (1 << bit_pos);   // 置1
    } else {
        new_output &= ~(1 << bit_pos);  // 置0
    }
    
    // 如果状态改变，更新硬件
    if (new_output != ctrl->hc595_output) {
        hc595_write_fast(new_output);
        ctrl->hc595_output = new_output;
    }
}
```

### 5. 批量更新器（性能优化）

```cpp
class HC595BatchUpdater {
private:
    bool dirty = false;
    uint8_t pending_output = 0;
    
public:
    // 设置位，但不立即写入硬件
    void set_bit(uint8_t pos, bool value) {
        if (value) {
            pending_output |= (1 << pos);
        } else {
            pending_output &= ~(1 << pos);
        }
        dirty = true;  // 标记需要更新
    }
    
    // 批量写入硬件（减少写入次数）
    void flush() {
        if (dirty) {
            hc595_write_fast(pending_output);
            dirty = false;
        }
    }
};
```

---

## ⚙️ 工作原理

### 1. 移位寄存器工作原理

```
数据流向：
ESP32 GPIO32 (SI) → 74HC595D 移位寄存器 → 存储寄存器 → Q0-Q7输出

时序：
1. LATCH拉低（准备接收）
2. 8个时钟周期，从MSB到LSB移入数据
3. LATCH拉高（锁存到输出）
4. Q0-Q7输出更新
```

### 2. 数据格式

**8位数据格式**（bit7对应Q7，bit0对应Q0）：

```
bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
 Q7    Q6    Q5    Q4    Q3    Q2    Q1    Q0
STEP  DIR   STEP  DIR   STEP  DIR   EN    LED
进纸  进纸  面板  面板  夹紧  夹紧  使能  指示灯
```

**示例**：
- `0x00` = 所有输出低电平（所有电机停止，LED亮）
- `0xFF` = 所有输出高电平（所有电机方向高，LED灭）
- `0x04` = 仅Q2高电平（夹紧电机方向高）
- `0x08` = 仅Q3高电平（夹紧电机步进脉冲）

### 3. 电机控制时序

```cpp
void motor_step_control(uint8_t step_bit, uint8_t dir_bit, bool forward, uint32_t step_interval) {
    // 1. 设置方向（需要稳定时间）
    hc595_set_bit(dir_bit, !forward);  // HR4988: 0=正转, 1=反转
    hc595_update();
    delayMicroseconds(10);  // 方向稳定时间
    
    // 2. 生成步进脉冲（上升沿触发）
    hc595_set_bit(step_bit, HIGH);  // 脉冲上升沿
    delayMicroseconds(5);            // 脉冲宽度
    hc595_set_bit(step_bit, LOW);   // 脉冲下降沿
    hc595_update();
    
    // 3. 步进间隔（控制速度）
    delayMicroseconds(step_interval);
}
```

---

## 🔒 安全机制

### 1. 引脚冲突检测

代码在初始化时自动检测引脚冲突：

```cpp
// 检查HC595引脚是否与XYZ轴STEP引脚冲突
if (HC595_LATCH_PIN == X_STEP_PIN) {
    // 错误：引脚冲突！
    grbl_sendf(CLIENT_ALL, "[MSG: Error: HC595_LATCH_PIN conflicts with X_STEP_PIN]\n");
    return;  // 停止初始化
}

// 检查HC595引脚之间是否冲突
if (HC595_DATA_PIN == HC595_CLOCK_PIN) {
    // 错误：引脚冲突！
    return;
}
```

### 2. 互斥控制

**双重互斥机制**确保同一时间只有一组电机工作：

```cpp
// GPIO26硬件控制（一级保护）
// GPIO26=LOW: XYZ轴使能，换纸电机禁用
// GPIO26=HIGH: XYZ轴禁用，换纸电机使能

// HC595 Q1软件控制（二级确认）
// Q1=HIGH: 换纸电机禁用，XYZ轴可以工作
// Q1=LOW: 换纸电机使能，XYZ轴禁用
```

---

## 📊 性能特性

### 1. 时序参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 数据建立时间 | 1μs | 时钟上升沿前数据稳定时间 |
| 数据保持时间 | 1μs | 时钟上升沿后数据保持时间 |
| 时钟脉宽 | 1μs | 时钟高/低电平最小宽度 |
| 锁存稳定时间 | 10μs | 锁存后输出稳定时间 |
| 总写入时间 | ~20μs | 完整8位数据写入时间 |

### 2. 性能对比

| 函数 | 写入时间 | 适用场景 |
|------|---------|----------|
| `hc595_write()` | ~50μs | 调试、可靠性优先 |
| `hc595_write_fast()` | ~20μs | 生产、速度优先 |
| `HC595BatchUpdater` | 批量更新 | 多次位操作时优化 |

---

## 🛠️ 使用示例

### 1. 控制按钮LED

```cpp
// LED亮
set_button_led(true);   // Q0=LOW

// LED灭
set_button_led(false);  // Q0=HIGH
```

### 2. 控制换纸电机使能

```cpp
// 使能换纸电机
enable_paper_motors();  // GPIO26=LOW, Q1=LOW

// 禁用换纸电机
disable_paper_motors(); // GPIO26=HIGH, Q1=HIGH
```

### 3. 控制电机步进

```cpp
// 进纸电机正转一步
motor_step_control(
    BIT_FEED_MOTOR_STEP,    // 步进位
    BIT_FEED_MOTOR_DIR,     // 方向位
    true,                   // 正转
    2000                    // 2000μs间隔（控制速度）
);
```

### 4. 直接写入数据

```cpp
// 所有输出低电平
hc595_write(0x00);

// 所有输出高电平
hc595_write(0xFF);

// 仅Q2和Q3高电平（夹紧电机方向+步进）
hc595_write(0x0C);  // 0b00001100
```

---

## 🐛 调试和故障排除

### 1. 调试命令

```gcode
M806 0x00    ; 所有输出低电平
M806 0xFF    ; 所有输出高电平
M806 0x04    ; 仅Q2高电平（测试夹紧电机方向）
```

### 2. 常见问题

#### 问题1：HC595输出无响应

**可能原因**：
- 引脚连接错误
- 电源未连接
- OE引脚未接地

**排查步骤**：
1. 检查GPIO32/3/5连接
2. 检查74HC595D Pin 16 (VCC) = 3.3V
3. 检查74HC595D Pin 13 (OE) = GND
4. 使用示波器检查时钟信号

#### 问题2：输出状态错误

**可能原因**：
- 数据位顺序错误
- 时序问题

**排查步骤**：
1. 验证位映射定义是否正确
2. 检查时序参数（建立时间、保持时间）
3. 使用逻辑分析仪捕获SPI时序

#### 问题3：电机不动作

**可能原因**：
- HC595输出未连接到HR4988
- 使能信号未正确设置

**排查步骤**：
1. 检查Q2-Q7到HR4988的连接
2. 检查Q1使能信号
3. 检查GPIO26使能信号

---

## 📈 扩展建议

### 1. 级联多个74HC595D

如果需要更多输出，可以级联多个74HC595D：

```
ESP32 → 74HC595D #1 → 74HC595D #2 → 74HC595D #3
        (8位)      (8位)      (8位)
```

**实现方式**：
- 第一个74HC595D的Q7连接到第二个的SI
- 共享时钟和锁存信号
- 一次写入16位或24位数据

### 2. 添加输入扩展

如果需要扩展输入，可以使用74HC165（并入串出移位寄存器）：

```
传感器 → 74HC165 → ESP32 GPIO
```

---

## 📝 代码文件位置

| 功能 | 文件路径 |
|------|---------|
| 位映射定义 | `Grbl_Esp32/src/PaperChangeConfig.h` |
| 硬件实现 | `Grbl_Esp32/src/PaperChangeHardware.cpp` |
| 接口声明 | `Grbl_Esp32/src/PaperChangeHardware.h` |
| 配置参数 | `Grbl_Esp32/src/Machines/custom_3axis_hr4988.h` |

---

## ✅ 验证清单

- [x] HC595初始化成功
- [x] 引脚冲突检测正常
- [x] 数据写入功能正常
- [x] 位操作功能正常
- [x] 电机控制功能正常
- [x] LED控制功能正常
- [x] 使能控制功能正常
- [x] 批量更新器工作正常

---

**文档版本**: v1.0  
**最后更新**: 2026-01-15  
**适用硬件**: ESP32-WROOM-32E + 74HC595D
