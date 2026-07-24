# merge_firmware.py - PlatformIO post-build script
# Merge bootloader(0x1000) + partitions(0x8000) + firmware(0x10000)
# into full-flash image from 0x0 (pure Python + 0xFF pad).
# Output: .pio/build/<env>/firmware_full_0x0.bin
# Flash: esptool write_flash 0x0 firmware_full_0x0.bin

Import("env")
import os

_FLASH_SIZE_BYTES = {
    "1MB": 0x100000,
    "2MB": 0x200000,
    "4MB": 0x400000,
    "8MB": 0x800000,
    "16MB": 0x1000000,
}


def _resolve_build_dir(target, source, env):
    nodes = []
    for group in (target, source):
        if not group:
            continue
        for node in group:
            nodes.append(str(node))
    for p in nodes:
        ap = os.path.abspath(p)
        base = os.path.basename(ap).lower()
        if base == "firmware.bin":
            return os.path.dirname(ap)
    # Fallbacks when PIOENV/BUILD_DIR are empty in post-action env
    env_name = env.subst("$PIOENV")
    if env_name:
        cand = os.path.join(".pio", "build", env_name)
        if os.path.isfile(os.path.join(cand, "firmware.bin")):
            return os.path.abspath(cand)
    build_dir = env.subst("$BUILD_DIR")
    if build_dir and os.path.isdir(build_dir) and os.path.isfile(os.path.join(build_dir, "firmware.bin")):
        return os.path.abspath(build_dir)
    rel = os.path.join(".pio", "build", "release")
    if os.path.isfile(os.path.join(rel, "firmware.bin")):
        return os.path.abspath(rel)
    raise RuntimeError("merge_firmware: cannot resolve build dir; nodes=%r" % (nodes,))


def _merge_full_image(target, source, env):
    build_dir = _resolve_build_dir(target, source, env)

    platform = env.PioPlatform()
    sdk_bin = os.path.join(platform.get_package_dir("framework-arduinoespressif32"), "tools", "sdk", "bin")
    board = env.BoardConfig()

    flash_mode = board.get("build.flash_mode", "qio")
    freq_m = str(int(str(board.get("build.f_flash", "80000000L")).rstrip("L")) // 1000000)
    flash_size = board.get("upload.flash_size", "4MB")
    size_bytes = _FLASH_SIZE_BYTES.get(flash_size, 0x400000)

    segments = [
        (0x1000, os.path.join(sdk_bin, "bootloader_%s_%sm.bin" % (flash_mode, freq_m))),
        (0x8000, os.path.join(build_dir, "partitions.bin")),
        (0x10000, os.path.join(build_dir, "firmware.bin")),
    ]

    image = bytearray(b"\xff" * size_bytes)
    for offset, path in segments:
        if not os.path.isfile(path):
            raise RuntimeError("missing flash segment for merge: %s (offset 0x%x)" % (path, offset))
        with open(path, "rb") as f:
            seg = f.read()
        if offset + len(seg) > size_bytes:
            raise RuntimeError("%s at 0x%x exceeds flash size %d" % (path, offset, size_bytes))
        image[offset:offset + len(seg)] = seg

    env_name = env.subst("$PIOENV")
    _FULL_NAMES = {
        "release_clamp160": ("firmware_clamp160.bin", "firmware_clamp160_full_0x0.bin"),
        "release_clamp100": ("firmware_clamp100.bin", "firmware_clamp100_full_0x0.bin"),
    }
    app_name, out_name = _FULL_NAMES.get(env_name, (None, "firmware_full_0x0.bin"))
    if app_name:
        app_src = os.path.join(build_dir, "firmware.bin")
        app_dst = os.path.join(build_dir, app_name)
        if os.path.isfile(app_src):
            import shutil
            shutil.copy2(app_src, app_dst)
            print("App firmware copy: %s" % app_dst)
    out = os.path.join(build_dir, out_name)
    with open(out, "wb") as f:
        f.write(image)
    print("Full flash image from 0x0 (%d bytes): %s" % (len(image), out))


env.AddPostAction("buildprog", _merge_full_image)