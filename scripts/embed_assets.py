#!/usr/bin/env python3
"""Generate C++ read-only resources from an explicit public asset allowlist."""
import argparse,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',required=True);p.add_argument('--output',required=True);a=p.parse_args();root=Path(a.root);out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
assets=[('/', 'index.html','text/html; charset=utf-8'),('/index.html','index.html','text/html; charset=utf-8'),('/app.js','app.js','text/javascript; charset=utf-8'),('/release.js','release.js','text/javascript; charset=utf-8'),('/gateway-session.js','gateway-session.js','text/javascript; charset=utf-8'),('/style.css','style.css','text/css; charset=utf-8')]
def literal(b):
 text=b.decode('utf-8');tag='r'+hashlib.sha256(b).hexdigest()[:12];assert ')'+tag+'"' not in text and '\0' not in text;return f'R"{tag}({text}){tag}"'
def write(path,text):
 if not path.exists() or path.read_text()!=text:path.write_text(text)
header='''#pragma once
#include <string_view>
namespace reach::assets {
struct Asset {std::string_view path, mime, body;};
const Asset* find(std::string_view path);
std::string_view schema();
}
'''
cpp='#include "embedded_assets.hpp"\nnamespace reach::assets {\nstatic const Asset resources[]={\n'
for url,file,mime in assets:cpp+='{"'+url+'","'+mime+'",'+literal((root/'frontend'/file).read_bytes())+'},\n'
cpp+='};\nconst Asset* find(std::string_view path){for(auto &a:resources)if(a.path==path)return &a;return nullptr;}\nstd::string_view schema(){return '+literal((root/'db/001_initial.sql').read_bytes())+';}\n}\n'
write(out/'embedded_assets.hpp',header);write(out/'embedded_assets.cpp',cpp)
