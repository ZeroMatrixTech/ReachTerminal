"""Verify a previously provisioned Gateway belongs to this Linux device."""
import hashlib,json,re,sys
from pathlib import Path
machine=Path('/etc/machine-id').read_text().strip()
if not re.fullmatch('[0-9a-fA-F]{32}',machine):raise SystemExit('Machine identity unavailable; Gateway startup denied.')
device=hashlib.sha256(('ReachTerminal/IBKR/device/v1:'+machine).encode()).hexdigest()
marker=Path(sys.argv[1])/'.reach-device.json'
if not marker.is_file():raise SystemExit('Gateway is not provisioned for this device. Install a fresh local Gateway; do not copy runtime directories.')
if json.loads(marker.read_text()).get('device_id')!=device:raise SystemExit('Gateway belongs to another device. Migration is prohibited; install and log in locally.')
print('Gateway device binding verified.')
