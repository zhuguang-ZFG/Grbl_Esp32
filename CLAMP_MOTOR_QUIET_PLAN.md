# 拾落电机静音优化方案

## 问题分析

### 当前配置
| 参数 | 当前值 | 说明 |
|------|--------|------|
| 脉冲时序 | 2800us/2800us | 频率约 178Hz |
| DAC 电流 | 150 | 约 1.94V，对应约 1.5A |
| 面板电机电流 | 80 | 约 1.04V，对应约 0.8A |

### 问题根源
1. **频率问题**：178Hz 正好在人耳敏感范围（100-200Hz）
2. **电流问题**：拾落电机电流较高（150 vs 面板 80）
3. **共振问题**：步进电机在低频下容易产生共振

---

## Superpowers 原则解决方案

### 原则 1：主动控制（Active Control）

**方案 A：提高脉冲频率**
```cpp
// 将拾落电机频率提高到 400Hz 以上
#define PAPER_CLAMP_HI_US   1200u   // 原 2800us → 1200us
#define PAPER_CLAMP_LO_US   1200u   // 频率从 178Hz → 416Hz
```

**方案 B：降低电流（静音模式）**
```cpp
// 降低拾落电机电流
#define PAPER_REF_DAC_CLAMP   100   // 原 150 → 100（约 1.3V，1.0A）
```

**方案 C：加减速曲线优化**
```cpp
// 添加 S 曲线加减速
#define PAPER_CLAMP_RAMP_STEPS  20u
#define PAPER_CLAMP_RAMP_HI_US  4000u  // 启动时慢速
#define PAPER_CLAMP_RAMP_LO_US  4000u
```

### 原则 2：预通知机制（Pre-notification）

```cpp
// 在拾落操作前通知上位机
grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperEvent] CLAMP_MOTOR_START (noise mode=%s)", 
               quiet_mode ? "QUIET" : "NORMAL");
```

### 原则 3：状态可见（State Visibility）

```cpp
// 上报当前拾落电机状态
grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, 
    "[PaperMotor] Clamp: freq=%uHz, current=%u, mode=%s",
    (unsigned)(1000000 / (PAPER_CLAMP_HI_US + PAPER_CLAMP_LO_US)),
    (unsigned)PAPER_REF_DAC_CLAMP,
    quiet_mode ? "QUIET" : "NORMAL");
```

### 原则 4：优雅降级（Graceful Degradation）

```cpp
// 如果检测到异常（如堵转），自动降低电流
if (detect_stall()) {
    paper_set_ref_dac(PAPER_REF_DAC_CLAMP / 2);  // 电流减半
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning, "[PaperMotor] Stall detected, reducing current");
}
```

---

## 实施计划

### 阶段 1：快速优化（立即生效）
1. 调整脉冲频率：从 2800us → 1200us（提高到 416Hz）
2. 降低电流：从 150 → 100（降低 33%）
3. 编译测试

### 阶段 2：功能增强
4. 添加加减速曲线：减少启动/停止时的噪音
5. 添加静音模式开关：M 命令控制
6. 状态上报：让上位机知道当前模式

### 阶段 3：智能优化
7. 自适应电流：根据负载自动调整
8. 堵转检测：异常时自动降级

---

## 预期效果

| 指标 | 当前 | 优化后 |
|------|------|--------|
| 脉冲频率 | 178Hz | 416Hz |
| 电流 | 1.5A | 1.0A |
| 噪音水平 | 高 | 低 |
| 人耳敏感度 | 高 | 低（超出敏感范围）|
