# merge_firmware.py — PlatformIO post-build script
# 每次构建后，把 bootloader(0x1000) + partitions(0x8000) + firmware(0x10000)
# 合并为从 0x0 开始、整片 flash 大小的量产镜像（纯 Python 拼接 + 0xFF 填充，
# 不依赖 esptool merge_bin——本仓 espressif32@3.0.0 自带的 esptool 2.8 无此命令）：
#   .pio/build/<env>/firmware_full_0x0.bin   →   esptool write_flash 0x0 firmware_full_0x0.bin
# 不含 SPIFFS 数据区（产品默认 BT 模式不用 WebUI，与 CI 产物一致）。

Import("env")
import os

_FLASH_SIZE_BYTES = {
    "1MB": 0x100000, "2MB": 0x200000, "4MB": 0x400000,
    "8MB": 0x800000, "16MB": 0x1000000,
}


def _merge_full_image(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    platform = env.PioPlatform()
    sdk_bin = os.path.join(platform.get_package_dir("framework-arduinoespressif32"), "tools", "sdk", "bin")
    board = env.BoardConfig()

    flash_mode = board.get("build.flash_mode", "qio")
    freq_m = str(int(str(board.get("build.f_flash", "80000000L")).rstrip("L")) // 1000000)
    flash_size = board.get("upload.flash_size", "4MB")
    size_bytes = _FLASH_SIZE_BYTES.get(flash_size, 0x400000)

    # 各段地址按 ESP32 经典布局；镜像头参数（qio/80m）已在构建期由 elf2image 固化
    segments = [
        (0x1000, os.path.join(sdk_bin, "bootloader_%s_%sm.bin" % (flash_mode, freq_m))),
        (0x8000, os.path.join(build_dir, "partitions.bin")),
        (0x10000, os.path.join(build_dir, "firmware.bin")),
    ]

    image = bytearray(b"\xff" * size_bytes)
    for offset, path in segments:
        with open(path, "rb") as f:
            seg = f.read()
        image[offset:offset + len(seg)] = seg

    out = os.path.join(build_dir, "firmware_full_0x0.bin")
    with open(out, "wb") as f:
        f.write(image)
    print("Full flash image from 0x0 (%d bytes): %s" % (len(image), out))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _merge_full_image)
