#!/usr/bin/env python3
"""Package the Linux executable with embedded frontend; no runtime state copied."""
import argparse,hashlib,shutil,tarfile,platform
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
name='ReachTerminal-0.1.0-linux-'+platform.machine();folder=out/name;folder.mkdir(exist_ok=True)
shutil.copy2(root/'build/reachterminal',folder/'reachterminal');shutil.copy2(root/'LICENSE',folder/'LICENSE')
(folder/'start.sh').write_text('''#!/usr/bin/env bash
set -euo pipefail
base="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${REACH_DATABASE_URL:?请设置本机 PostgreSQL 连接 REACH_DATABASE_URL；不会打包或迁移原设备数据库和网关。}"
export REACH_ENV=user
export REACH_ROOT="${REACH_ROOT:-$base/workspace}"
export REACH_PORT="${REACH_PORT:-8788}"
mkdir -p "$REACH_ROOT"
exec "$base/reachterminal" serve
''');(folder/'start.sh').chmod(0o755)
(folder/'README.txt').write_text('''ReachTerminal 0.1.0 · Linux 本地内嵌版

HTML、JavaScript、CSS 和数据库初始化 SQL 已内嵌到 C++ 可执行程序。
使用浏览器访问程序提供的本机地址（默认 http://127.0.0.1:8788/）。
启动前设置 REACH_DATABASE_URL 指向用户自己的 PostgreSQL 数据库，运行 ./start.sh。
数据库名称不能是 reachterminal、reachterminal_demo_* 或 reachterminal_test_*。
依赖兼容的 libpq、libcurl、libssl/libcrypto、libxml2、libcpp-httplib 及其系统依赖；本包不是静态跨发行版二进制。
不需要 Node.js、Python、frontend/ 或 db/ 目录运行用户终端。
本机已安装仓库可继续用 scripts/live.sh；该脚本负责本地数据库与开发依赖配置。

程序包没有账户、密钥、CA 配置、网关、会话、用户导出或数据库。
IBKR Gateway 在每台设备独立安装、设置和登录，不能迁移或复用设备状态。
登录页关闭不会代表失败：返回数据中心，查看账户验证及行情会话两种状态。
登录监控持续三分钟，SSO 账户验证按官方限额最多每60秒一次。
行情未认证时可点击“连接行情会话（不抢占）”；终端不会自动执行或抢占其他会话。
''')
archive=out/(name+'.tar.gz')
with tarfile.open(archive,'w:gz') as t:
 for file in ['reachterminal','LICENSE','start.sh','README.txt']:t.add(folder/file,arcname=name+'/'+file,recursive=False)
(out/(archive.name+'.sha256')).write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')
print(archive)
