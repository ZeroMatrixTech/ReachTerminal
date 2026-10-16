#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "${REACH_ENV:-user}" == "user" ]] || { echo "Real Gateway cannot start from a test/demo runtime." >&2; exit 1; }
gateway_dir="${REACH_GATEWAY_DIR:-$repo_dir/.local/clientportal}"
if [[ -x "$repo_dir/.local/java-runtime-21/usr/lib/jvm/java-21-openjdk-amd64/bin/java" ]]; then
 export PATH="$repo_dir/.local/java-runtime-21/usr/lib/jvm/java-21-openjdk-amd64/bin:$PATH"
fi
command -v java >/dev/null || { echo 'Install Java (JRE 11 or newer) first.' >&2; exit 1; }
[[ -f "$gateway_dir/root/conf.yaml" ]] || { echo 'Download and extract the official Client Portal Gateway; see docs/ibkr.md.' >&2; exit 1; }
python3 "$repo_dir/scripts/device_guard.py" "$gateway_dir"
cd "$gateway_dir"
exec bash bin/run.sh root/conf.yaml
