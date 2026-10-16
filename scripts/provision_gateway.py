"""Provision a fresh official Gateway archive on this device; never rebind old runtimes."""
import argparse,hashlib,json,re,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--archive',required=True);p.add_argument('--destination',required=True);a=p.parse_args()
identity=Path('/etc/machine-id').read_text().strip()
if not re.fullmatch('[0-9a-fA-F]{32}',identity):raise SystemExit('Machine identity unavailable.')
device=hashlib.sha256(('ReachTerminal/IBKR/device/v1:'+identity).encode()).hexdigest()
dest=Path(a.destination).resolve()
if dest.exists():raise SystemExit('Destination must not exist. Existing or migrated Gateways cannot be rebound.')
with zipfile.ZipFile(a.archive) as z:
 if not {'root/conf.yaml','bin/run.sh'}.issubset(z.namelist()):raise SystemExit('Expected official Client Portal Gateway archive.')
 if any(Path(x).name=='.reach-device.json' for x in z.namelist()):raise SystemExit('Archive contains device state; migration prohibited.')
 for info in z.infolist():
  if not (dest/info.filename).resolve().is_relative_to(dest):raise SystemExit('Unsafe archive path.')
  if (info.external_attr>>16)&0o170000==0o120000:raise SystemExit('Archive symlinks are not accepted.')
 dest.mkdir(parents=True,mode=0o700);z.extractall(dest)
marker=dest/'.reach-device.json';marker.write_text(json.dumps({'device_id':device}));marker.chmod(0o600)
print('Fresh Gateway provisioned on this device. Configure a unique local HTTPS certificate, then sign in locally.')
