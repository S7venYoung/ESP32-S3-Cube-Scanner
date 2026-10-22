"""Exercise Cube with the pinned upstream Runtime Tap Dance protobuf."""
from pathlib import Path
import importlib
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
class TapDanceWire(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.folder = Path(cls.tmp.name)
        proto = ROOT / 'experimental/cube_dongle/studio_proto'
        subprocess.run([sys.executable, '-m', 'grpc_tools.protoc', f'-I{proto}', f'--python_out={cls.folder}', str(proto / 'runtime_tapdance.proto')], check=True)
        sys.path.insert(0, str(cls.folder))
        cls.p = importlib.import_module('runtime_tapdance_pb2')
        cls.bin = cls.folder / 'td'
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(ROOT / 'experimental/cube_dongle/runtime_tapdance_test.cc'), '-o', str(cls.bin)], check=True)
    @classmethod
    def tearDownClass(cls):
        sys.path.remove(str(cls.folder))
        cls.tmp.cleanup()
    def setUp(self):
        self.proc = subprocess.Popen([str(self.bin), '--rpc'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    def tearDown(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=5)
        self.proc.stdout.close()
    def call(self, unlocked=True, **kw):
        self.proc.stdin.write(('01' if unlocked else '00') + self.p.Request(**kw).SerializeToString().hex() + '\n')
        self.proc.stdin.flush()
        return self.p.Response.FromString(bytes.fromhex(self.proc.stdout.readline()))
    def test_wire_and_persistence(self):
        g = getattr(self.call(get_global={}), "global")
        self.assertEqual(g.api_version, 2)
        self.assertEqual(g.max_entries, 16)
        a = self.call(create={'name': 'first'}).state
        self.assertEqual(a.index, 0)
        for count in (1, 2, 3):
            response = self.call(set={'index': 0, 'tap_count': count, 'binding': {'behavior_id': 1, 'param1': 0x70003 + count}})
            self.assertEqual(response.state.max_count, count)
        state = self.call(get={'index': 0}).state
        self.assertEqual(state.config.single_binding.param1, 0x70004)
        self.assertEqual(state.config.double_binding.param1, 0x70005)
        self.assertEqual(state.config.triple_binding.param1, 0x70006)
        self.call(save={})
        self.call(create={'name': 'temporary'})
        self.proc.stdin.write('REBOOT\n'); self.proc.stdin.flush()
        self.assertEqual(self.proc.stdout.readline().strip(), 'OK')
        self.assertEqual(self.call(create={'name': 'third'}).state.index, 2)
        self.assertEqual(self.call(get={'index': 0}).state.name, 'first')
        self.assertTrue(self.call(unlocked=False, create={'name':'locked'}).HasField('error'))
        self.assertTrue(self.call(set_interval={'interval_ms': 0}).HasField('error'))
        self.assertEqual(getattr(self.call(set_interval={'interval_ms': 350}), 'global').interval_ms, 350)
