#!/usr/bin/env python3
"""Validate the actual Gateway Java executable before starting an account session."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

MINIMUM = (21, 0, 13)
DENIED = {'86a1ecba089c4a8d3bbe2734c612ba341d813e043cf9e8a862cd5c57a36bbe6b'}


def validate_version(text):
    match = re.search(r'^\s*java\.version\s*=\s*(\S+)', text, re.M)
    if not match:
        raise ValueError('Cannot identify actual Java version')
    version = match[1]
    if re.search(r'(?:-ea|-internal)', version):
        raise ValueError('Preview/internal Java is not approved for the Gateway')
    parts = re.match(r'^(\d+)\.(\d+)\.(\d+)(?:[.+-]|$)', version)
    if not parts or tuple(map(int, parts.groups())) < MINIMUM or int(parts[1]) != 21:
        raise ValueError('Gateway requires security-updated Java 21.0.13 or newer in the 21.x line; update the actual selected runtime')
    return version


def inspect(java):
    java = Path(java).resolve(strict=True)
    env = dict(os.environ, LC_ALL='C')
    # Ambient options can change the effective trust store or executable startup.
    for key in ('JAVA_TOOL_OPTIONS', 'JDK_JAVA_OPTIONS', '_JAVA_OPTIONS'):
        if env.get(key):
            raise ValueError('Unset '+key+' before starting the Gateway; review runtime settings explicitly')
    result = subprocess.run([str(java), '-XshowSettings:properties', '-version'],
                            capture_output=True, text=True, timeout=15, env=env, check=True)
    text = result.stdout+'\n'+result.stderr
    version = validate_version(text)
    home = re.search(r'^\s*java\.home\s*=\s*(.+)$', text, re.M)
    if not home:
        raise ValueError('Cannot identify actual java.home')
    home = Path(home[1].strip()).resolve(strict=True)
    if java != (home/'bin/java').resolve(strict=True):
        raise ValueError('Java executable and java.home differ')
    store = home/'lib/security/cacerts'
    # JSSE selects jssecacerts before cacerts when it exists.
    if (home/'lib/security/jssecacerts').exists():
        store = home/'lib/security/jssecacerts'
    if not store.is_file():
        raise ValueError('Java trust store is unavailable')
    certs = subprocess.run([str(home/'bin/keytool'), '-J-Duser.language=en', '-list', '-v',
                            '-keystore', str(store), '-storepass', 'changeit'],
                           capture_output=True, text=True, timeout=20, env=env, check=True)
    fingerprints = {x.replace(':', '').lower() for x in re.findall(r'SHA256:\s*([0-9A-Fa-f:]{95})', certs.stdout)}
    if not fingerprints:
        raise ValueError('Cannot verify Java trust-store fingerprints')
    if fingerprints & DENIED:
        raise ValueError('Java trust store still trusts the compromised emSign root; install an updated vendor runtime/trust store')
    return dict(java=str(java), java_home=str(home), version=version,
                trust_store=str(store), trust_store_sha256=hashlib.sha256(store.read_bytes()).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--java', default=shutil.which('java'))
    parser.add_argument('--java-home', action='store_true')
    args = parser.parse_args()
    try:
        if not args.java:
            raise ValueError('Select an updated runtime with REACH_JAVA_HOME')
        info = inspect(args.java)
        print(info['java_home'] if args.java_home else json.dumps(info, indent=2))
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        raise SystemExit('Gateway runtime denied: '+str(exc))
