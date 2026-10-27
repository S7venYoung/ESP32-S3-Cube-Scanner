from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class UsbExclusiveTests(unittest.TestCase):
    def test_disconnect_policy_executes_production_helper(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        helper = source[source.index('bool MustDisconnectComputer('):source.index('void ApplyPending()')]
        program = '''#include <cassert>
enum class HostCommand {None,Usb,Select,Clear,ClearAll,ClearSlot,Repair,Ble,Auto,Pair};
''' + helper + '''
int main(){
 assert(MustDisconnectComputer(HostCommand::Usb));
 assert(MustDisconnectComputer(HostCommand::Select));
 assert(MustDisconnectComputer(HostCommand::Clear));
 assert(MustDisconnectComputer(HostCommand::ClearAll));
 assert(MustDisconnectComputer(HostCommand::ClearSlot));
 assert(MustDisconnectComputer(HostCommand::Repair));
 assert(!MustDisconnectComputer(HostCommand::Ble));
 assert(!MustDisconnectComputer(HostCommand::Auto));
 assert(!MustDisconnectComputer(HostCommand::None));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'usb.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'usb')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_disconnect_order_pending_lifetime_and_late_connect(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        tick = source[source.index('void CubeDualTick()'):source.index('void CubeDualSubmit(')]
        self.assertIn('transition && OldReleased() && physical_neutral', tick)
        self.assertIn('MustDisconnectComputer(pending.command)', tick)
        self.assertLess(tick.index('MustDisconnectComputer(pending.command)'),
                        tick.index('ApplyPending();'))
        apply = source[source.index('void ApplyPending()'):source.index('bool CubeBleHostInit()')]
        disconnect = apply[apply.index('if (MustDisconnectComputer(request.command))'):]
        self.assertLess(disconnect.index('ble_gap_terminate('), disconnect.index('pending = {};'))
        self.assertLess(disconnect.index('pending = {};'), disconnect.index('SaveSelection('))
        self.assertIn('next_output = Output::Usb;', apply)
        self.assertIn('if (request.command == HostCommand::Pair)\n        next_output = Output::Ble;', apply)
        gap = source[source.index('int Gap(ble_gap_event* event'):source.index('enum Field :')]
        self.assertLess(gap.index('preference == Output::Usb'),
                        gap.index('connection = event->connect.conn_handle;'))
        late = gap[gap.index('if (preference == Output::Usb'):gap.index('// Multiple advertisements')]
        self.assertIn('ble_gap_terminate(event->connect.conn_handle', late)
        self.assertNotIn('delete_peer', late)
        self.assertNotIn('CubeSplitForget', apply)


if __name__ == '__main__':
    unittest.main()
