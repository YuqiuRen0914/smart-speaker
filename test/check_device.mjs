import { isIPv4 } from 'node:net';
import {parseArgs} from './support.mjs';
async function main() {
  const o = parseArgs(process.argv.slice(2), { deviceip: 'string' });
  if (typeof o.deviceip !== 'string' || !isIPv4(o.deviceip))
    throw new Error('Use --device-ip with the device IPv4 address shown on its screen.');
  const base = `http://${o.deviceip}`;
  async function get(route, json = true) {
    const response = await fetch(base + route, { signal: AbortSignal.timeout(10000) });
    if (!response.ok) throw new Error(`Device HTTP ${response.status}: ${route}`);
    if (!json) return response.text();
    const result = await response.json();
    if (result.ok !== true) throw new Error(`Device API failed: ${route}`);
    return result.result;
  }
  const page = await get('', false);
  if (!page.includes('/api/v1/modules/') || !page.includes('firmwareFile'))
    throw new Error('The device is serving an old or incomplete maintenance page.');
  await get('/api/v1/device');
  const capabilities = await get('/api/v1/capabilities');
  const ota = await get('/api/v1/modules/ota/status.get'),
    speaker = await get('/api/v1/modules/smart_speaker/status.get');
  if (ota.project_name !== 'lunaredu-smart-speaker')
    throw new Error('Connected device is not a smart speaker.');
  for (const id of ['ota', 'smart_speaker']) {
    const modules = capabilities.modules.filter((m) => m.id === id);
    if (modules.length !== 1) throw new Error(`Missing module: ${id}`);
    if (
      id === 'ota' &&
      !modules[0].operations.some((o) => o.id === 'firmware.upload' && o.method === 'PUT')
    )
      throw new Error('Standard OTA upload operation is missing.');
  }
  if (!ota.rollback_enabled)
    throw new Error(
      'Rollback is disabled; install the new bootloader before closing the enclosure.',
    );
  if (ota.pending_verify)
    throw new Error(
      'The new firmware is still pending verification. Wait and run this check again.',
    );
  console.log(
    JSON.stringify(
      {
        Address: base,
        Project: ota.project_name,
        Version: ota.version,
        Rollback: ota.rollback_enabled,
        BootConfirmed: ota.boot_confirmed,
        RunningSlot: ota.running_label,
        NextSlot: ota.next_label,
        AudioReady: speaker.audio_ready,
        Result: 'Read-only checks passed. A real upload/reboot test is still required.',
      },
      null,
      2,
    ),
  );
}
main().catch((error) => {
  console.error(error.message);
  process.exitCode = 1;
});
