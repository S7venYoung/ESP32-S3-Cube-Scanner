"""Interoperate with pinned upstream custom RPC schemas, independently of C++ encoders."""
from pathlib import Path
import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

@unittest.skipUnless(importlib.util.find_spec('grpc_tools'), 'protobuf tooling unavailable')
class CustomWireTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        cls.target = Path(cls.folder.name)
        proto = ROOT / 'experimental/cube_dongle/studio_proto'
        subprocess.run([sys.executable, '-m', 'grpc_tools.protoc', f'-I{proto}', f'--python_out={cls.target}', *map(str, proto.glob('*.proto'))], check=True, capture_output=True)
        sys.path.insert(0, str(cls.target))
        cls.studio = importlib.import_module('studio_pb2')
        cls.macro = importlib.import_module('runtime_macro_pb2')
        cls.diagnostics = importlib.import_module('kscan_diagnostics_pb2')
        cls.watchdog = importlib.import_module('watchdog_pb2')
        cls.combo = importlib.import_module('runtime_combo_pb2')
        cls.binary = cls.target / 'custom-test'
        subprocess.run([shutil.which('clang++') or 'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(ROOT / 'experimental/cube_dongle/studio_custom_test.cc'), '-o', str(cls.binary)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        sys.path.remove(str(cls.target))
        cls.folder.cleanup()

    def setUp(self):
        self.process = subprocess.Popen([str(self.binary), '--rpc'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.sequence = 0

    def tearDown(self):
        self.process.stdin.close()
        self.process.wait(timeout=5)
        self.process.stdout.close()

    def exchange(self, command):
        self.process.stdin.write(command + '\n')
        self.process.stdin.flush()
        frame = bytes.fromhex(self.process.stdout.readline().strip())
        self.assertEqual(frame[:1], b'\xab')
        self.assertEqual(frame[-1:], b'\xad')
        payload, escaped = bytearray(), False
        for byte in frame[1:-1]:
            if escaped:
                payload.append(byte)
                escaped = False
            elif byte == 0xac:
                escaped = True
            else:
                payload.append(byte)
        self.assertFalse(escaped)
        return self.studio.Response.FromString(payload)

    def call(self, module, subsystem_index, field, **arguments):
        inner = module.Request()
        body = getattr(inner, field)
        body.SetInParent()
        for name, value in arguments.items():
            target = getattr(body, name)
            if hasattr(target, 'CopyFrom'):
                target.CopyFrom(value)
            elif isinstance(value, (list, tuple)):
                target.extend(value)
            else:
                setattr(body, name, value)
        request = self.studio.Request(request_id=self.sequence)
        self.sequence += 1
        request.custom.call.subsystem_index = subsystem_index
        request.custom.call.payload = inner.SerializeToString()
        response = self.exchange(request.SerializeToString().hex()).request_response
        self.assertEqual(response.request_id, request.request_id)
        self.assertEqual(response.custom.call.subsystem_index, subsystem_index)
        return module.Response.FromString(response.custom.call.payload)

    def test_registry(self):
        request = self.studio.Request(request_id=9)
        request.custom.list_custom_subsystems.SetInParent()
        response = self.exchange(request.SerializeToString().hex()).request_response
        subsystems = response.custom.list_custom_subsystems.subsystems
        self.assertEqual([s.identifier for s in subsystems], ['cormoran__runtime_macro', 'cormoran__kscan_diagnostics', 'cormoran__watchdog', 'cormoran__runtime_combo'])
        self.assertEqual([s.index for s in subsystems], [0, 1, 2, 3])

    def test_combo_config_persist_and_reload(self):
        c = self.combo
        binding = c.BehaviorBinding(behavior_id=1, param1=0x70028)
        args = dict(index=0, key_positions=[0, 1], behavior=binding, enabled=True, layer_mask=3,
                    timeout_ms=35, require_prior_idle_ms=100, slow_release_override=1, persist=True)
        self.assertEqual(self.call(c, 3, 'set_combo', **args).WhichOneof('response_type'), 'error')
        self.exchange('unlock')
        self.assertEqual(self.call(c, 3, 'set_combo', **args).WhichOneof('response_type'), 'status')
        self.call(c, 3, 'set_combo_name', index=0, name='Enter', persist=True)
        self.exchange('reload')
        combo = self.call(c, 3, 'get_combo', index=0).get_combo.combo
        self.assertEqual((combo.name, list(combo.key_positions), combo.behavior.param1), ('Enter', [0, 1], 0x70028))
        self.assertEqual((combo.layer_mask, combo.timeout_ms, combo.require_prior_idle_ms, combo.slow_release_override), (3, 35, 100, 1))
        self.assertTrue(combo.enabled)
        self.assertEqual(combo.source, c.COMBO_SOURCE_RUNTIME)
        self.assertEqual(len(self.call(c, 3, 'list_combos').list_combos.combos), 8)
        self.call(c, 3, 'delete_combo', index=0, persist=True)
        self.exchange('reload')
        self.assertFalse(self.call(c, 3, 'get_combo', index=0).get_combo.combo.enabled)

    def test_combo_invalid_positions_and_failed_flash(self):
        c = self.combo
        self.exchange('unlock')
        binding = c.BehaviorBinding(behavior_id=1, param1=0x70028)
        for keys in ([0, 0], [0, 61], [0], [0, 1, 2, 3, 4]):
            response = self.call(c, 3, 'set_combo', index=0, key_positions=keys, behavior=binding, enabled=True)
            self.assertEqual(response.WhichOneof('response_type'), 'error')
        self.call(c, 3, 'set_combo', index=0, key_positions=[0, 1], behavior=binding, enabled=True, persist=True)
        self.exchange('fail-save')
        response = self.call(c, 3, 'set_combo_name', index=0, name='not-saved', persist=True)
        self.assertEqual(response.WhichOneof('response_type'), 'error')
        self.assertEqual(self.call(c, 3, 'get_combo', index=0).get_combo.combo.name, '')

    def test_macro_edit_save_reload(self):
        m = self.macro
        self.assertEqual(self.call(m, 0, 'create_macro', name='hello').WhichOneof('response_type'), 'error')
        self.exchange('unlock')
        self.assertEqual(self.call(m, 0, 'create_macro', name='hello').WhichOneof('response_type'), 'status')
        step = m.MacroStep(tap=m.BehaviorBinding(behavior_id=1, param1=0x70004))
        self.assertEqual(self.call(m, 0, 'append_macro_step', slot=0, step=step).WhichOneof('response_type'), 'status')
        sequence = m.MacroStep(key_tap_sequence=m.KeyTapSequenceStep(packed_keys=b'\x84\x05'))
        self.call(m, 0, 'append_macro_step', slot=0, step=sequence)
        self.call(m, 0, 'save_macros')
        self.exchange('reload')
        detail = self.call(m, 0, 'get_macro', slot=0).get_macro.macro
        self.assertEqual(detail.name, 'hello')
        self.assertEqual(detail.steps[0].tap.param1, 0x70004)
        self.assertEqual(detail.steps[1].key_tap_sequence.packed_keys, b'\x84\x05')
        self.assertFalse(self.call(m, 0, 'list_macros').list_macros.macros[0].has_unsaved_changes)
        self.call(m, 0, 'rename_macro', old_name='hello', new_name='renamed', persist=True)
        self.exchange('reload')
        self.assertEqual(self.call(m, 0, 'get_macro', slot=0).get_macro.macro.name, 'renamed')
        self.call(m, 0, 'delete_macro', name='renamed')
        self.assertEqual(len(self.call(m, 0, 'list_macros').list_macros.macros), 0)

    def test_macro_failed_flash_keeps_previous_state(self):
        self.exchange('unlock')
        self.call(self.macro, 0, 'create_macro', name='old', persist=True)
        self.exchange('fail-save')
        response = self.call(self.macro, 0, 'rename_macro', old_name='old', new_name='new', persist=True)
        self.assertEqual(response.WhichOneof('response_type'), 'error')
        self.assertEqual(self.call(self.macro, 0, 'get_macro', slot=0).get_macro.macro.name, 'old')

    def test_diagnostics_reports_remote_positions_not_fake_gpio(self):
        info = self.call(self.diagnostics, 1, 'get_info').info
        self.assertEqual(info.max_positions, 61)
        self.assertEqual(info.device_count, 0)
        self.assertTrue(info.stats_enabled)
        stats = self.call(self.diagnostics, 1, 'get_stats', offset=0).stats
        self.assertEqual(stats.total, 61)
        self.assertEqual([e.position for e in stats.entries], list(range(16)))
        self.assertEqual(self.call(self.diagnostics, 1, 'reset_stats').WhichOneof('response_type'), 'error')
        self.exchange('unlock')
        self.assertEqual(self.call(self.diagnostics, 1, 'reset_stats').WhichOneof('response_type'), 'ok')

    def test_watchdog_real_incident_and_locked_delete(self):
        status = self.call(self.watchdog, 2, 'get_status').status
        self.assertEqual((status.capacity, status.stored), (16, 1))
        page = self.call(self.watchdog, 2, 'list_incidents').incident_page
        self.assertEqual(page.incidents[0].reset.cause_bits, 16)
        self.assertEqual(self.call(self.watchdog, 2, 'delete_incidents', all=True).WhichOneof('response_type'), 'error')
        self.exchange('unlock')
        self.assertEqual(self.call(self.watchdog, 2, 'delete_incidents', all=True).delete_result.deleted, 1)
        self.assertEqual(self.call(self.watchdog, 2, 'inject_test').WhichOneof('response_type'), 'error')
