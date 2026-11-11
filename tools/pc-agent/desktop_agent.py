#!/usr/bin/env python3
"""macOS menu bar / Windows system tray entry point for the LAN agent.

UI calls never collect metrics or wait on HTTP. Configuration is local JSON;
no cloud account, startup registration, administrator rights or remote control.
"""
from __future__ import annotations

import argparse
import platform
import sys
import webbrowser
from urllib.parse import urlsplit

from agent_worker import AgentWorker


def run_macos(worker: AgentWorker):
    import rumps

    class SpeakerApp(rumps.App):
        def __init__(self):
            super().__init__("Speaker", title="Speaker", quit_button=None)
            self.status_item = rumps.MenuItem("Not connected")
            self.status_item.set_callback(None)
            self.menu = [self.status_item, rumps.MenuItem("Connect", callback=self.connect),
                         rumps.MenuItem("Disconnect", callback=self.disconnect),
                         rumps.MenuItem("Open speaker", callback=self.open_speaker),
                         rumps.MenuItem("Quit", callback=self.quit)]
            self.timer = rumps.Timer(self.refresh, 0.5)
            self.timer.start()

        def connect(self, _):
            worker.connect()

        def disconnect(self, _):
            # Signal only; the UI must stay responsive during request timeout.
            worker._stop.set()

        def open_speaker(self, _):
            webbrowser.open(worker.device)

        def refresh(self, _):
            status = worker.status()
            self.title = "Speaker: " + status.state
            self.status_item.title = status.detail

        def quit(self, _):
            self.timer.stop()
            if worker.stop():
                rumps.quit_application()
            else:
                self.timer.start()
                self.status_item.title = "Still stopping; quit again shortly"

    SpeakerApp().run()


def run_windows(worker: AgentWorker):
    import pystray
    from PIL import Image, ImageDraw
    import threading

    image = Image.new("RGB", (64, 64), "#111827")
    draw = ImageDraw.Draw(image)
    draw.rectangle((16, 12, 48, 52), outline="#F4C95D", width=4)
    draw.ellipse((24, 30, 40, 46), outline="#F4C95D", width=3)
    closed = threading.Event()

    def quit_app(icon, _):
        if worker.stop():
            closed.set()
            icon.stop()

    icon = pystray.Icon("smart-speaker", image, "Speaker: disconnected", menu=pystray.Menu(
        pystray.MenuItem(lambda _: worker.status().detail, None, enabled=False),
        pystray.MenuItem("Connect", lambda *_: worker.connect()),
        pystray.MenuItem("Disconnect", lambda *_: worker._stop.set()),
        pystray.MenuItem("Open speaker", lambda *_: webbrowser.open(worker.device)),
        pystray.MenuItem("Quit", quit_app)))

    def update():
        while not closed.wait(0.5):
            icon.title = "Speaker: " + worker.status().state
            icon.update_menu()

    updater = threading.Thread(target=update, name="speaker-tray-status", daemon=True)
    updater.start()
    try:
        icon.run()
    finally:
        closed.set()
        worker.stop()
        updater.join(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True, help="Speaker LAN URL")
    parser.add_argument("--computer-id", required=True, help="ID paired in the speaker web page")
    parser.add_argument("--interval", type=float, default=5.0)
    args = parser.parse_args()
    url = urlsplit(args.device)
    if url.scheme not in {"http", "https"} or not url.hostname or url.username or url.password:
        parser.error("--device must be an HTTP(S) URL without credentials")
    if not args.computer_id.strip() or len(args.computer_id.encode()) > 128:
        parser.error("--computer-id must be 1–128 UTF-8 bytes")
    worker = AgentWorker(args.device.rstrip("/"), args.computer_id, args.interval)
    try:
        if platform.system() == "Darwin":
            run_macos(worker)
        elif platform.system() == "Windows":
            run_windows(worker)
        else:
            parser.error("Desktop UI supports macOS and Windows; use telemetry_agent.py for CLI")
    except ImportError as error:
        print(f"Missing desktop dependency: {error}. Install requirements-desktop.txt in a venv.", file=sys.stderr)
        return 2
    finally:
        worker.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
