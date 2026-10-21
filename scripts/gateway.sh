#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "${REACH_ENV:-user}" == "user" ]] || { echo "Real Gateway cannot start from a test/demo runtime." >&2; exit 1; }
gateway_dir="${REACH_GATEWAY_DIR:-$repo_dir/.local/clientportal}"
# Explicit runtime selection; never silently prefer a stale private JRE.
if [[ -n "${REACH_JAVA_HOME:-}" ]]; then
 java_executable="$REACH_JAVA_HOME/bin/java"
else
 java_executable="$(command -v java || true)"
fi
JAVA_HOME="$(python3 "$repo_dir/scripts/runtime_guard.py" --java "$java_executable" --java-home)"
export JAVA_HOME
export PATH="$JAVA_HOME/bin:$PATH"
[[ -f "$gateway_dir/root/conf.yaml" ]] || { echo 'Download and extract the official Client Portal Gateway; see docs/ibkr.md.' >&2; exit 1; }
python3 "$repo_dir/scripts/device_guard.py" "$gateway_dir"
cd "$gateway_dir"
exec bash bin/run.sh root/conf.yaml
