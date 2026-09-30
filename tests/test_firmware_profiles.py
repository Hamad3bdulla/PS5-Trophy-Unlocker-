import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "Script" / "firmware_profiles.py"
SPEC = importlib.util.spec_from_file_location("firmware_profiles", MODULE_PATH)
firmware_profiles = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(firmware_profiles)


class FirmwareProfileTests(unittest.TestCase):
    def test_normalizes_common_fw_forms(self):
        self.assertEqual(firmware_profiles.normalize_firmware("13.60"), "13.60")
        self.assertEqual(firmware_profiles.normalize_firmware("FW 13.60"), "13.60")
        self.assertEqual(firmware_profiles.normalize_firmware("fw:6.02"), "6.02")
        self.assertEqual(firmware_profiles.normalize_firmware("auto"), "auto")

    def test_fw_602_allows_verified_shellcore_patch(self):
        profile = firmware_profiles.get_firmware_profile("6.02")
        self.assertIsNotNone(profile)
        self.assertTrue(profile["shellcore_patch"])
        self.assertEqual(profile["strategy"], "shellcore-gate")
        self.assertEqual(len(profile["patches"]), 2)
        self.assertTrue(firmware_profiles.shellcore_patch_allowed("6.02"))

    def test_fw_1360_is_safe_no_write_profile(self):
        profile = firmware_profiles.get_firmware_profile("13.60")
        self.assertIsNotNone(profile)
        self.assertFalse(profile["shellcore_patch"])
        self.assertEqual(profile["strategy"], "trophy2-uds-safe")
        self.assertEqual(profile["patches"], ())
        self.assertFalse(firmware_profiles.shellcore_patch_allowed("13.60"))

    def test_unknown_firmware_cannot_patch(self):
        self.assertIsNone(firmware_profiles.get_firmware_profile("99.99"))
        self.assertFalse(firmware_profiles.shellcore_patch_allowed("99.99"))
        self.assertEqual(firmware_profiles.profile_strategy("99.99"), "unsupported")

    def test_auto_never_enables_shellcore_patch(self):
        self.assertIsNone(firmware_profiles.get_firmware_profile("auto"))
        self.assertFalse(firmware_profiles.shellcore_patch_allowed("auto"))


if __name__ == "__main__":
    unittest.main()
