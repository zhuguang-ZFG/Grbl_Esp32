# 画圆卡顿优化方案 - Superpowers 闭环执行

## 问题分析

| 风险 | 场景 | 影响 | 优先级 |
|------|------|------|--------|
| Segment buffer 不足 | 蓝牙延迟 > 10ms | 卡顿 | 高 |
| 小圆段长过短 | 半径 < 2mm | 速度降低 | 中 |
| 蓝牙延迟 | 高速连续发送 | 饥饿 | 中 |

## 当前配置

| 参数 | 当前值 | 说明 |
|------|--------|------|
| SEGMENT_BUFFER_SIZE | 12 | 步进段缓冲区（已优化） |
| BLOCK_BUFFER_SIZE | 80 | 规划块缓冲区 |
| DT_SEGMENT | 1.67ms | 每段执行时间 |
| arc_tolerance | 0.002mm | 圆弧容差 |
| N_ARC_CORRECTION | 12 | 每12段精确修正 |

---

## Superpowers 原则执行计划

### 原则 1：主动控制

**优化 1：增大 Segment Buffer**
```cpp
// Stepper.h
#define SEGMENT_BUFFER_SIZE 12  // 从6增大到12
```

**效果**：
- 缓冲区深度：10ms → 20ms
- 容忍蓝牙延迟能力翻倍
- 减少 underflow 卡顿

**风险评估**：
- RAM 增加：约 120 bytes（12 × 10 bytes/segment）
- ESP32 有足够 RAM，风险极低

### 原则 2：状态可见

**添加圆弧段数上报**
```cpp
// MotionControl.cpp - mc_arc 函数
grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info,
    "[Arc] radius=%.2fmm, segments=%u, seg_len=%.3fmm",
    radius, segments, segment_length);
```

### 原则 3：预通知

**已有机制**：
- segment_buffer_underflow 标志
- LOW_BUFFER 消息

### 原则 4：优雅降级

**已有机制**：
- yield() 避免 WDT
- plan_check_full_buffer() 忙等待时 yield

---

## 执行步骤

| 步骤 | 原则 | 修改内容 | 文件 |
|------|------|----------|------|
| 1 | 主动控制 | SEGMENT_BUFFER_SIZE 6→12 | Stepper.h | ✅ |
| 2 | 状态可见 | 添加圆弧段数上报 | MotionControl.cpp | ✅ |
| 3 | 验证 | 编译烧录测试 | - | ✅ |
| 4 | 文档 | 更新文档 | - | ✅ |
