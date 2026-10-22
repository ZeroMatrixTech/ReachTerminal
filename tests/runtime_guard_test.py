import importlib.util
from pathlib import Path
import subprocess,tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('runtime_guard',ROOT/'scripts/runtime_guard.py')
guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
AAA='d7a7a0fb5d7e2731d771e9484ebcdef71d5f0c3e0a2948782bc83ee0ea699ef4'
ECC='86a1ecba089c4a8d3bbe2734c612ba341d813e043cf9e8a862cd5c57a36bbe6b'
OTHER='ab'*32
def colons(h):return ':'.join(h[i:i+2] for i in range(0,64,2)).upper()
def inspect(home,version,fingerprints,blocked=None):
    # Fake JDK home: java -XshowSettings output, keytool listing and optional blocked.certs.
    (home/'bin').mkdir(exist_ok=True);(home/'lib/security').mkdir(parents=True,exist_ok=True)
    java=home/'bin/java';java.touch();(home/'lib/security/cacerts').write_bytes(b'fixture')
    if blocked is not None:(home/'lib/security/blocked.certs').write_text(blocked)
    props=subprocess.CompletedProcess([],0,'',f' java.version = {version}\n java.home = {home}\n')
    listing=subprocess.CompletedProcess([],0,''.join('SHA256: '+colons(f)+'\n' for f in fingerprints),'')
    with patch.dict(guard.os.environ,{},clear=True),patch.object(guard.subprocess,'run',side_effect=[props,listing]) as run:
        return guard.inspect(java),run
def vendor_fix(entries=None,algorithm='SHA-256'):
    return 'Algorithm='+algorithm+'\n'+'\n'.join(sorted(guard.BLOCKED if entries is None else entries))+'\n'
class RuntimeGuardTests(unittest.TestCase):
    def test_stale_private_runtime_rejected(self):
        with self.assertRaisesRegex(ValueError,'21.0.13'):guard.validate_version('    java.version = 21.0.12.1')
    def test_fixed_21_accepted(self):
        for version in ['21.0.13','21.0.13.0.2','21.0.14']:
            with self.subTest(version=version):self.assertEqual(guard.validate_version(' java.version = '+version),version)
    def test_all_version_components_compared(self):
        for version in ['21.0.12.9','21.0.12.1.2','21']:
            with self.subTest(version=version),self.assertRaises(ValueError):guard.validate_version(' java.version = '+version)
    def test_other_major_or_preview_not_silently_accepted(self):
        for version in ['17.0.21','25.0.5','21.0.13-ea','garbage']:
            with self.subTest(version=version),self.assertRaises(ValueError):guard.validate_version(' java.version = '+version)
    def test_missing_version_rejected(self):
        with self.assertRaises(ValueError):guard.validate_version('java is fine')
    def test_disclosure_fingerprint_counts(self):
        # 8 ECDSA-signed certificates x 4 encodings + 8 RSA-signed x 2.
        self.assertEqual((len(guard.ANCHORS),len(guard.BLOCKED)),(4,48))
        self.assertTrue(all(x==x.upper() and len(x)==64 for x in guard.BLOCKED))
    def test_compromised_trust_store_rejected_and_jssecacerts_precedence(self):
        with tempfile.TemporaryDirectory() as tmp:
            home=Path(tmp);(home/'bin').mkdir();(home/'lib/security').mkdir(parents=True)
            java=home/'bin/java';java.touch();(home/'lib/security/cacerts').write_bytes(b'fixture')
            effective=home/'lib/security/jssecacerts';effective.write_bytes(b'override')
            props=subprocess.CompletedProcess([],0,'',f' java.version = 21.0.13\n java.home = {home}\n')
            listing=subprocess.CompletedProcess([],0,'SHA256: '+colons(ECC),'')
            with patch.dict(guard.os.environ,{},clear=True),patch.object(guard.subprocess,'run',side_effect=[props,listing]) as run:
                with self.assertRaisesRegex(ValueError,'compromised'):guard.inspect(java)
                self.assertIn(str(effective),run.call_args.args[0])
    def test_aaa_cross_sign_without_emsign_root_rejected(self):
        # Removing only emSign ECC Root CA - G3 leaves the Sectigo cross-signed chains trusted.
        with tempfile.TemporaryDirectory() as tmp,self.assertRaisesRegex(ValueError,'CVE-2026-91403'):
            inspect(Path(tmp),'21.0.13',[AAA,OTHER])
    def test_vendor_blocked_certs_fix_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            info,_=inspect(Path(tmp),'21.0.13.0.2',[AAA,ECC,OTHER],vendor_fix())
            self.assertEqual((info['version'],info['emudhra']),('21.0.13.0.2','blocked.certs'))
    def test_incomplete_blocked_certs_rejected(self):
        partial=sorted(guard.BLOCKED)[1:]
        for name,blocked in [('missing file',None),('missing entry',vendor_fix(partial)),
                             ('lower case',vendor_fix({x.lower() for x in guard.BLOCKED})),
                             ('other algorithm',vendor_fix(algorithm='SHA-1'))]:
            with self.subTest(name),tempfile.TemporaryDirectory() as tmp,self.assertRaisesRegex(ValueError,'blocked.certs'):
                inspect(Path(tmp),'21.0.13.0.2',[AAA,ECC],blocked)
    def test_clean_trust_store_returns_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            info,_=inspect(Path(tmp),'21.0.13',[OTHER])
            self.assertEqual((info['version'],info['emudhra']),('21.0.13','no-affected-root'))
if __name__=='__main__':unittest.main()
