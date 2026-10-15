#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
deps="$repo_dir/.local/deps"
cmake_args=()
if [[ -d "$deps/usr" ]]; then
  export PATH="$deps/usr/bin:$PATH"
  export LD_LIBRARY_PATH="$deps/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export PKG_CONFIG_SYSROOT_DIR="$deps"
  export PKG_CONFIG_PATH="$deps/usr/lib/x86_64-linux-gnu/pkgconfig:$deps/usr/share/pkgconfig"
  cmake_args+=("-DCMAKE_PREFIX_PATH=$deps/usr"
    "-DCURL_LIBRARY=/usr/lib/x86_64-linux-gnu/libcurl.so.4"
    "-DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so.3"
    "-DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so.3"
    "-DLIBXML2_LIBRARY=/usr/lib/x86_64-linux-gnu/libxml2.so.16"
    "-DLIBXML2_INCLUDE_DIR=$deps/usr/include/libxml2")
fi
if [[ -z "${REACH_DATABASE_URL:-}" ]]; then
  pg_bin="${REACH_PG_BIN:-}"
  if [[ -z "$pg_bin" ]]; then
    if [[ -d "$deps/usr/lib/postgresql/18/bin" ]]; then pg_bin="$deps/usr/lib/postgresql/18/bin"
    elif command -v pg_config >/dev/null; then pg_bin="$(pg_config --bindir)"
    else echo 'Install PostgreSQL or set REACH_PG_BIN.' >&2; exit 1; fi
  fi
  mkdir -p .local/socket
  chmod 700 .local .local/socket
  if [[ ! -f .local/pgdata/PG_VERSION ]]; then "$pg_bin/initdb" -D .local/pgdata -A trust --no-locale --encoding=UTF8; fi
  pg_port="${REACH_PG_PORT:-55433}"
  db_name="${REACH_DB_NAME:-reachterminal}"
  [[ "$db_name" =~ ^[a-z][a-z0-9_]{0,62}$ ]] || { echo "Invalid REACH_DB_NAME" >&2; exit 1; }
  if ! "$pg_bin/pg_ctl" -D .local/pgdata status >/dev/null 2>&1; then
    "$pg_bin/pg_ctl" -D .local/pgdata -l .local/postgres.log -o "-k '$repo_dir/.local/socket' -p $pg_port -h ''" start
  fi
  export REACH_DATABASE_URL="host='$repo_dir/.local/socket' port=$pg_port dbname=$db_name"
  if ! "$pg_bin/psql" "host='$repo_dir/.local/socket' port=$pg_port dbname=postgres" -tAc "SELECT 1 FROM pg_database WHERE datname='$db_name'" | grep -qx 1; then
    "$pg_bin/createdb" -h "$repo_dir/.local/socket" -p "$pg_port" "$db_name"
  fi
fi
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "${cmake_args[@]}"
cmake --build build -j "${REACH_BUILD_JOBS:-2}"
ctest --test-dir build --output-on-failure
export REACH_ROOT="$repo_dir"
exec ./build/reachterminal serve
