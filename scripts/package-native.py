#!/usr/bin/env python3
"""Package native executables only; never include device or user runtime state."""
import argparse, hashlib, platform, shutil, tarfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(); p.add_argument('--output', required=True); args=p.parse_args()
out=Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=True)
name='ReachTerminal-0.1.0-native-linux-'+platform.machine(); folder=out/name
folder.mkdir(exist_ok=True)
files=['reachterminal-desktop','reachterminal','LICENSE','start.sh','README.txt']
for binary in files[:2]: shutil.copy2(root/'build'/binary,folder/binary)
shutil.copy2(root/'LICENSE',folder/'LICENSE')
(folder/'start.sh').write_text('''#!/usr/bin/env bash
set -euo pipefail
base="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${REACH_DATABASE_URL:?请设置本机用户 PostgreSQL 连接 REACH_DATABASE_URL}"
export REACH_ENV=user
export REACH_ROOT="${REACH_ROOT:-$base/workspace}"
export REACH_PORT="${REACH_PORT:-8788}"
export REACH_DESKTOP_PORT="$REACH_PORT"
mkdir -p "$REACH_ROOT"
"$base/reachterminal" serve &
backend_pid=$!
cleanup() { kill "$backend_pid" 2>/dev/null || true; wait "$backend_pid" 2>/dev/null || true; }
trap cleanup EXIT INT TERM
sleep 1
if ! kill -0 "$backend_pid" 2>/dev/null; then echo "本地服务启动失败（端口可能占用）" >&2; exit 1; fi
"$base/reachterminal-desktop"
''')
(folder/'start.sh').chmod(0o755)
(folder/'README.txt').write_text((root/'docs/native-desktop.md').read_text())
archive=out/(name+'.tar.gz')
with tarfile.open(archive,'w:gz') as target:
    for file in files: target.add(folder/file,arcname=name+'/'+file,recursive=False)
(out/(archive.name+'.sha256')).write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')
print(archive)
