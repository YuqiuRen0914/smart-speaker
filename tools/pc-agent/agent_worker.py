"""One bounded telemetry worker shared by CLI and desktop adapters."""
from __future__ import annotations

import json
import threading
from dataclasses import dataclass
from urllib.error import HTTPError

from telemetry_agent import disconnect, sample, send


@dataclass(frozen=True)
class AgentStatus:
    state: str = "disconnected"
    detail: str = "Not connected"


class AgentWorker:
    def __init__(self, device: str, computer_id: str, interval: float = 5.0):
        self.device = device
        self.computer_id = computer_id
        self.interval = max(1.0, interval)
        self._stop = threading.Event()
        self._thread = None
        self._lock = threading.Lock()
        self._status = AgentStatus()

    def status(self) -> AgentStatus:
        with self._lock:
            return self._status

    def _set(self, state: str, detail: str):
        with self._lock:
            self._status = AgentStatus(state, detail)

    def connect(self) -> bool:
        if self._thread and self._thread.is_alive():
            return False
        self._stop.clear()
        self._set("connecting", "Connecting to speaker")
        self._thread = threading.Thread(target=self._run, name="speaker-telemetry", daemon=False)
        self._thread.start()
        return True

    def stop(self, timeout: float = 8.0) -> bool:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout)
            if self._thread.is_alive():
                self._set("stopping", "Waiting for current network request")
                return False
        return True

    def _run(self):
        try:
            while not self._stop.is_set():
                try:
                    values = sample()
                    if self._stop.is_set():
                        break
                    send(self.device, self.computer_id, values)
                    cpu = (f"CPU {values['cpu_percent']:.1f}%" if values['cpu_status'] == 'fresh'
                           else "CPU " + values['cpu_status'])
                    self._set("connected", cpu)
                except HTTPError as error:
                    # An expired pairing or bad protocol requires configuration,
                    # not an endless retry loop. Transient server errors retry at
                    # the normal bounded sampling interval.
                    self._set("error", f"Speaker rejected request (HTTP {error.code})")
                    if 400 <= error.code < 500:
                        break
                except (OSError, ValueError, RuntimeError) as error:
                    self._set("error", str(error)[:160])
                if self._stop.wait(self.interval):
                    break
        finally:
            if self._stop.is_set():
                try:
                    disconnect(self.device, self.computer_id)
                    self._set("disconnected", "Disconnected")
                except (OSError, ValueError, RuntimeError) as error:
                    self._set("error", "Disconnect failed; sample will expire: " + str(error)[:100])
