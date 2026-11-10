import lunaredu


def show(name, value):
    print(name, value)


def call(name, func):
    try:
        show(name, func())
    except Exception as exc:
        show(name + ".error", repr(exc))


def call_module(name, operation="status"):
    module = getattr(lunaredu, name, None)
    if module is None:
        show(name, "not compiled")
        return
    call(name + "." + operation, getattr(module, operation))


show("capabilities", lunaredu.capabilities())
call("status", lunaredu.status)

call_module("rgb", "state")
for module_name in ("imu", "camera", "tfcard", "filesystem", "servo",
                    "battery", "asrpro", "screen", "vision"):
    call_module(module_name)

if hasattr(lunaredu, "lunarlink"):
    call("lunarlink.status", lunaredu.lunarlink.status)
    call("lunarlink.tools", lunaredu.lunarlink.tools)

if hasattr(lunaredu, "camera") and lunaredu.camera.available():
    call("camera.snapshot_bytes", lambda: len(lunaredu.camera.snapshot()))

if (hasattr(lunaredu, "camera") and hasattr(lunaredu, "vision") and
        lunaredu.camera.available() and lunaredu.vision.available()):
    call("vision.snapshot_analysis", lunaredu.vision.analyze_snapshot)
