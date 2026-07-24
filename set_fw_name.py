# set_fw_name.py - PlatformIO pre script: distinct PROGNAME per clamp variant
Import("env")

_NAMES = {
    "release_clamp160": "firmware_clamp160",
    "release_clamp100": "firmware_clamp100",
}

pioenv = env.subst("$PIOENV")
if pioenv in _NAMES:
    name = _NAMES[pioenv]
    env.Replace(PROGNAME=name)
    env["PROGNAME"] = name
    print("PROGNAME=%s (env=%s)" % (name, pioenv))
