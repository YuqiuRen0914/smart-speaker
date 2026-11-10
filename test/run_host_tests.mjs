import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const result = spawnSync('sh', [fileURLToPath(new URL('./run_host_tests.sh', import.meta.url))], { stdio: 'inherit' });
if (result.error) throw result.error;
process.exit(result.status ?? 1);
