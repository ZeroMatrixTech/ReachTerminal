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

MINIMUM = (21, 0, 13)  # October 2026 CPU; eMudhra protection is checked separately below.
# CVE-2026-91403: roots that validate the compromised eMudhra intermediates directly or via cross-signs.
ANCHORS = {
    'd7a7a0fb5d7e2731d771e9484ebcdef71d5f0c3e0a2948782bc83ee0ea699ef4',  # AAA Certificate Services (Sectigo)
    '86a1ecba089c4a8d3bbe2734c612ba341d813e043cf9e8a862cd5c57a36bbe6b',  # emSign ECC Root CA - G3
    '7dd78d5f4f13459a83dff9abbba62edbaf6f2d102bf257fd712f4d9f2746ed8d',  # emSign Root TLS CA - G3
    'f2edaaff3d9b701fb4d14c9f285f8c0bad90fc3bcc1fa07d47f4718edaba34a5',  # emSign Root CS CA - G3
}
# blocked.certs entries added by OpenJDK 8395142 (21.0.13.0.2): 16 certificates plus the alternate
# signature encodings an attacker could replay. The JDK matches these upper-case values exactly.
BLOCKED = set('''
    0164D7CEAE5BB909C272AF151FF44B1177AE9B47D9424B94F1D9558C30934D19 10E7E5BFDB55CFB78D0C21366654046182087A1960C079682513795665AA0C6B
    169A0523C5E071AA4E298286DBD4EE0E40C8C06ABB6B0C077D132F001FA19240 21182E577E5106B669322ABE6086FFF6B101E8C83FD2D01DC32D854961AAC832
    33C1BCDC6DA52DF950B3B14121127DA1955F7F31F828497A7608986EB2DDCC09 37E866582D43C35D7C17D47794B974F291B3047B868DC123382477F7A08AEFA8
    3B88E4B77FC53C45CA216700E39FC0EA7621A0A40FCB268215082E35D2BD5829 3C5456D4219BA2372273867FAC9A07104600391E96C7FDCDE3B4420C0F1F69BF
    42C4ACCD12C4F59597925E1A08A7A90102608543809308D50EC427E118BA1493 432EC25F0D28DE9006D97BF752F17BBB2CD41F669933FCB072D9A1148D0261F3
    45A883F349AEDFD5F3A649B5498FFB121712BA8B448F3F2A91DBC5A766FCDDBE 4B2460A466F95FD40113BCDCCA2CEB29B3B093921DE45474DA7DF48874C630B1
    4DED6F955374E40B8558A611A9719B2AB69AA6852AC50966EDC4B07DE4D4277C 50E71B08B9AC407B93D50EE90100CB6B6B41C956DC7AA86C5BAA38AE713CFD6A
    558EC6CA13DA9B800FEDC9ACC3C18CA98BB19768F1309B60B5BB89793B3481CB 579480AEA166290BD3C643589E795FC0D0E54E86505D180F1DFF99EFD3199512
    59CC5FD35A737B84682EDA26AD23F5ADE8AA847D5BEB3B38AB6CD62347925A66 5D8FC3ECDDB4DDB243AEE5C306BCFDE670ADF2D66B193DD3AA2D6B2FB91869BE
    5F1DED4DD590AF74631290CF073B122A2AAC6A1FA479778A0BD43579BD832699 62B7F9868263F72F6911FC78CEAF72C1979158B7368BF648683BB2AA39B02E72
    7485B51C0ED192878FA1D2F948FB529AE1842CC5086C4B67F7BE5B2039DE9489 75D8853BE99C87678E6C7BF9F684B31454D81DEB1C7F37B9FBC026A268337536
    7BA62FEC54E9D040556FCA70A6A88F03761C178FA1F7CB8390A73076849F55A2 84112972B8E59D14560AE7E7119654474AAD119990CF3722BFC1E3B91615E1FD
    8F580C6E34CEE7EE57B93BA9C0AC5FAEDD7BF3085F5EE9C6C0175A2555B111D0 8FED925362F62FFF90DD835BE6ADC32B02DEC8DF5E82D83E512F1C977D066E82
    912A48745BFF2F14D1798B27858613DE1C0EB03C2BDE630AFD0DDD5251E1EBE1 98B896625C0C230ADEEC3EB2CF6AAD5001ECF9156A68644E82374CE5833BC25B
    9E39DE2881273ACC225A149C72BA3E0F51424963669AB633EE1DC7DF5DB067B4 9E7617035CFF537C837E078A7CDC7FD9EB77BCE7DDF3DC6DBD5C50AAFC1F1FA7
    A0ED2032C5249EE1A653F1F88005D9E779E385DF533B5C521137CEE5176CD7FC A104B1314DF8E02F7DE27DDC287D0961D6E6824AD4DAFC3D11064F24C1B653D5
    A4328E058C867AF9CA8D468C388B72C250CE84BDB6BAF7F8C47C9A0B1CB35EEC A496E23AC0CAB8155FC9B117B97EAE368EFCBB9041AEC8804D5A308D2D01C2D8
    A4B72DBD37256324B557B9176F8CBA4310DDFFBCC4609977E2463F3504A716C8 A61FE79B019062DD3C6420BC20EAF64702CC1063C546A084DD0A0BCB915A14A9
    AB7292F82267333031F512379FDBADA949DFA49D3C046CC8FA7F34117C342986 B7121039CAF13E03E52507F42DB56FA802FF119DA7591ABA8A2E842133DB7C9C
    B75104427B6A5BE55033AF50DC0FE3102181BE130E761D487A4E8178C93317DD C5F1531C6331659C75A6411A088A65DAD7518FD91F57B64537A651B6DCD76AE0
    C65CBC98295549BA940494A6214B9706E7115E64F3FE269412C5B48AEACB2222 C93F7B1EEF27CFD0F227DFE3F057D63833533890B7230B1821A424BD40848E33
    CB6E15165AC802E9B3F993F16C25D41F4E81C21EBDE71AE1B012AC00BF9C7C3F CD740652E552D93EE64D7C9D1CC25228E466895889CEE25A11C962AB09906978
    D8A50FA0068E9558E6C3BEE18CDB03B7EECBC3C56C1B2336B98AEE11644994B8 E7568FE6E12331F4FD7DE61DA4C7559D0F8EB192A218D4741A3BA75184333838
    EF34030C01B13BF42B534072DCC3C75B13C009DC9241119ADA39E2C15E2AFD2E FD9D5C2DBBD4995AB69B8D50C439D9527B00529199C2FD01B2174EB58F07021F
'''.split())


def validate_version(text):
    match = re.search(r'^\s*java\.version\s*=\s*(\S+)', text, re.M)
    if not match:
        raise ValueError('Cannot identify actual Java version')
    version = match[1]
    if re.search(r'(?:-ea|-internal)', version):
        raise ValueError('Preview/internal Java is not approved for the Gateway')
    # Compare every numeric component: 21.0.12.1 < 21.0.13 <= 21.0.13.0.2.
    parts = re.match(r'^(\d+(?:\.\d+)*)(?:[+-]|$)', version)
    numbers = tuple(map(int, parts[1].split('.'))) if parts else ()
    if not numbers or numbers[0] != 21 or numbers < MINIMUM:
        raise ValueError('Gateway requires security-updated Java 21.0.13 or newer in the 21.x line; update the actual selected runtime')
    return version


def blocked_certs(home):
    # Same Properties layout the JDK loads from lib/security/blocked.certs.
    path = home/'lib/security/blocked.certs'
    if not path.is_file():
        return set()
    algorithm, entries = None, set()
    for line in path.read_text(errors='replace').splitlines():
        line = line.strip()
        if not line or line[0] in '#!':
            continue
        key, _, value = line.partition('=')
        if key.strip() == 'Algorithm':
            algorithm = value.strip()
        else:
            entries.add(key.strip())
    return entries if algorithm == 'SHA-256' else set()


def emudhra_mitigation(home, fingerprints):
    if not fingerprints & ANCHORS:
        return 'no-affected-root'
    missing = BLOCKED - blocked_certs(home)
    if missing:
        raise ValueError('Java trust store still trusts compromised eMudhra chains (CVE-2026-91403): '
                         f'{len(missing)} of {len(BLOCKED)} blocked.certs fingerprints are missing; '
                         'install a runtime carrying OpenJDK 8395142 (21.0.13.0.2 or newer) or a trust store without the affected roots')
    return 'blocked.certs'


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
    mitigation = emudhra_mitigation(home, fingerprints)
    return dict(java=str(java), java_home=str(home), version=version, emudhra=mitigation,
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
