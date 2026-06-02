# 电机静音优化方案（最终版）

## 优化总结

| 电机 | 优化措施 | 最终参数 | 状态 |
|------|----------|----------|------|
| 拾落电机 | 提频降流 | 1200us/1200us, DAC=130 | ✓ 已实施 |
| 面板电机 | 均匀脉冲降流 | 100us/100us, DAC=60 | ✓ 已实施 |
| 出纸速度 | 提速13% | 38us/38us | ✓ 已实施 |

---

## 1. 拾落电机静音优化

### 问题分析
- **频率问题**：178Hz 正好在人耳敏感范围（100-200Hz）
- **电流问题**：拾落电机电流较高（DAC=150）

### 实施方案
```cpp
// custom_3axis_hr4988.h
#define PAPER_CLAMP_HI_US        1200u  // 原 2800us → 1200us
#define PAPER_CLAMP_LO_US        1200u
#define PAPER_REF_DAC_CLAMP      130    // 原 150 → 130（降13%，保留足够扭矩）
```

### 修复记录
- **DAC顺序修复**：步骤3中 `paper_set_ref_dac` 移到 `paper_enable_panel_and_feeder()` 之后，避免软启动覆盖为零电流
- **M716修复**：添加拾落电机DAC设置，确保所有路径使用优化参数

### 状态上报
```
[PaperMotor] Clamp: freq=416Hz, ref_voltage_mV=1682 (DAC=130)
```

---

## 2. 面板电机静音优化

### 问题分析
- **脉冲不均匀**：快速模式 75us/150us 导致振动模式不均匀
- **电流过大**：DAC=80

### 实施方案
```cpp
// custom_3axis_hr4988.h
#define PAPER_PANEL_FAST_HI_US   100u   // 原 75us → 100us（均匀化）
#define PAPER_PANEL_FAST_LO_US   100u   // 原 150us → 100us（均匀化）
#define PAPER_REF_DAC_PANEL      60     // 原 80 → 60（降25%）
```

### 状态上报
```
[PaperMotor] Panel+Feeder sync: panel_dac=60, feeder_dac=80, freq=3333Hz
```

---

## 3. 出纸速度提升

### 问题分析
- 当前出纸时间：约 695ms
- 有提速空间，且噪音不会增大

### 实施方案
```cpp
// custom_3axis_hr4988.h
#define PAPER_EJECT_RAMP_HI_US    120u   // 原 136us → 120us
#define PAPER_EJECT_RAMP_LO_US    120u
#define PAPER_EJECT_NORMAL_HI_US  38u    // 原 43us → 38us
#define PAPER_EJECT_NORMAL_LO_US  38u
```

### 提速效果
| 指标 | 修改前 | 修改后 |
|------|--------|--------|
| 脉宽 | 43us/43us | 38us/38us |
| 频率 | 11628Hz | 13158Hz |
| 出纸时间 | 695ms | 615ms |
| 噪音 | 低 | 更低（更接近超声波）|

---

## 频率分析

| 电机 | 脉宽 | 频率 | 人耳敏感度 |
|------|------|------|------------|
| 拾落电机 | 1200us/1200us | 417Hz | 低 |
| 面板电机(起步) | 400us/400us | 1250Hz | 低 |
| 面板电机(正常) | 150us/150us | 3333Hz | 低 |
| 面板电机(快速) | 100us/100us | 5000Hz | 低 |
| 出纸 | 38us/38us | 13158Hz | 低（超声波边缘）|

**人耳敏感范围：100-200Hz，所有电机频率均高于此范围**

---

## Superpowers 原则实施记录

| 原则 | 措施 | 状态 |
|------|------|------|
| **主动控制** | 拾落电机频率提升至416Hz | ✓ |
| **主动控制** | 面板电机脉冲均匀化 | ✓ |
| **主动控制** | 电流优化（拾落130，面板60）| ✓ |
| **预通知** | 操作前上报电机参数 | ✓ |
| **状态可见** | 添加[PaperMotor]状态上报 | ✓ |
| **优雅降级** | 保留加减速曲线 | ✓ |

---

## Commit 记录

| Commit | 说明 |
|--------|------|
| b5bfffd | feat: 出纸速度提升13%（Superpowers原则） |
| 3d325c5 | feat: 面板电机静音优化（Superpowers原则闭环） |
| 977df24 | fix: 拾落电机扭矩不足 - 修复DAC顺序并提高电流 |
| d13d07d | feat: 拾落电机静音优化（Superpowers原则） |
| 3331846 | fix: 写字模式默认启用 (panel_hold_mode=true) |
