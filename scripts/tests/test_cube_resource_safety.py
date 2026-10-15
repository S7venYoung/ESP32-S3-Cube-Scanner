"""Resource regressions; desktop stack budgets are not ESP32 stack measurements."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class ResourceSafetyTest(unittest.TestCase):
    def test_usb_read_errors_block(self):
        source = (ROOT / 'main/scanner/cube_usb_dongle.cc').read_text()
        body = source[source.index('int CubeCdcRead'):source.index('void CubeCdcWrite')]
        error = body[body.index('ESP_OK)'):body.index('if (!count)')]
        self.assertIn('vTaskDelay(', error)
        self.assertLess(error.index('vTaskDelay('), error.index('return 0;'))

    def test_sync_always_yields_and_has_read_only_diagnostics(self):
        source = (ROOT / 'main/scanner/codex_sync.cc').read_text()
        body = source[source.index('void SyncTask'):source.index('CodexSnapshot GetCodexSnapshot')]
        self.assertIn('vTaskDelay(1);', body[:body.index('CubeStudioPoll();')])
        self.assertIn('line == "DONGLE DIAG"', body)
        self.assertIn('uxTaskGetStackHighWaterMark(nullptr)', body)

    @unittest.skipUnless(shutil.which('clang++'), 'clang stack reports unavailable')
    def test_optimized_desktop_stack_budgets(self):
        with tempfile.TemporaryDirectory() as folder:
            for source, symbol, budget in (
                ('extended_wire_test.cc', 'FastKeymap', 4096),
                ('studio_test.cc', '3Rpc6Handle', 4096),
            ):
                output = Path(folder) / source.removesuffix('.cc')
                subprocess.run([shutil.which('clang++'), '-std=c++17', '-Os', '-fstack-usage',
                                str(ROOT / 'experimental/cube_dongle' / source), '-o', str(output)],
                               check=True, capture_output=True)
                reports = list(Path(folder).glob('*.su'))
                values = [int(line.split('\t')[1]) for report in reports
                          for line in report.read_text().splitlines()
                          if symbol in line and len(line.split('\t')) >= 3]
                self.assertTrue(values, symbol)
                self.assertLessEqual(max(values), budget, symbol)

    def test_failed_notification_batch_does_not_repeat_timeouts(self):
        source = (ROOT / 'main/scanner/cube_studio.cc').read_text()
        body = source[source.index('void Send('):source.index('struct RelayEvent')]
        self.assertIn('if (sessions[transport].tx_failed)', body)
        self.assertIn('sessions[transport].tx_failed = true;', body)
        self.assertLess(body.index('if (sessions[transport].tx_failed)'), body.index('CubeCdcWritePacket('))
