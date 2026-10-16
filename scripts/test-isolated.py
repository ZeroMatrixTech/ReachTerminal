#!/usr/bin/env python3
"""Run all mutating integration tests in a fresh private cluster and state directory."""
import json,os,socket,subprocess,sys,tempfile,time,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def executable(name):
 p=root/'.local/deps/usr/lib/postgresql/18/bin'/name
 if p.exists():return str(p)
 import shutil
 q=shutil.which(name)
 if q:return q
 return str(Path(subprocess.check_output(['pg_config','--bindir'],text=True).strip())/name)
state=root/'.local/test-runs';state.mkdir(parents=True,exist_ok=True)
run=Path(tempfile.mkdtemp(prefix='isolated-',dir=state));run.chmod(0o700)
env={k:v for k,v in os.environ.items() if not k.startswith('PG') and k not in ['REACH_DATABASE_URL','REACH_SEC_USER_AGENT','REACH_ALPHA_API_KEY','REACH_ROOT']}
env['LD_LIBRARY_PATH']=str(root/'.local/deps/usr/lib/x86_64-linux-gnu')+(':'+env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
def command(args,**kw):return subprocess.run([str(x) for x in args],env=env,check=True,**kw)
def free_port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
pg_port=free_port();http_port=free_port();url=f'http://127.0.0.1:{http_port}'
with socket.socket() as s:s.bind(('127.0.0.1',5001)) # Refuse an existing mock or real process.
socket_dir=Path(tempfile.mkdtemp(prefix='reach-test-socket-'));socket_dir.chmod(0o700);(run/'app').mkdir()
for name in ['examples']:(run/'app'/name).symlink_to(root/name,target_is_directory=True)
command(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',run/'mock-key.pem','-out',run/'mock-cert.pem','-days','2','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost','-addext','basicConstraints=critical,CA:TRUE'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
command([executable('initdb'),'-D',run/'pgdata','-A','trust','--no-locale','--encoding=UTF8'],stdout=subprocess.DEVNULL)
processes=[];started=False
try:
 command([executable('pg_ctl'),'-D',run/'pgdata','-l',run/'postgres.log','-o',f"-k '{socket_dir}' -p {pg_port} -h ''",'start'],stdout=subprocess.DEVNULL);started=True
 name='reachterminal_test_isolated'
 command([executable('createdb'),'-h',socket_dir,'-p',pg_port,name])
 env.update(REACH_ENV='test',REACH_ROOT=str(run/'app'),REACH_PORT=str(http_port),REACH_DATABASE_URL=f"host='{socket_dir}' port={pg_port} dbname={name}")
 for args,log in [([sys.executable,root/'tests/ibkr_mock.py','--fixture-dir',run],'mock.log'),([root/'build/reachterminal','serve'],'app.log')]:
  output=open(run/log,'w');processes.append(subprocess.Popen([str(x) for x in args],env=env,stdout=output,stderr=subprocess.STDOUT))
 for _ in range(60):
  if any(p.poll() is not None for p in processes):raise RuntimeError('Isolated process failed; inspect test logs')
  try:
   data=json.load(urllib.request.urlopen(url+'/api/health',timeout=1))
   if data.get('environment')=='test':break
  except OSError:pass
  time.sleep(.1)
 else:raise RuntimeError('Isolated server did not start')
 command([sys.executable,root/'tests/ibkr_gateway_test.py','--url',url,'--fixture-dir',run])
 command([sys.executable,root/'tests/integration.py','--url',url,'--root',root,'--report',run/'integration-report.json'])
 command([sys.executable,root/'tests/release_integration.py','--url',url])
 print('ISOLATED_TEST_REPORTS='+str(run))
finally:
 for p in reversed(processes):
  p.terminate()
  try:p.wait(timeout=5)
  except subprocess.TimeoutExpired:p.kill();p.wait()
 if started:command([executable('pg_ctl'),'-D',run/'pgdata','stop','-m','fast'],stdout=subprocess.DEVNULL)
