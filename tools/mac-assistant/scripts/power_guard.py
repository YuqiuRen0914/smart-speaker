"""Prevent idle sleep only on AC power; manual sleep and display sleep remain."""
import os
import subprocess
import threading


class PowerGuard:
    def __init__(self):
        self.stop_event = threading.Event()
        self.assertion = None
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop_event.is_set():
            try:
                status = subprocess.check_output(['/usr/bin/pmset', '-g', 'batt'], text=True, timeout=3)
                on_ac = "Now drawing from 'AC Power'" in status
            except (OSError, subprocess.SubprocessError):
                on_ac = False
            if on_ac and self.assertion is None:
                self.assertion = subprocess.Popen(['/usr/bin/caffeinate', '-i', '-w', str(os.getpid())])
            elif not on_ac and self.assertion is not None:
                self.release()
            self.stop_event.wait(2)
        self.release()

    def release(self):
        if self.assertion:
            self.assertion.terminate()
            self.assertion.wait(timeout=3)
            self.assertion = None

    def close(self):
        self.stop_event.set()
        self.thread.join(6)
