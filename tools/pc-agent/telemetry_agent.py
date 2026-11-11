#!/usr/bin/env python3
"""Small LAN computer telemetry client for the Smart Speaker.

The agent deliberately reports unavailable metrics as a status string. It
never turns a missing permission or unsupported sensor into a numeric zero.
The same script runs on macOS and Windows; packaging it as a menu-bar/tray
launcher is a deployment concern, while this protocol and collector remain
testable without a GUI dependency.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
import time
from dataclasses import dataclass
from urllib.error import URLError
from urllib.request import Request, urlopen

try:
    import psutil  # type: ignore
except ImportError:
    psutil = None

PROTOCOL_VERSION = 1


@dataclass
class Metric:
    status: str = "unavailable"
    value: float = 0.0


def cpu_metric() -> Metric:
    # A load average is a runnable-process ratio, not CPU utilisation.
    if psutil is not None:
        try:
            value = float(psutil.cpu_percent(interval=0.25))
            return Metric("fresh", max(0.0, min(100.0, value)))
        except PermissionError:
            return Metric("no_permission")
        except (OSError, ValueError):
            return Metric("unavailable")
    if platform.system() == "Windows":
        try:
            result = subprocess.run(
                ["powershell", "-NoProfile", "-Command",
                 "(Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average).Average"],
                check=True, capture_output=True, text=True, timeout=2)
            return Metric("fresh", float(result.stdout.strip()))
        except (OSError, ValueError, subprocess.SubprocessError):
            pass
    return Metric("unavailable")


def sample() -> dict:
    cpu = cpu_metric()
    # GPU, temperature and fan sources require optional vendor/permission
    # libraries. Keep them explicit until a supported collector is installed.
    return {
        "protocol_version": PROTOCOL_VERSION,
        "cpu_status": cpu.status,
        "cpu_percent": cpu.value,
        "gpu_status": "unavailable",
        "temperature_status": "unavailable",
        "fan_status": "unavailable",
        "platform": platform.system().lower(),
    }


def send(device: str, computer_id: str, values: dict, timeout: float = 3.0) -> dict:
    url = device.rstrip("/") + "/api/v1/modules/smart_speaker/product.computer"
    payload = {"action": "sample", "computer_id": computer_id, **values}
    request = Request(url, data=json.dumps(payload).encode(),
                      headers={"Content-Type": "application/json"}, method="POST")
    with urlopen(request, timeout=timeout) as response:
        body = json.loads(response.read().decode("utf-8"))
        if response.status >= 400 or body.get("ok") is False:
            raise RuntimeError(body.get("error", {}).get("message", "device_rejected"))
        return body


def disconnect(device: str, computer_id: str, timeout: float = 3.0) -> dict:
    url = device.rstrip("/") + "/api/v1/modules/smart_speaker/product.computer"
    payload = {"action": "disconnect", "computer_id": computer_id,
               "protocol_version": PROTOCOL_VERSION}
    request = Request(url, data=json.dumps(payload).encode(),
                      headers={"Content-Type": "application/json"}, method="POST")
    with urlopen(request, timeout=timeout) as response:
        body = json.loads(response.read().decode("utf-8"))
        if response.status >= 400 or body.get("ok") is False:
            raise RuntimeError(body.get("error", {}).get("message", "device_rejected"))
        return body


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", required=True, help="Speaker base URL, e.g. http://192.168.4.1")
    parser.add_argument("--computer-id", default=platform.node(), help="Stable local pairing identifier")
    parser.add_argument("--interval", type=float, default=5.0)
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()
    if not args.computer_id:
        parser.error("--computer-id must not be empty")
    try:
        while True:
            try:
                print(json.dumps(send(args.device, args.computer_id, sample()), ensure_ascii=False), flush=True)
            except (OSError, URLError, RuntimeError, ValueError) as error:
                print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=False), flush=True)
            if args.once:
                return 0
            time.sleep(max(1.0, args.interval))
    except KeyboardInterrupt:
        try:
            print(json.dumps(disconnect(args.device, args.computer_id), ensure_ascii=False), flush=True)
        except (OSError, URLError, RuntimeError, ValueError) as error:
            print(json.dumps({"ok": False, "error": "disconnect: " + str(error)}, ensure_ascii=False), flush=True)
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
