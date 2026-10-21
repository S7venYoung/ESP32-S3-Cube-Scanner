"""Decode Cube extensions with pinned upstream protobuf implementations."""
from pathlib import Path
import importlib
import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

@unittest.skipUnless(importlib.util.find_spec('grpc_tools'), 'protobuf tooling unavailable')
class ExtendedWireTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        cls.target = Path(cls.folder.name)
        proto = ROOT / 'experimental/cube_dongle/studio_proto'
        subprocess.run([sys.executable, '-m', 'grpc_tools.protoc', f'-I{proto}',
                        f'--python_out={cls.target}', *map(str, proto.glob('*.proto'))],
                       check=True, capture_output=True)
        sys.path.insert(0, str(cls.target))
        cls.modules = [importlib.import_module(name + '_pb2') for name in
                       ('fast_keymap', 'default_layer', 'os_detection', 'custom_settings', 'runtime_input_processor', 'runtime_sensor_rotate')]
        cls.binary = cls.target / 'extended-wire'
        subprocess.run([shutil.which('clang++') or 'g++', '-std=c++17', '-Wall', '-Wextra',
                        '-Werror', str(ROOT / 'experimental/cube_dongle/extended_wire_test.cc'),
                        '-o', str(cls.binary)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        sys.path.remove(str(cls.target))
        cls.folder.cleanup()

    def setUp(self):
        self.process = subprocess.Popen([str(self.binary)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, text=True)

    def tearDown(self):
        self.process.stdin.close()
        self.process.wait(timeout=5)
        self.process.stdout.close()

    def call(self, index, request):
        self.process.stdin.write(f'{index:02x}' + request.SerializeToString().hex() + '\n')
        self.process.stdin.flush()
        return self.modules[index].Response.FromString(bytes.fromhex(self.process.stdout.readline()))

    def test_fast_geometry_and_metadata(self):
        p = self.modules[0]
        response = self.call(0, p.Request(get_snapshot=True))
        self.assertEqual(response.snapshot.behavior_count, 12)
        self.assertEqual(response.snapshot.available_layers, 4)
        response = self.call(0, p.Request(get_physical_layout=p.GetPhysicalLayoutRequest(layout_index=0)))
        self.assertEqual(len(response.get_physical_layout.layout.keys), 61)
        response = self.call(0, p.Request(list_behaviors=p.ListBehaviorsRequest(details=2, include_alias=True)))
        self.assertEqual(len(response.list_behaviors.behaviors), 12)
        self.assertEqual(len({b.def_fp for b in response.list_behaviors.behaviors}), 12)
        self.assertTrue(all(b.details.display_name for b in response.list_behaviors.behaviors))

    def test_negative_default_layer_sentinels(self):
        p = self.modules[1]
        response = self.call(1, p.Request(set_endpoint_layer=p.SetEndpointLayerRequest(endpoint_index=0, value=-2)))
        self.assertEqual(response.WhichOneof('response_type'), 'state')
        self.assertEqual(response.state.endpoints[0].value, -2)
        self.assertEqual(response.state.resolved_layer, -1)
        response = self.call(1, p.Request(set_os_layer=p.SetOsLayerRequest(os=2, value=1)))
        self.assertEqual(response.state.os_layers[2].value, 1)

    def test_os_override_and_real_bond_mask(self):
        p = self.modules[2]
        response = self.call(2, p.Request(get_state=p.GetStateRequest()))
        self.assertEqual([v.bonded for v in response.state.ble_profiles], [True, True, False, False, False])
        self.assertEqual(response.state.usb.detected, p.OS_UNKNOWN)
        response = self.call(2, p.Request(set_ble_override=p.SetBleOverrideRequest(profile_index=1, os=p.OS_MACOS)))
        self.assertEqual(response.set_ble_override.profile.effective, p.OS_MACOS)

    def test_custom_settings_write_and_constraints(self):
        p = self.modules[3]
        ref = p.SettingRef(key='hold_tap.term_ms')
        response = self.call(3, p.Request(write_setting=p.WriteSettingRequest(setting=ref,
                             value=p.SettingValue(int32_value=350), mode=1)))
        self.assertEqual(response.WhichOneof('response_type'), 'status')
        response = self.call(3, p.Request(get_setting=p.GetSettingRequest(setting=ref, require_meta=True, require_default=True)))
        setting = response.get_setting.setting
        self.assertEqual(setting.value.int32_value, 350)
        self.assertEqual(setting.default_value.int32_value, 200)
        self.assertEqual(setting.meta.write_permission, p.SETTING_PERMISSION_SECURE)
        self.assertEqual(setting.meta.constraints[0].range.max.int32_value, 5000)

    def test_pointer_write_modes_save_discard_reset(self):
        p = self.modules[4]
        request = p.Request(set_scale_multiplier=p.SetScaleMultiplierRequest(id=0, value=3, write_mode=p.WRITE_MODE_MEMORY))
        self.assertEqual(self.call(4, request).WhichOneof('response_type'), 'set_scale_multiplier')
        get = p.Request(get_input_processor=p.GetInputProcessorRequest(id=0))
        self.assertEqual(self.call(4, get).get_input_processor.processor.scale_multiplier, 3)
        self.call(4, p.Request(discard_all_settings=p.DiscardAllSettingsRequest()))
        self.assertEqual(self.call(4, get).get_input_processor.processor.scale_multiplier, 1)
        self.call(4, request)
        self.call(4, p.Request(save_all_settings=p.SaveAllSettingsRequest()))
        self.call(4, p.Request(set_scale_multiplier=p.SetScaleMultiplierRequest(id=0, value=5, write_mode=p.WRITE_MODE_TEMPORARY)))
        self.call(4, p.Request(discard_all_settings=p.DiscardAllSettingsRequest()))
        self.assertEqual(self.call(4, get).get_input_processor.processor.scale_multiplier, 3)
        self.call(4, p.Request(reset_all_settings=p.ResetAllSettingsRequest()))
        self.assertEqual(self.call(4, get).get_input_processor.processor.scale_multiplier, 1)

    def test_encoder_real_bindings_and_success(self):
        p = self.modules[5]
        response = self.call(5, p.Request(get_sensors=p.GetSensorsRequest()))
        self.assertEqual(len(response.get_sensors.sensors), 1)
        binding = p.Binding(behavior_id=1, param1=0x70004, tap_ms=20)
        response = self.call(5, p.Request(set_layer_cw_binding=p.SetLayerCwBindingRequest(sensor_index=0, layer=1, binding=binding)))
        self.assertTrue(response.set_layer_cw_binding.success)
        response = self.call(5, p.Request(get_all_layer_bindings=p.GetAllLayerBindingsRequest(sensor_index=0)))
        self.assertEqual(response.get_all_layer_bindings.bindings[1].cw_binding, binding)
