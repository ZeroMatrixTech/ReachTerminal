import http.server,ssl,json,urllib.parse,argparse,time
p=argparse.ArgumentParser();p.add_argument("--fixture-dir",required=True);a=p.parse_args()
from pathlib import Path
fixture=Path(a.fixture_dir)
class Mock(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_POST(self):self.respond()
 def do_GET(self):self.respond()
 def respond(self):
  path=urllib.parse.urlparse(self.path).path
  mode=Path(fixture/'mock-mode').read_text().strip() if Path(fixture/'mock-mode').exists() else 'ok'
  with open(fixture/'mock-requests.jsonl','a') as f:f.write(json.dumps({'method':self.command,'path':self.path})+'\n')
  code=200
  payload=json.loads(self.rfile.read(int(self.headers.get('Content-Length',0))) or b'{}') if self.command=='POST' else {}
  if path.endswith('/ssodh/init'):assert payload=={'publish':True,'compete':False}
  if mode=='unauthorized':code=401;out={'error':'login'}
  elif mode=='warming' and path.endswith('/iserver/accounts'):code=400;out={'error':'init session first PRIVATE-ERROR-IDENTITY'}
  elif mode=='emptyaccounts' and path.endswith('/iserver/accounts'):out={'accounts':[]}
  elif mode=='quota':code=429;out={'error':'quota'}
  elif path.endswith('/logout'):Path(fixture/'mock-mode').write_text('logoutfail' if mode=='logoutfail' else 'loggedout');out={'status':mode!='logoutfail'}
  elif mode=='initializing' and path.endswith('/iserver/accounts'):code=400;out={'error':'init session first'}
  elif mode=='initializing' and path.endswith('/ssodh/init'):out={'passed':False,'authenticated':False,'connected':True,'competing':False}
  elif path.endswith('/ssodh/init'):out={'authenticated':True,'competing':False,'MAC':'PRIVATE-MAC'}
  elif path.endswith('/sso/validate'):out={'RESULT':mode!='loggedout','USER_NAME':'PRIVATE-SSO','USER_ID':123456}
  elif path.endswith('/auth/status'):out={'authenticated':mode not in ['loggedout','readonly'],'connected':True,'competing':False,'MAC':'PRIVATE-MAC'}
  elif path.endswith('/portfolio/accounts'):out=[{'accountId':'TEST_ONLY','currency':'USD','tradingType':'PM','accountAlias':'PRIVATE-ALIAS'}]
  elif path.endswith('/TEST_ONLY/summary'):out={k:{'amount':v,'currency':'USD','isNull':False,'timestamp':int(time.time()*1000)} for k,v in {'netliquidation':100000,'excessliquidity':5000,'maintmarginreq':20000}.items()}
  elif path.endswith('/accounts'):out={'accounts':['PRIVATE-ACCOUNT']}
  elif path.endswith('/secdef/search'):out=[{'conid':'265598','symbol':'AAPL','companyName':'Apple · SYNTHETIC TEST','companyHeader':'SYNTHETIC TEST / NASDAQ','sections':[{'secType':'STK'}],'secret':'PRIVATE-ACCOUNT'}]
  elif path.endswith('/info'):out={'con_id':265598,'symbol':'AAPL','company_name':'Apple · SYNTHETIC TEST','currency':'USD','exchange':'SMART','instrument_type':'STK','industry':'Technology'}
  elif path.endswith('/snapshot'):out=[{'conid':265598,'31':'C193.18','84':'193.06','86':'193.14','6509':'DpB','_updated':1729000000000,'7671':'1.00','73':'PRIVATE-POSITION'}] if mode!='pending' else [{'conid':265598}]
  elif path.endswith('/history'):out={'symbol':'AAPL','barLength':86400,'mdAvailability':'D','data':[{'o':190,'h':195,'l':189,'c':193,'v':1000,'t':1728864000000}]}
  else:code=404;out={'error':'not whitelisted'}
  body=json.dumps(out).encode();self.send_response(code);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
server=http.server.HTTPServer(('127.0.0.1',5001),Mock)
c=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);c.load_cert_chain(str(fixture/'mock-cert.pem'),str(fixture/'mock-key.pem'));server.socket=c.wrap_socket(server.socket,server_side=True)
print('Synthetic HTTPS Gateway on 5001',flush=True);server.serve_forever()
