# SEG Underflow 闭环验证方案

## 一、验证目标

验证固件修改是否解决了蓝牙断流导致的 SEG underflow 问题。

---

## 二、验证前准备

### 2.1 烧录固件

```bash
# 方式1：使用 PlatformIO 烧录
cd d:\Grbl_Esp32
python -m platformio run -e release --target upload

# 方式2：使用 esptool 烧录（如果 PlatformIO 烧录失败）
python -m esptool --chip esp32 --port COM28 --baud 921600 write_flash 0x0 .pio/build/release/firmware.bin
```

### 2.2 连接设备

1. 确保 ESP32 已通过蓝牙配对
2. 打开串口终端（如 PuTTY、CoolTerm）
3. 设置波特率：115200
4. 连接到蓝牙串口（通常是 COM28 或类似端口）

---

## 三、验证步骤

### 步骤 1：基础功能验证

**目标**：确认固件正常启动，预通知功能正常

**操作**：
1. 连接串口终端
2. 发送 `?` 查询状态
3. 发送简单的 G 代码测试

**预期日志**：
```
<Idle|MPos:0.000,0.000,0.000|FS:0,0|WCO:0.000,0.000,0.000|Bf:79,511>
ok
```

**检查点**：
- [ ] 能正常连接
- [ ] 状态报告正常
- [ ] Bf: 字段显示 buffer 水位

---

### 步骤 2：预通知验证

**目标**：验证 M30 预通知功能

**操作**：
1. 发送以下 G 代码序列：
```gcode
G90G0 X0Y0
G1 X100 Y100 F8000
M30
```

**预期日志**：
```
ok
ok
[MSG:PAGE_END_IMMINENT]
[MSG:PAPER_OK]
ok
```

**检查点**：
- [ ] 收到 `[MSG:PAGE_END_IMMINENT]` 预通知
- [ ] 预通知在 M30 执行前发送
- [ ] 换纸流程正常完成

---

### 步骤 3：Buffer 水位监控验证

**目标**：验证水位监控功能

**操作**：
1. 发送大量连续 G 代码（模拟长时间运行）：
```gcode
G90G0 X0Y0
G1 X100 Y0 F8000
G1 X100 Y100 F8000
G1 X0 Y100 F8000
G1 X0 Y0 F8000
... (重复 100 次)
M30
```

**预期日志**：
- 如果 buffer 水位正常：无额外输出
- 如果 buffer 水位低：`[MSG:LOW_BUFFER B=xx]`

**检查点**：
- [ ] 正常情况下无 `[MSG:LOW_BUFFER]` 消息
- [ ] 低水位时能收到警告消息
- [ ] Buffer 水位数值合理（Bf:79,511 正常）

---

### 步骤 4：SEG Underflow 验证（核心测试）

**目标**：验证是否还有 SEG underflow

**操作**：
1. 准备一个包含多页的 G 代码文件（测试文件见附录 A）
2. 通过蓝牙发送完整文件
3. 观察串口日志

**预期日志**：
```
... (正常 G 代码执行)
[MSG:PAGE_END_IMMINENT]     ← 预通知
[MSG:PAPER_OK]              ← 换纸成功
... (下一页 G 代码)
[BT] Page end detected, waiting for next page...  ← 换页场景（静默处理）
[MSG:PAGE_END_IMMINENT]     ← 预通知
[MSG:PAPER_OK]              ← 换纸成功
... (重复)
```

**检查点**：
- [ ] 没有 `[SEG underflow]` 警告
- [ ] 换页时有 `[MSG:PAGE_END_IMMINENT]` 预通知
- [ ] 换纸流程正常完成
- [ ] 打印质量正常（无停顿、无错位）

---

### 步骤 5：蓝牙断流模拟测试

**目标**：模拟蓝牙断流场景，验证恢复能力

**操作**：
1. 发送 G 代码
2. 在执行过程中，手动暂停蓝牙连接（约 2-3 秒）
3. 重新连接蓝牙
4. 观察日志

**预期日志**：
```
... (正常执行)
[MSG:LOW_BUFFER B=15]       ← 低水位警告
... (蓝牙断流期间)
[BT] Page end detected, waiting for next page...  ← 换页场景
... (蓝牙恢复后)
[MSG:PAGE_END_IMMINENT]     ← 预通知
[MSG:PAPER_OK]              ← 换纸成功
```

**检查点**：
- [ ] 断流期间能收到低水位警告
- [ ] 恢复后能正常继续执行
- [ ] 没有 `[SEG underflow]` 警告
- [ ] 打印质量正常

---

## 四、验证检查清单

### 4.1 功能验证

| 序号 | 检查项 | 预期结果 | 实际结果 | 状态 |
|------|--------|----------|----------|------|
| 1 | 固件启动 | 正常启动，无错误 | | |
| 2 | 状态查询 | 正常响应 Bf: 字段 | | |
| 3 | 预通知发送 | M30 前收到 [MSG:PAGE_END_IMMINENT] | | |
| 4 | 水位监控 | 低水位时收到 [MSG:LOW_BUFFER] | | |
| 5 | 换纸流程 | 正常完成，收到 [MSG:PAPER_OK] | | |

### 4.2 性能验证

| 序号 | 检查项 | 预期结果 | 实际结果 | 状态 |
|------|--------|----------|----------|------|
| 1 | SEG underflow | 无 [SEG underflow] 警告 | | |
| 2 | 打印质量 | 无停顿、无错位 | | |
| 3 | 连续运行 | 能完成多页打印 | | |
| 4 | 蓝牙稳定性 | 断流后能恢复 | | |

### 4.3 边界测试

| 序号 | 检查项 | 预期结果 | 实际结果 | 状态 |
|------|--------|----------|----------|------|
| 1 | 高速打印 | 无异常 | | |
| 2 | 低速打印 | 无异常 | | |
| 3 | 长时间运行 | 无内存泄漏 | | |
| 4 | 频繁换页 | 无异常 | | |

---

## 五、问题排查

### 5.1 如果仍有 SEG underflow

**可能原因**：
1. 蓝牙断流时间过长（> 5 秒）
2. Buffer 配置仍不够大
3. 上位机发送速度过快

**排查步骤**：
1. 检查日志中 `[MSG:LOW_BUFFER]` 出现的频率
2. 检查蓝牙连接质量
3. 尝试增大 `SEGMENT_BUFFER_SIZE` 到 32

### 5.2 如果预通知未发送

**可能原因**：
1. M30 检测逻辑有误
2. 串口输出被阻塞

**排查步骤**：
1. 检查 GCode.cpp 修改是否正确
2. 添加调试日志确认 M30 检测

### 5.3 如果水位监控未触发

**可能原因**：
1. 检查间隔太长
2. 阈值设置不合理

**排查步骤**：
1. 检查 `BUFFER_CHECK_INTERVAL_MS` 设置
2. 检查 `BUFFER_LOW_THRESHOLD` 设置

---

## 六、附录

### 附录 A：测试 G 代码文件

创建文件 `test_multi_page.gcode`：

```gcode
; 第一页
G90G0 X0Y0
G1 X100 Y0 F8000
G1 X100 Y100 F8000
G1 X0 Y100 F8000
G1 X0 Y0 F8000
M30

; 第二页（上位机会自动发送）
G90G0 X0Y0
G1 X50 Y0 F8000
G1 X50 Y50 F8000
G1 X0 Y50 F8000
G1 X0 Y0 F8000
M30

; 第三页
G90G0 X0Y0
G1 X75 Y0 F8000
G1 X75 Y75 F8000
G1 X0 Y75 F8000
G1 X0 Y0 F8000
M30
```

### 附录 B：日志分析命令

```bash
# 保存串口日志到文件
python -m serial.tools.miniterm COM28 115200 > serial_log.txt

# 分析日志中的关键事件
findstr /C:"SEG underflow" serial_log.txt
findstr /C:"PAGE_END_IMMINENT" serial_log.txt
findstr /C:"LOW_BUFFER" serial_log.txt
findstr /C:"PAPER_OK" serial_log.txt
```

### 附录 C：回退方案

如果验证失败，可以回退修改：

```bash
# 回退 GCode.cpp
git checkout Grbl_Esp32/src/GCode.cpp

# 回退 Protocol.cpp
git checkout Grbl_Esp32/src/Protocol.cpp

# 回退 Config.h
git checkout Grbl_Esp32/src/Config.h

# 重新编译
python -m platformio run -e release
```

---

## 七、验证结论

### 验证结果

| 验证项 | 结果 | 备注 |
|--------|------|------|
| 预通知功能 | | |
| 水位监控 | | |
| Underflow 优化 | | |
| Buffer 配置 | | |

### 最终结论

- [ ] 验证通过，问题已解决
- [ ] 验证部分通过，需要进一步优化
- [ ] 验证失败，需要回退修改

### 后续行动

根据验证结果决定：

1. **验证通过**：
   - 提交代码到版本库
   - 更新文档
   - 部署到生产环境

2. **部分通过**：
   - 分析失败原因
   - 调整参数或代码
   - 重新验证

3. **验证失败**：
   - 回退修改
   - 重新分析问题
   - 制定新的解决方案

---

## 八、版本记录

| 日期 | 版本 | 修改内容 | 作者 |
|------|------|----------|------|
| 2026-06-02 | v1.0 | 初始版本 | Qoder |

