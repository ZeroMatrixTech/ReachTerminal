#!/usr/bin/env python3
"""Create a source-only release archive. Never include runtime data or credentials."""
import argparse,hashlib,tarfile,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
version=re.search(r'project\(ReachTerminal VERSION ([0-9.]+)',(root/'CMakeLists.txt').read_text())[1]
name=f'ReachTerminal-{version}';target=out/(name+'.tar.gz')
allowed={'backend':{'.hpp','.cpp'},'frontend':{'.html','.js','.css'},'db':{'.sql'},'scripts':{'.py','.sh'},'tests':{'.cpp','.py','.mjs'},'docs':{'.md','.json','.png'},'examples':{'.json','.csv','.xml'},'.github':{'.yml','.yaml'}}
files=[root/f for f in ['CMakeLists.txt','README.md','LICENSE','.gitignore','.clang-format']]
for directory,extensions in allowed.items():
 for f in (root/directory).rglob('*'):
  if f.is_file() and not f.is_symlink() and f.suffix in extensions and '__pycache__' not in f.parts:files.append(f)
with tarfile.open(target,'w:gz') as tar:
 for f in sorted(files):
  if f.exists():tar.add(f,arcname=name+'/'+str(f.relative_to(root)),recursive=False)
digest=hashlib.sha256(target.read_bytes()).hexdigest();(out/(target.name+'.sha256')).write_text(digest+'  '+target.name+'\n')
print(f'Source release: {target}; {len(files)} allowlisted files; SHA256 {digest}')
