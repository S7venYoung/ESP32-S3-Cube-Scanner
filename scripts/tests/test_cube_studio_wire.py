"""Use original Studio protobuf schemas, not the implementation's encoder."""
from pathlib import Path
import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

@unittest.skipUnless(importlib.util.find_spec('grpc_tools'), 'protobuf interoperability dependencies unavailable')
class StudioWireTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        cls.target = Path(cls.folder.name)
        proto = ROOT / 'experimental/cube_dongle/studio_proto'
        subprocess.run([sys.executable, '-m', 'grpc_tools.protoc', f'-I{proto}', f'--python_out={cls.target}', *map(str, proto.glob('*.proto'))], check=True, capture_output=True)
        sys.path.insert(0, str(cls.target))
        import studio_pb2
        cls.proto = studio_pb2
        cls.binary = cls.target / 'studio-test'
        subprocess.run([shutil.which('clang++') or 'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(ROOT / 'experimental/cube_dongle/studio_test.cc'), '-o', str(cls.binary)], check=True, capture_output=True)

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
                payload.append(byte); escaped = False
            elif byte == 0xac:
                escaped = True
            else:
                self.assertNotIn(byte, (0xab, 0xad)); payload.append(byte)
        self.assertFalse(escaped)
        return self.proto.Response.FromString(payload)

    def call(self, subsystem, field, value=True):
        request = self.proto.Request(request_id=self.sequence)
        self.sequence += 1
        body = getattr(request, subsystem)
        destination = getattr(body, field)
        if isinstance(value, dict):
            for name, val in value.items():
                setattr(destination, name, val)
        elif field in ('list_custom_subsystems',):
            destination.SetInParent()
        else:
            setattr(body, field, value)
        response = self.exchange(request.SerializeToString().hex()).request_response
        self.assertEqual(response.request_id, request.request_id)
        return response

    def set_key(self, usage=5, position=26, behavior=1):
        request = self.proto.Request(request_id=self.sequence)
        self.sequence += 1
        edit = request.keymap.set_layer_binding
        edit.layer_id = 0; edit.key_position = position
        edit.binding.behavior_id = behavior; edit.binding.param1 = 0x70000 | usage
        response = self.exchange(request.SerializeToString().hex()).request_response
        self.assertEqual(response.request_id, request.request_id)
        return response

    def test_handshake_behaviors_and_layout(self):
        self.assertEqual(self.call('core', 'get_device_info').core.get_device_info.name, 'Cube Velvet Pro')
        self.assertEqual(self.call('core', 'get_lock_state').core.get_lock_state, 0)
        self.assertEqual(len(self.call('custom', 'list_custom_subsystems', {}).custom.list_custom_subsystems.subsystems), 0)
        self.assertEqual(list(self.call('behaviors', 'list_all_behaviors').behaviors.list_all_behaviors.behaviors), [1, 2, 3, 4, 5, 6])
        details = self.call('behaviors', 'get_behavior_details', {'behavior_id': 1}).behaviors.get_behavior_details
        self.assertEqual(details.id, 1); self.assertTrue(details.metadata)
        layouts = self.call('keymap', 'get_physical_layouts').keymap.get_physical_layouts
        self.assertEqual(len(layouts.layouts), 1); self.assertEqual(len(layouts.layouts[0].keys), 61)
        self.assertEqual(layouts.layouts[0].keys[56].r, -3000)
        keymap = self.call('keymap', 'get_keymap').keymap.get_keymap
        self.assertEqual([layer.name for layer in keymap.layers], ['BASE', 'NVA', 'SYS'])
        self.assertEqual([len(layer.bindings) for layer in keymap.layers], [61]*3)
        self.assertEqual(keymap.available_layers, 0)

    def test_unlock_edit_save_and_reload(self):
        self.assertEqual(self.set_key().meta.simple_error, 1)
        self.assertEqual(self.exchange('unlock').notification.core.lock_state_changed, 1)
        self.assertEqual(self.set_key().keymap.set_layer_binding, 0)
        self.assertTrue(self.call('keymap', 'check_unsaved_changes').keymap.check_unsaved_changes)
        self.assertTrue(self.call('keymap', 'save_changes').keymap.save_changes.ok)
        self.exchange('reload')
        keymap = self.call('keymap', 'get_keymap').keymap.get_keymap
        self.assertEqual(keymap.layers[0].bindings[26].param1, 0x70005)
        self.assertFalse(self.call('keymap', 'check_unsaved_changes').keymap.check_unsaved_changes)
        self.assertEqual(self.call('core', 'get_lock_state').core.get_lock_state, 0)

    def test_save_failure_and_discard(self):
        self.exchange('unlock'); self.set_key(); self.exchange('fail-save')
        self.assertEqual(self.call('keymap', 'save_changes').keymap.save_changes.err, 1)
        self.assertTrue(self.call('keymap', 'check_unsaved_changes').keymap.check_unsaved_changes)
        self.assertTrue(self.call('keymap', 'discard_changes').keymap.discard_changes)
        self.assertFalse(self.call('keymap', 'check_unsaved_changes').keymap.check_unsaved_changes)

    def test_client_manual_lock_still_rejects_writes(self):
        self.exchange('unlock')
        self.assertTrue(self.call('core', 'lock').meta.no_response)
        self.assertEqual(self.call('core', 'get_lock_state').core.get_lock_state, 0)
        self.assertEqual(self.set_key().meta.simple_error, 1)

    def test_negative_position_and_unsupported_behavior_do_not_change_map(self):
        self.exchange('unlock')
        self.assertEqual(self.set_key(position=-1).keymap.set_layer_binding, 1)
        self.assertNotEqual(self.set_key(behavior=900).keymap.set_layer_binding, 0)
        self.assertFalse(self.call('keymap', 'check_unsaved_changes').keymap.check_unsaved_changes)
