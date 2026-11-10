#!/bin/sh
set -eu
exec "${NODE:-node}" "$(dirname -- "$0")/run_host_tests.mjs" "$@"
