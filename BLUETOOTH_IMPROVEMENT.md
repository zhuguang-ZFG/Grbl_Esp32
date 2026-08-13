# 蓝牙信号与稳定性改进(2026-08-13)

## 背景

现场反馈:蓝牙信号弱、部分蓝牙发射器搜不到本机。经代码审查与实机排查,固件侧完成三项修复,并查清"搜不到"的根因。

## 固件改动

### 1. BR/EDR 发射功率拉满至 +9 dBm

- 文件:`Grbl_Esp32/src/WebUI/BTConfig.cpp`
- ESP32 经典蓝牙默认发射功率上限仅 +3 dBm(SDK 默认 `N0~P3`,见 `esp_bt.h` 注释)。
- 现在 `SerialBT.begin()` 成功后调用 `esp_bredr_tx_power_set(ESP_PWR_LVL_P9, ESP_PWR_LVL_P9)`,提升 6 dB,理论通信距离约翻倍。
- 失败时串口输出 `[MSG:BT tx power set failed <code>]`。
- **注意:+9 dBm 已是芯片硬上限**(`ESP_PWR_LVL_P9` 为枚举最大值),再往上只能改硬件(天线/电源)。
- **注意:功率加大后,蓝牙发射对换纸键的 EMI 干扰([ESP910] 抑制逻辑所防的毛刺)等比例变强,现场需留意误触发。**

### 2. 状态报告 Bf 字段修正(BT 客户端流控)

- 文件:`Grbl_Esp32/src/Serial.cpp`、`Serial.h`、`Report.cpp`
- 原实现 `512 - SerialBT.available()`(标注 TODO FIXME):容量数值错误,且积压超 512 字节时会向上位机报**负数**,用字符计数流控的上位机会错误限速甚至停发(表现为画圆/写字卡顿)。
- 现在 `client_get_rx_buffer_available(CLIENT_BT)` 返回真实的 2048 字节 InputBuffer 余量,扣除 SPP 队列积压、下限截 0;返回类型 `uint8_t` → `int`,避免 2048 截断。

### 3. SPP 回调瘦身(消除死锁/堆竞争隐患)

- 文件:`Grbl_Esp32/src/WebUI/BTConfig.cpp`、`BTConfig.h`
- SPP 回调运行在蓝牙协议栈任务(core 0)。原实现在回调里直接操作 Arduino `String`(与 core 1 读取方存在跨核堆竞争,偶发崩溃风险)并调用 `grbl_sendf(CLIENT_ALL, ...)` 回写 SPP(拥塞时可能卡死蓝牙任务)。
- 现在回调只把对端地址存入定长数组、置事件标志;`[MSG:BT Connected/Disconnected]` 的发送与 `_btclient` 更新推迟到 `BTConfig::handle()`(clientCheckTask,core 1)执行。
- `_btclient` 由 `String` 改为 `char[18]`。
- Paper 系统的 EMI 抑制钩子(`paper_btn_arm_bt_suppress` 等)保留在回调内立即生效——它们只置标志位,安全,且抑制窗口必须在连接瞬间开启。

## 排查结论(重要,供售后/产测参考)

### "有些蓝牙发射器搜不到本机"的根因

1. **设备名过滤**:奎享系上位机/发射器按固定设备名 `BtWriter` 搜索连接(官方教程明确写明)。本固件蓝牙名为 `pxkj`(`BTConfig.h` 的 `DEFAULT_BT_NAME`),按名字白名单自动连接的发射器会无视本机。实测两台机器(pxkj 与 BtWriter)广播的设备类别 CoD 完全相同(`0x02C110`,Computer 类),协议层无差别,能区分的只有名字。临时验证/兼容可用 `$Bluetooth/Name=BtWriter`(ESP140 设置)改名,重启生效,无需刷机。
2. **射频信号弱(硬件)**:Windows 系统搜索不按名过滤,若在别的电脑上仍搜不到本机而能搜到别的机器,说明本机查询应答信号弱到接收端听不见。廉价 CSR 克隆 dongle(VID_0A12/PID_0001)接收灵敏度差会放大此问题(该类 dongle 在 Win11 24H2 上驱动已被微软拒载,故障码 31,根本无法工作)。
3. **BLE 扫描器/音频发射器天然搜不到**:本机是经典蓝牙 SPP 设备,BLE-only 扫描器(小程序、flutter_blue 等)和只认 A2DP 音频设备的发射器不可能发现它,属正常现象(参见 arduino-esp32 issue #4929)。

### 硬件排查方向(信号弱,按优先级)

1. 天线净空:PCB 天线正下方及周边 10mm 内不得铺铜/被金属机壳、电机排线遮挡;量产可考虑 WROOM-32U(IPEX 外接天线)。
2. 电源完整性:蓝牙发射突发电流叠加 4 路 HR4988,示波器确认 3.3V 无明显跌落。
3. 接收端:建议搭配 RTL8761B 方案 dongle(Win11 免驱)。

## 验证记录

- `release` 环境编译通过(RAM 31.4% / Flash 94.4%),已烧录实机(COM3,ESP32-D0WD-V3)。
- 开机日志正常:`[MSG:BT Started with pxkj]`,无功率设置失败提示。
- Windows(内置 MediaTek 适配器)经典蓝牙近距离扫描可发现 `pxkj`(MAC b4:bf:e9:e6:3a:0e),广播正常。
- 待现场验证:手机同距离 RSSI 对比(预期 +6 dB)、发射器改名后能否发现、换纸键有无新增误触发。
