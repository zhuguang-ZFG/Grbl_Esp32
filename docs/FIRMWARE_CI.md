# 固件 GitHub 自动编译与下载

Workflow：`.github/workflows/firmware-build.yml`

## 行为（编译一次 → 上传一次 → 有 tag 再发布一次）

| 触发 | 编译 | Artifacts 上传 | GitHub Release |
|------|------|----------------|----------------|
| Push（`main` / `master` / `Branch_*`） | ✅ | ✅ 一次 | — |
| Pull Request | ✅ | ✅ 一次 | — |
| 手动 **Actions → Firmware Build → Run** | ✅ | ✅ 一次 | — |
| 推送 tag **`v*`**（如 `v1.2.0`） | ✅ | ✅ 一次 | ✅ **同一次构建**挂到 Release |

同一次 job 内先编产品机，再编 `test_drive`，最后只 **upload-artifact 一次**；若是 `v*` tag，再用同一批 `dist/*` 调 **gh-release 一次**（不二次编译）。

## 产物

| 文件 | 说明 |
|------|------|
| `firmware-product.bin` | 默认机 `custom_3axis_hr4988`（纸路 + BT） |
| `firmware-test_drive.bin` | `test_drive.h` 安全机（无驱动 I/O） |
| `partitions.bin` | `min_spiffs.csv` 分区表 |
| `BUILD_INFO.txt` | ref / sha / run id |
| `firmware-product.elf` | 仅 Artifacts（体积大；Release 默认不附） |

## 怎么下载

### A. 每次构建（Artifacts）

1. 打开仓库 **Actions** → **Firmware Build**
2. 点进对应 run
3. 底部 **Artifacts** → `grbl-esp32-firmware-<sha>` → 下载 zip  
4. 保留 **30 天**

### B. 正式发版（Release）

```bash
# 本地打 tag 并推送（示例）
git tag v1.0.0
git push origin v1.0.0
```

1. 等待 Actions 绿
2. 打开仓库 **Releases** → 对应 tag
3. 下载 `firmware-product.bin` 等

`v*rc*` / `*beta*` / `*alpha*` 会标为 **pre-release**。

## 烧录提示

- 仅应用：`esptool.py write_flash 0x10000 firmware-product.bin`  
  （具体 offset 以本仓 `min_spiffs.csv` 的 `app0` 为准，默认 **0x10000**）
- 整片量产（本地构建产物，非 CI artifact）：每次 `pio run`（release）都会由 `merge_firmware.py` 生成  
  `.pio/build/release/firmware_full_0x0.bin`（4MB，从 **0x0** 开始：bootloader@0x1000 + partitions@0x8000 + app@0x10000，0xFF 填充）→  
  `esptool write_flash 0x0 firmware_full_0x0.bin`。不含 SPIFFS 数据区。缺 bootloader/partitions/firmware 段时 post 脚本显式 `RuntimeError`（路径+offset），勿当运行时报错。
- 或本机：`pio run -e release -t upload`
- GitHub **不能**直接写你电脑上的串口；只负责出 bin。

## 与本机一致

- Platform：`espressif32@3.0.0`（`platformio.ini`）
- Env：`release`
- 产品机：`Machine.h` 默认，不设 `MACHINE_FILENAME`
- 安全机：`PLATFORMIO_BUILD_FLAGS=-DMACHINE_FILENAME=test_drive.h`

## Agent 注意

- CI 绿 = **能编译**，≠ 纸路/BT 真机验收（仍要 `docs/ACCEPTANCE_CHECKLIST.md` + HIL）
- Host SIL 仍在 fz：`agent_gate`（见 `docs/AGENT_HANDOFF.md`）
