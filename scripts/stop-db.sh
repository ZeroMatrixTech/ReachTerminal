#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps="$repo_dir/.local/deps"
if [[ -d "$deps/usr/lib/postgresql/18/bin" ]]; then
  export LD_LIBRARY_PATH="$deps/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  pg_bin="$deps/usr/lib/postgresql/18/bin"
else pg_bin="${REACH_PG_BIN:-$(pg_config --bindir)}"; fi
"$pg_bin/pg_ctl" -D "$repo_dir/.local/pgdata" stop -m fast
