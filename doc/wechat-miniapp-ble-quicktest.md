# 微信小程序 BLE 最小对接测试清单（Grbl_Esp32）

本文用于快速验证小程序是否可通过 BLE 控制当前固件（经典蓝牙 SPP + BLE NUS 并存版本）。

## 1. 固件侧前提

- 已编译并烧录最新固件。
- 上电后串口日志出现：`[MSG:BLE UART NUS advertising as ...]`
- 板子蓝牙名与经典蓝牙一致（来自 `Bluetooth name` 设置）。

## 2. 固件 UUID（Nordic UART Service）

- Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX Characteristic（小程序写入）: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- TX Characteristic（小程序订阅通知）: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

## 3. 小程序最小流程（必须按顺序）

1. `wx.openBluetoothAdapter`
2. `wx.startBluetoothDevicesDiscovery`
3. 找到目标设备后 `wx.stopBluetoothDevicesDiscovery`
4. `wx.createBLEConnection`
5. `wx.getBLEDeviceServices`，匹配 Service UUID
6. `wx.getBLEDeviceCharacteristics`，找到 RX/TX UUID
7. `wx.notifyBLECharacteristicValueChange` 打开 TX 通知
8. 发送测试命令（写 RX）
9. 在 `wx.onBLECharacteristicValueChange` 接收回包

## 4. 最小代码骨架（页面/模块均可）

```javascript
const UUID = {
  service: "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
  rx: "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
  tx: "6e400003-b5a3-f393-e0a9-e50e24dcca9e",
};

let deviceId = "";

function ab2str(buf) {
  return String.fromCharCode.apply(null, new Uint8Array(buf));
}

function str2ab(str) {
  const buf = new ArrayBuffer(str.length);
  const view = new Uint8Array(buf);
  for (let i = 0; i < str.length; i++) view[i] = str.charCodeAt(i);
  return buf;
}

export async function bleQuickConnectAndTest(targetName) {
  await wx.openBluetoothAdapter();
  await wx.startBluetoothDevicesDiscovery({ allowDuplicatesKey: false });

  const found = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("scan timeout")), 12000);
    wx.onBluetoothDeviceFound((res) => {
      for (const d of res.devices || []) {
        if (d.name === targetName || d.localName === targetName) {
          clearTimeout(timer);
          resolve(d);
          return;
        }
      }
    });
  });

  deviceId = found.deviceId;
  await wx.stopBluetoothDevicesDiscovery();
  await wx.createBLEConnection({ deviceId });

  const services = await wx.getBLEDeviceServices({ deviceId });
  const svc = services.services.find((s) => s.uuid.toLowerCase() === UUID.service);
  if (!svc) throw new Error("NUS service not found");

  const chars = await wx.getBLEDeviceCharacteristics({
    deviceId,
    serviceId: svc.uuid,
  });
  const rx = chars.characteristics.find((c) => c.uuid.toLowerCase() === UUID.rx);
  const tx = chars.characteristics.find((c) => c.uuid.toLowerCase() === UUID.tx);
  if (!rx || !tx) throw new Error("NUS RX/TX not found");

  await wx.notifyBLECharacteristicValueChange({
    deviceId,
    serviceId: svc.uuid,
    characteristicId: tx.uuid,
    state: true,
  });

  wx.onBLECharacteristicValueChange((res) => {
    if (res.characteristicId.toLowerCase() === tx.uuid.toLowerCase()) {
      console.log("[BLE RX]", ab2str(res.value));
    }
  });

  // 发送 '?' 请求状态（注意结尾换行）
  await wx.writeBLECharacteristicValue({
    deviceId,
    serviceId: svc.uuid,
    characteristicId: rx.uuid,
    value: str2ab("?\n"),
  });
}
```

## 5. 分包规则（重要）

当前固件策略：

- **默认** TX 通知按 `20` 字节分片（最稳）。
- 如果小程序在连接后开始以 `>20` 字节写入（通常表示已协商更大 MTU），固件会自动把 TX 分片提升到相近大小（上限 `244`）。

- 命令短：可直接一次写（例如 `?\n`、`$I\n`）
- 命令长：建议先按 20 字节分片写入，确认稳定后再尝试更大分片，末包带换行 `\n`
- 连续写入建议每包间隔 `10~30ms`，避免手机端队列拥塞

## 6. 联调建议命令

按顺序发送：

1. `?\n`（应收到 `<Idle|...>` 状态行）
2. `$I\n`（应收到版本信息）
3. `$$\n`（应收到配置项，多行分片）

## 7. 常见故障排查

- 扫不到设备：
  - 确认串口有 BLE advertising 日志
  - 手机蓝牙定位权限已授权
  - 关闭其他已连接 BLE 工具
- 能连但无回包：
  - 检查是否成功开启 TX notify
  - 检查写的是 RX UUID，不是 TX UUID
  - 命令是否带 `\n`
- 回包乱码：
  - 当前协议为 ASCII 文本，按 UTF-8/ASCII 处理
  - 注意分片拼接，不要把单包当完整消息

## 8. 验收标准（最小）

- 小程序可连接设备
- `?\n` 能稳定收到状态行
- `$I\n` 至少返回一条版本信息
- 断开重连后仍可继续收发

