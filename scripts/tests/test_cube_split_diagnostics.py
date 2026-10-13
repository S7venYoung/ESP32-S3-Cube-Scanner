import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('cube_diag', Path(__file__).resolve().parents[1] / 'cube-split-diagnostics.py')
diag = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diag)


class CubeSplitDiagnosticsTest(unittest.TestCase):
    def test_identity_types(self):
        self.assertEqual(diag.validate_identity('c1:23:45:67:89:ab', 1), 'C1:23:45:67:89:AB')
        self.assertEqual(diag.validate_identity('01:23:45:67:89:AB', 0), '01:23:45:67:89:AB')
        for value in ['01:23:45:67:89:AB', '00:00:00:00:00:00', 'FF:FF:FF:FF:FF:FF', 'C1:23:45:67:89:AB x']:
            with self.assertRaises(ValueError): diag.validate_identity(value, 1)

    def test_status_is_input_only(self):
        state = diag.decode_status('CUBE-SPLIT-BENCH/1 783 HID-OFF')
        self.assertEqual(state['pressed'], 3)
        self.assertTrue(state['left_ready'] and state['right_ready'])
        for response in ['CUBE-CODEX/2', 'CUBE-SPLIT-BENCH/1 0 HID-ON', 'CUBE-SPLIT-BENCH/1 16 HID-OFF', 'CUBE-SPLIT-BENCH/1 65535 HID-OFF']:
            with self.assertRaises(ValueError): diag.decode_status(response)
