#!/usr/bin/env python3
"""Run only against a fresh disposable ReachTerminal database. No remote services."""
import argparse
import datetime as dt
from decimal import Decimal as D, ROUND_HALF_UP
import json
from pathlib import Path
import random
import urllib.request
import urllib.error

parser = argparse.ArgumentParser()
parser.add_argument('--url', default='http://127.0.0.1:8787')
parser.add_argument('--root', default=str(Path(__file__).resolve().parents[1]))
parser.add_argument('--report')
args = parser.parse_args()
root = Path(args.root)
checks = []
token = ''

def request(path, data=None, headers=None, expected=200, raw=False, authenticated=True):
    h = {'Content-Type': 'application/json'}
    if authenticated:
        h['X-Reach-Token'] = token
    h.update(headers or {})
    body = data if raw else json.dumps(data).encode() if data is not None else None
    req = urllib.request.Request(args.url+path, data=body, headers=h)
    try:
        res = urllib.request.urlopen(req, timeout=60)
    except urllib.error.HTTPError as e:
        res = e
    text = res.read().decode()
    assert res.status == expected, (path, expected, res.status, text[:300])
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text

def check(ok, label):
    assert ok, label
    checks.append(label)

def near(a, b, label):
    check(abs(D(str(a))-D(str(b))) < D('0.00001'), label)

token = request('/api/session')['token']
check(request('/api/health')['trading_enabled'] is False, 'no trading enabled')
counts = request('/api/data')['counts']
check(not any(counts.values()), 'fresh disposable database precondition')
request('/api/import', {}, authenticated=False, expected=403)
checks.append('write token required')
request('/api/import', {}, headers={'Origin': 'https://evil.example'}, expected=403)
checks.append('cross-origin write blocked')
request('/api/health', headers={'Host': 'evil.example'}, expected=403)
checks.append('DNS rebinding host blocked')
request('/api/orders', {}, expected=404)
checks.append('no broker order route')
demo = json.loads((root/'examples/demo.json').read_text())
result = request('/api/demo', {})
check(result['inserted'] == sum(map(len,demo.values())), 'demo complete atomic import')
request('/api/demo', {}, expected=400)
checks.append('demo cannot mix into nonempty database')
repeat = request('/api/import', demo)
check(repeat['inserted'] == 0 and repeat['duplicates'] == result['inserted'], 'whole dataset idempotent')
p = request('/api/portfolio?as_of=2026-10-09')
check(p['complete'] and len(p['positions']) == 3, 'global multicurrency valuation')
near(p['net_external_flows'], 132240, 'dated external flows independent oracle')
near(p['cash_value'], D('90878')+D('12797')*D('1.18')+D('39980')*D('.129'), 'multicurrency cash oracle')
near(p['nav']-p['net_external_flows'], p['economic_pnl'], 'account NAV-flow identity')
check(all(abs(r.get('cash_difference',r.get('quantity_difference',0)))<1e-6 for r in p['reconciliation']), 'all demo broker snapshots reconcile')
check(p['max_drawdown'] < 0, 'loss days retained in index')
stats = request('/api/data')['counts']
bad = dict(demo['trades'][0], price='999')
request('/api/import', {'trades':[bad], 'notes':[{'id':'must-not-persist','instrument':'demo:atlas','title':'invalid batch','body':'should rollback','date':'2026-10-09'}]}, expected=400)
check(request('/api/data')['counts']==stats, 'immutable collision leaves every table unchanged')
oversell = dict(demo['trades'][0], id='oversell',quantity='-999999',date='2026-10-10')
request('/api/import', {'trades':[oversell]}, expected=400)
check(request('/api/data')['counts']==stats, 'oversell transaction rollback')
request('/api/import', {'quotes':[dict(demo['quotes'][0],id='conflict-quote',price='1')]},expected=400)
checks.append('conflicting daily quotes blocked')
request('/api/import', {'fx':[dict(demo['fx'][0],id='conflict-fx',rate='9')]},expected=400)
checks.append('conflicting FX blocked')
check(len(request('/api/news?as_of=2026-10-09'))==3,'news source lifetime safe')
r = request('/api/research?instrument=demo%3Aatlas&as_of=2026-10-09')
check(r['metrics']['pe'] is not None and len(r['selected'])==10,'annual fundamentals and ratios')
check(all(f['filed']<='2026-10-09' for f in r['history']),'research as-of disclosure filter')
check(not request('/api/research?instrument=demo%3Aatlas&as_of=2025-01-01')['selected'],'no future facts')
check(len(request('/api/peers?as_of=2026-10-09'))==3,'peer metrics available')
stress = request('/api/stress',{'as_of':'2026-10-09','equity_shock':-.1,'fx_shock':-.05})
expected = sum(D(str(x['market_value_base']))*(D('.9')*(D('1') if x['currency']=='USD' else D('.95'))-1) for x in p['positions'])
expected += sum(D(str(x['base_value']))*D('-.05') for x in p['cash'] if x['currency']!='USD')
near(stress['pnl'],expected,'joint equity/FX stress independent oracle')
request('/api/portfolio?as_of=2026-02-30',expected=400)
checks.append('invalid analysis date rejected')
a={'fcff':100,'growth':0,'wacc':.1,'terminal_growth':0,'net_debt':0,'shares':10,'years':5,'currency':'USD','sector':'Industrials'}
near(request('/api/dcf',a)['per_share'],100,'DCF perpetuity closed form')
request('/api/dcf',dict(a,terminal_growth=.1),expected=400)
checks.append('DCF terminal condition blocked')
request('/api/valuations',{'instrument':'demo:atlas','title':'integration version','date':'2026-10-09','assumptions':a})
request('/api/notes',{'instrument':'demo:atlas','title':'integration note','date':'2026-10-09','body':'<script>must remain plain text</script>'})
r=request('/api/research?instrument=demo%3Aatlas&as_of=2026-10-09')
near(r['valuations'][-1]['result']['per_share'],100,'persisted valuation result')
check(len(r['notes'])==2,'research notes persisted')

# Independent Decimal FIFO oracle, deterministic sequence of mixed buys/sells and fees.
rng=random.Random(73021)
batch={'instruments':[{'id':'test:oracle','symbol':'ORACLE','name':'Synthetic FIFO oracle','exchange':'TEST','country':'US','currency':'USD','sector':'Industrials','asset_class':'STK'}], 'trades':[], 'quotes':[], 'cashflows':[]}
lots=[]; realized=D(0); cash=D(100000);qty=0
quant=D('.000001')
for n in range(35):
    buying=qty==0 or rng.random()<.6
    q=rng.randint(1,10) if buying else -rng.randint(1,min(qty,10))
    price=D(rng.randint(500,2500))/100;fee=D(rng.randint(0,100))/100
    date=(dt.date(2024,1,1)+dt.timedelta(days=n)).isoformat()
    gross=D(q)*price;cash-=gross+fee
    if q>0:lots.append([q,gross+fee])
    else:
        sell=-q; basis=D(0)
        while sell:
            take=min(sell,lots[0][0]);cost=lots[0][1] if take==lots[0][0] else (lots[0][1]*take/lots[0][0]).quantize(quant,rounding=ROUND_HALF_UP)
            basis+=cost;lots[0][0]-=take;lots[0][1]-=cost;sell-=take
            if lots[0][0]==0:lots.pop(0)
        realized+=-gross-fee-basis
    qty+=q
    batch['trades'].append({'id':f'oracle-{n:03}','account':'TEST','instrument':'test:oracle','strategy':'oracle','date':date,'quantity':str(q),'price':str(price),'fee':str(fee),'currency':'USD','source':'Synthetic demo','source_url':'demo:oracle'})
batch['cashflows'].append({'id':'oracle-deposit','account':'TEST','strategy':'oracle','date':'2024-01-01','amount':'100000','currency':'USD','kind':'deposit','source':'Synthetic demo','source_url':'demo:oracle'})
batch['quotes'].append({'id':'oracle-mark','instrument':'test:oracle','date':'2024-03-01','price':'18','currency':'USD','status':'demo','source':'Synthetic demo','source_url':'demo:oracle'})
request('/api/import',batch)
op=request('/api/portfolio?as_of=2024-03-01')
row=next(x for x in op['positions'] if x['instrument']=='test:oracle')
near(row['quantity'],qty,'random oracle quantity')
near(row['cost_local'],sum(x[1] for x in lots),'random oracle remaining basis')
near(row['realized_local'],realized,'random oracle realized fees/FIFO')
near(op['cash_value'],cash,'random oracle cash')
near(row['unrealized_base'],D(qty)*18-sum(x[1] for x in lots),'random oracle unrealized')
sec_instrument=dict(batch['instruments'][0],id='test:sec',symbol='SEC',sec_cik='123')
request('/api/import',{'instruments':[sec_instrument]})
sec_raw=(root/'examples/sec-synthetic.json').read_bytes()
request('/api/import/sec?instrument=test%3Asec',sec_raw,raw=True)
before=request('/api/research?instrument=test%3Asec&as_of=2023-03-01')
after=request('/api/research?instrument=test%3Asec&as_of=2023-07-01')
near(before['selected'][0]['value'],100,'SEC API original as-of version')
near(after['selected'][0]['value'],95,'SEC API restatement as-of version')
repeat_sec=request('/api/import/sec?instrument=test%3Asec',sec_raw,raw=True)
check(repeat_sec['inserted']==0,'SEC raw-source idempotency')
request('/api/import/sec?instrument=demo%3Aatlas',sec_raw,raw=True,expected=400)
checks.append('wrong SEC entity binding rejected')
flex_raw=(root/'examples/flex-sample.xml').read_bytes()
request('/api/import/flex',flex_raw,raw=True)
repeat_flex=request('/api/import/flex',flex_raw,raw=True)
check(repeat_flex['inserted']==0,'Flex XML API idempotency')
request('/api/import/flex',b'<!DOCTYPE x [<!ENTITY t SYSTEM "file:///etc/passwd">]><x/>',raw=True,expected=400)
checks.append('XML external entity request blocked')
export=request('/api/export')
check(len(export['trades'])==len(demo['trades'])+36,'export roundtrip record count')
report={'ok':True,'checks':len(checks),'passed':checks,'scope':'Disposable synthetic DB; no real IBKR account or external data fetched'}
if args.report:Path(args.report).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
