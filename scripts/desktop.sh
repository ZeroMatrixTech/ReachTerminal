#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export LD_LIBRARY_PATH="$repo_dir/.local/deps/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export REACH_PORT="${REACH_PORT:-8788}"
export REACH_DESKTOP_PORT="$REACH_PORT"
export REACH_ENV=user
if [[ ! -x "$repo_dir/build/reachterminal-desktop" ]]; then
  echo "请先编译 reachterminal-desktop（需要 GTK 3 开发库）。" >&2
  exit 1
fi
health() {
  python3 - "$REACH_PORT" <<'PYHEALTH'
import json,sys,urllib.request
try:
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open('http://127.0.0.1:'+str(int(sys.argv[1]))+'/api/health',timeout=2) as f:
        data=json.load(f)
    sys.exit(0 if data.get('environment')=='user' else 2)
except Exception: sys.exit(1)
PYHEALTH
}
backend_pid=""
cleanup() { if [[ -n "$backend_pid" ]]; then kill "$backend_pid" 2>/dev/null || true; wait "$backend_pid" 2>/dev/null || true; fi; }
trap cleanup EXIT INT TERM
if health; then :; else
  status=$?
  if [[ "$status" == 2 ]]; then echo "该端口属于测试环境，拒绝连接。" >&2; exit 1; fi
  mkdir -p "$repo_dir/.local"
  (umask 077; exec "$repo_dir/scripts/live.sh") >"$repo_dir/.local/desktop-backend.log" 2>&1 &
  backend_pid=$!
  ready=false
  for ((i=0;i<120;i++)); do
    if health; then ready=true; break; fi
    if ! kill -0 "$backend_pid" 2>/dev/null; then break; fi
    sleep 1
  done
  if [[ "$ready" != true ]]; then echo "本地服务未就绪，请查看 .local/desktop-backend.log。" >&2; exit 1; fi
fi
"$repo_dir/build/reachterminal-desktop"
