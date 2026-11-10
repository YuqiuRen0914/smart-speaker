import {mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {parseArgs as parseNodeArgs} from 'node:util';
export const mkdir = (directory) => mkdirSync(directory, {recursive: true});
export const array = (value) => Array.isArray(value) ? value : [value];
export const add = (left, right) => left + right;
export function parseArgs(args, definitions) {
  const options = Object.fromEntries(Object.entries(definitions).map(([key, type]) =>
    [key === 'deviceip' ? 'device-ip' : key, {type}]));
  const {values} = parseNodeArgs({args, options});
  if (values['device-ip']) values.deviceip = values['device-ip'];
  return values;
}
export function run(command, args) {
  if (command === 'python') command = process.env.PYTHON || 'python3';
  if (['c++', 'g++'].includes(command)) command = process.env.CXX || command;
  const result = spawnSync(command, args, {stdio: 'inherit'});
  if (result.error) throw result.error;
  return result.status ?? 1;
}
