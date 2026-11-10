import {join, resolve, dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {mkdir, array, add, run, parseArgs} from './support.mjs';
const scriptDir = dirname(fileURLToPath(import.meta.url));
const scriptPath = fileURLToPath(import.meta.url);
const options = parseArgs(process.argv.slice(2), { cxx: 'string' });
let lastExitCode = 0;
function main() {
  var cxx = options['cxx'] ?? 'c++';
  var approot = dirname(scriptDir);
  var repoRoot = approot;
  var testroot = join(approot, '.build/host-tests');
  mkdir(testroot);
  var include = join(approot, 'main/smart_speaker');
  var audioInclude = join(repoRoot, 'lunaros/services/optional/service_audio/include');
  var dspRoot = join(approot, 'managed_components/espressif__esp-dsp');
  var dspIncludes = [
    join(dspRoot, 'modules/common/include'),
    join(dspRoot, 'modules/iir/include'),
    join(dspRoot, 'modules/math/include'),
  ];
  for (const name of array(
    [
      'dsp',
      'i2s_sync',
      'lyrics',
    ].flat(),
  )) {
    var sources = [join(scriptDir, add(name, '_test.cpp'))].flat();
    if (name === 'dsp') {
      sources.push(
        join(repoRoot, 'lunaros/services/optional/service_audio/src/realtime/audio_dsp.cpp'),
        join(dspRoot, 'modules/iir/biquad/dsps_biquad_f32_ansi.c'),
        join(dspRoot, 'modules/iir/biquad/dsps_biquad_gen_f32.c'),
      );
    }
    if (name === 'lyrics') sources.push(join(repoRoot, 'lunaros/apps/optional/app_music/src/lyrics.cpp'));
    var binary = join(testroot, add(name, '_test.exe'));
    lastExitCode = run(
      cxx,
      [
        '-std=c++17',
        '-Wall',
        '-Wextra',
        '-Werror',
        '-Wno-deprecated',
        '-O2',
        '-I' + String(include),
        '-I' + String(join(approot, 'test/host')),
        '-I' + String(audioInclude),
        ...dspIncludes.map((directory) => '-I' + String(directory)),
        '-I' + String(repoRoot) + '/lunaros/apps/optional/app_music/include',
        '-I' + String(repoRoot) + '/lunaros/os/core/include',
        sources,
        '-o',
        binary,
      ].flat(),
    );
    if (lastExitCode !== 0) {
      throw new Error(String(name) + ' test compilation failed');
    }
    lastExitCode = run(binary, [].flat());
    if (lastExitCode !== 0) {
      throw new Error(String(name) + ' test failed');
    }
  }
  for (const name of array(
    [
      'audio_lifecycle',
      'transfer',
      'upload_api',
      'lyrics_api',
      'track_remove',
      'playback_mode',
      'player_retirement',
      'runtime_scheduling',
      'sensor_snapshot',
    ].flat(),
  )) {
    lastExitCode = run('python', [join(scriptDir, add(name, '_test.py'))].flat());
    if (lastExitCode !== 0) {
      throw new Error(String(name) + ' production regression failed');
    }
  }
  lastExitCode = run(process.execPath, [join(scriptDir, 'web_api_test.mjs')]);
  if (lastExitCode !== 0) throw new Error('web API regression failed');
}
try {
  main();
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
