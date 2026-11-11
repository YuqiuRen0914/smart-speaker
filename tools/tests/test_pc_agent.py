import importlib.util
import pathlib
import sys
import time
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("telemetry_agent", ROOT / "tools/pc-agent/telemetry_agent.py")
agent = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = agent
spec.loader.exec_module(agent)

worker_spec = importlib.util.spec_from_file_location("agent_worker", ROOT / "tools/pc-agent/agent_worker.py")
worker = importlib.util.module_from_spec(worker_spec)
sys.modules[worker_spec.name] = worker
worker_spec.loader.exec_module(worker)


class PcAgentTests(unittest.TestCase):
    def test_sample_has_explicit_unsupported_metrics(self):
        values = agent.sample()
        self.assertIn(values["cpu_status"], {"fresh", "unavailable"})
        self.assertEqual(values["gpu_status"], "unavailable")
        self.assertEqual(values["temperature_status"], "unavailable")
        self.assertEqual(values["fan_status"], "unavailable")
        self.assertEqual(values["protocol_version"], 1)

    def test_payload_uses_one_pairing_id(self):
        values = agent.sample()
        self.assertEqual(values["platform"], agent.platform.system().lower())
        self.assertNotIn("password", values)

    def test_cpu_sample_is_a_percent_not_load_average(self):
        with mock.patch.object(agent, "psutil") as psutil:
            psutil.cpu_percent.return_value = 37.5
            metric = agent.cpu_metric()
        self.assertEqual(metric.status, "fresh")
        self.assertEqual(metric.value, 37.5)
        psutil.cpu_percent.assert_called_once_with(interval=0.25)

    def test_cpu_permission_failure_is_not_reported_as_zero(self):
        with mock.patch.object(agent, "psutil") as psutil:
            psutil.cpu_percent.side_effect = PermissionError()
            metric = agent.cpu_metric()
        self.assertEqual(metric.status, "no_permission")
        self.assertEqual(metric.value, 0.0)  # ignored unless status is fresh

    def test_disconnect_uses_the_paired_identity_and_protocol(self):
        response = mock.MagicMock()
        opened = response.__enter__.return_value
        opened.status = 200
        opened.read.return_value = b'{"ok":true}'
        with mock.patch.object(agent, "urlopen", return_value=response) as urlopen:
            self.assertEqual(agent.disconnect("http://device/", "paired-host"), {"ok": True})
        request = urlopen.call_args.args[0]
        self.assertEqual(request.full_url, "http://device/api/v1/modules/smart_speaker/product.computer")
        self.assertEqual(__import__("json").loads(request.data), {
            "action": "disconnect", "computer_id": "paired-host", "protocol_version": 1})

    def test_worker_keeps_network_work_off_the_caller_and_disconnects(self):
        sample = {"cpu_status": "fresh", "cpu_percent": 14.0}
        with mock.patch.object(worker, "sample", return_value=sample), \
             mock.patch.object(worker, "send", return_value={"ok": True}) as send, \
             mock.patch.object(worker, "disconnect", return_value={"ok": True}) as disconnect:
            client = worker.AgentWorker("http://speaker", "host", interval=1)
            self.assertTrue(client.connect())
            deadline = time.monotonic() + 1
            while client.status().state != "connected" and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertEqual(client.status().detail, "CPU 14.0%")
            self.assertTrue(client.stop())
            send.assert_called_once()
            disconnect.assert_called_once_with("http://speaker", "host")


if __name__ == "__main__":
    unittest.main()
