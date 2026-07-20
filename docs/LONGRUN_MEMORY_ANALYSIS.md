# 长跑内存/稳定性分析（九轮审查后，静态结论）

**问题：** 长时间运行是否有内存泄漏/死机风险？
**方法：** 静态审查三类风险源 — 堆分配泄漏、String 累积、计数器/时间戳溢出。
**结论：静态层面未发现会导致长跑泄漏或死机的缺陷。** 关键路径均为有界设计。
（注：静态结论 ≠ HIL；连续打印数天的真机 heap 曲线仍应作为发货前观测项。）

---

## 1. 堆分配（new/malloc）—— 无泄漏

| 位置 | 性质 | 判定 |
|------|------|------|
| `Report.cpp:80/119/155` `new char[len+1]` | **热路径**（长消息 grbl_send/sendf/notifyf） | ✅ 三处均 `if (temp != loc_buf) delete[] temp` 配对，短消息走 100B 栈缓冲不分配；`va_end` 顺序正确 |
| `I2SOut.cpp:759-800` malloc/heap_caps | init 一次性（DMA 缓冲） | ✅ 启动期一次，不释放也不重复 |
| `System.cpp:141-150` new DigitalOutput/AnalogOutput | init 一次性 | ✅ 全局对象，生命周期=整机 |
| `WebServer/Telnet new` | 仅 ENABLE_WIFI，begin/end 配对 | ✅ 产品 WiFi 关不涉及；WiFi 构建下 end() 已修 detachWS（六轮） |

**结论：** 无"每次操作 new 但从不 free"的路径。热路径 Report 分配全部配对释放。

## 2. String 累积 / 堆碎片 —— 无累积

- **BT 发送**（最热路径）：用**固定容量环形缓冲** `bt_tx_ring_storage[BT_TX_RING_SIZE]`
  （BTState.cpp:52，静态数组），**非动态 String**。有界，不累积。
- `Report.cpp:207/359` 的 `String +=` 是**函数局部** String，返回即析构，不跨调用累积。
  会产生短期堆碎片，但 ESP32 heap 分配器可回收，无单调增长。
- 无全局/静态 String 被逐次 `+=`/`concat` 追加的路径。

## 3. 计数器 / 时间戳溢出 —— 回绕安全

- `millis()` 回绕（49.7 天）：所有比较用**无符号差值**（`now - start`），回绕安全。
  已核实点：按钮双击窗口、BT-EOL gap、0x18 忽略窗口、通知超时。
- `line_number`（int32）、`sd_current_line_number`（uint32）：单调递增仅用于**报告显示**，
  溢出只影响显示数字，不影响运行逻辑。

## 4. 现成监控手段
- `heapCheckTask`（Serial.cpp:86）：定义 `DEBUG_REPORT_HEAP_SIZE` 后每次 heap 变化打印
  `heap <bytes>`。**HIL 长跑时建议开启**，观测 heap 是否单调下降。

---

## HIL 长跑观测项（发货前建议）
- [ ] 开 `DEBUG_REPORT_HEAP_SIZE`，连续多页/多小时写字，看 `heap` 是否稳定（不单调降）
- [ ] 连续换纸循环数百次，观测 heap 与响应无退化
- [ ] BT 断连/重连反复，确认 TX ring 与 String 无残留增长
- [ ] 跨 `millis()` 回绕点（理论上需运行 ~49 天，或改系统时钟基准注入测试）验证时间比较无异常

**判据：** heap 稳定在启动值附近波动（分配器碎片正常抖动），无跨小时的单调下降趋势。
