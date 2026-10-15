"""Read-only verification of an already populated real research workspace."""
import argparse,json,urllib.request
p=argparse.ArgumentParser();p.add_argument('--url',default='http://127.0.0.1:8788');p.add_argument('--instrument',default='us:ibm');a=p.parse_args()
def get(path):return json.load(urllib.request.urlopen(a.url+path,timeout=60))
from urllib.parse import urlencode
r=get('/api/research?'+urlencode({'instrument':a.instrument}))
d=get('/api/data')
assert not d['demo_present'],'Use real data workspace'
assert r['quote'] and r['quote']['status']=='eod'
assert r['quote']['source']=='Alpha Vantage'
assert len(r['bars'])>0 and len(r['history'])>0
assert r['quote']['price']==r['bars'][-1]['close']
assert r['quote']['date']==r['bars'][-1]['date']
assert all(f['source']=='SEC CompanyFacts' for f in r['history'])
print(json.dumps({'ok':True,'bars':len(r['bars']),'facts':len(r['history']),'quote_date':r['quote']['date'],'checks':7},indent=2))
