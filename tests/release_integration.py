import argparse,json,urllib.request,urllib.error,time
p=argparse.ArgumentParser();p.add_argument('--url',required=True);a=p.parse_args()
def get(path):return json.load(urllib.request.urlopen(a.url+path,timeout=30))
session=get('/api/session');assert session['environment']=='test','Refusing real user workspace';token=session['token'];checks=0
def post(path,v,status=200):
 global checks
 req=urllib.request.Request(a.url+path,data=json.dumps(v).encode(),headers={'X-Reach-Token':token,'Content-Type':'application/json'})
 try:r=urllib.request.urlopen(req,timeout=30)
 except urllib.error.HTTPError as e:r=e
 data=json.load(r);assert r.status==status,(path,r.status,data);checks+=1;return data
book={'currency':'USD','positions':[{'id':'release-stock','type':'STK','underlying':'TEST','spot':100,'quantity':10,'multiplier':1}],'scenario':{'spot':-.1,'volatility':0,'days':0}}
assert post('/api/hedge/analyze',book)['scenario_pnl']==-100
post('/api/hedge/models',{'date':'2026-01-01','title':'SYNTHETIC TEST','assumptions':book})
rows=get('/api/hedge/models?as_of=2026-01-01');assert len(rows)==1 and rows[0]['result']['scenario_pnl']==-100
assert get('/api/hedge/models?as_of=2025-12-31')==[]
post('/api/macro/fetch',{'country':'USA','indicator':'gdp'},400);post('/api/news/fetch',{},400)
o={'id':'test-macro-release','country':'USA','indicator':'gdp','period_end':'2024-12-31','frequency':'annual','value':30000000000000,'unit':'current USD','known_at':None,'retrieved_at_ms':1767225600000,'date':'2026-01-01','source':'Synthetic test','source_url':'manual:test'}
post('/api/import',{'macro_observations':[o]});assert get('/api/macro?as_of=2025-12-31')['observations']==[]
assert get('/api/macro?as_of=2026-01-01')['observations'][0]['value']==30000000000000
o['id']='test-bad-retrieval';o['date']='2025-12-31';post('/api/import',{'macro_observations':[o]},400)
book['positions'][0]['currency']='EUR';post('/api/hedge/analyze',book,400)
assert all(k in get('/api/export') for k in ['hedge_models','macro_observations'])
print(json.dumps({'ok':True,'mutating_checks':checks,'scope':'Synthetic isolated release endpoints, retrieval chronology, persistence and blocked external fetch'}))
