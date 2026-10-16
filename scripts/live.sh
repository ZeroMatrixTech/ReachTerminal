#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Local settings stay ignored; never commit contacts or API credentials.
if [[ -f "$repo_dir/.local/live.env" ]]; then
  set -a
  source "$repo_dir/.local/live.env"
  set +a
fi
if [[ -n "${REACH_DATABASE_URL:-}" ]]; then
  echo "Unset REACH_DATABASE_URL before live.sh to keep the real-data workspace isolated." >&2
  exit 1
fi
export REACH_ENV=user
export REACH_DB_NAME=reachterminal_live
export REACH_PORT="${REACH_PORT:-8788}"
exec "$repo_dir/scripts/dev.sh"
