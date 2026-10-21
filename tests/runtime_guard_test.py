import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('runtime_guard',ROOT/'scripts/runtime_guard.py')
guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
class RuntimeGuardTests(unittest.TestCase):
    def test_stale_private_runtime_rejected(self):
        with self.assertRaisesRegex(ValueError,'21.0.13'):guard.validate_version('    java.version = 21.0.12.1')
    def test_fixed_21_accepted(self):
        self.assertEqual(guard.validate_version(' java.version = 21.0.13'),'21.0.13')
        self.assertEqual(guard.validate_version(' java.version = 21.0.14'),'21.0.14')
    def test_other_major_or_preview_not_silently_accepted(self):
        for version in ['17.0.21','25.0.5','21.0.13-ea','garbage']:
            with self.subTest(version=version),self.assertRaises(ValueError):guard.validate_version(' java.version = '+version)
    def test_missing_version_rejected(self):
        with self.assertRaises(ValueError):guard.validate_version('java is fine')
    def test_compromised_trust_store_rejected_and_jssecacerts_precedence(self):
        import tempfile,subprocess
        with tempfile.TemporaryDirectory() as tmp:
            home=Path(tmp);(home/'bin').mkdir();(home/'lib/security').mkdir(parents=True)
            java=home/'bin/java';java.touch()
            (home/'lib/security/cacerts').write_bytes(b'fixture')
            effective=home/'lib/security/jssecacerts';effective.write_bytes(b'override')
            props=subprocess.CompletedProcess([],0,'',f' java.version = 21.0.13\n java.home = {home}\n')
            denied=':'.join(next(iter(guard.DENIED))[i:i+2] for i in range(0,64,2))
            listing=subprocess.CompletedProcess([],0,'SHA256: '+denied,'')
            with patch.dict(guard.os.environ,{},clear=True),patch.object(guard.subprocess,'run',side_effect=[props,listing]) as run:
                with self.assertRaisesRegex(ValueError,'compromised'):guard.inspect(java)
                self.assertIn(str(effective),run.call_args.args[0])
    def test_clean_trust_store_returns_evidence(self):
        import tempfile,subprocess
        with tempfile.TemporaryDirectory() as tmp:
            home=Path(tmp);(home/'bin').mkdir();(home/'lib/security').mkdir(parents=True)
            java=home/'bin/java';java.touch();(home/'lib/security/cacerts').write_bytes(b'fixture')
            props=subprocess.CompletedProcess([],0,'',f' java.version = 21.0.13\n java.home = {home}\n')
            listing=subprocess.CompletedProcess([],0,'SHA256: '+':'.join(['ab']*32),'')
            with patch.dict(guard.os.environ,{},clear=True),patch.object(guard.subprocess,'run',side_effect=[props,listing]):
                self.assertEqual(guard.inspect(java)['version'],'21.0.13')
if __name__=='__main__':unittest.main()
