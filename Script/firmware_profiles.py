"""Firmware profiles for PS5 Trophy Unlocker.

This module intentionally contains only static metadata and pure helper functions
so it can be unit-tested without a console or PS5Debug dependency.
"""

from __future__ import annotations


FIRMWARE_PROFILES = {
    "6.02": {
        "label": "6.02",
        "shellcore_patch": True,
        "strategy": "shellcore-gate",
        "notes": "Known SceShellCore gate patch with verified byte signatures.",
        "patches": (
            {
                "name": "getApp0DirPath isDebuggerOrAppHome gate",
                "file_offset": 0x004D790C,
                "original_hex": "0f 84 87 00 00 00",
                "patched_hex": "90 90 90 90 90 90",
            },
            {
                "name": "getSceSysDirPath isDebuggerOrAppHome gate",
                "file_offset": 0x004D7BEC,
                "original_hex": "0f 84 86 00 00 00",
                "patched_hex": "90 90 90 90 90 90",
            },
        ),
    },
    "13.60": {
        "label": "13.60",
        "shellcore_patch": False,
        "strategy": "trophy2-uds-safe",
        "notes": (
            "Safe profile: never applies the FW 6.02 ShellCore offsets. "
            "Use the Trophy2/UDS payload path and collect debug logs before "
            "adding any firmware-specific memory patch."
        ),
        "patches": (),
    },
}


def normalize_firmware(value: str | None) -> str:
    text = (value or "").strip().lower()
    if text.startswith("fw"):
        text = text[2:].strip(" :-_")
    if text in {"", "auto", "unknown"}:
        return "auto"
    return text


def get_firmware_profile(value: str | None):
    return FIRMWARE_PROFILES.get(normalize_firmware(value))


def shellcore_patch_allowed(value: str | None) -> bool:
    profile = get_firmware_profile(value)
    return bool(profile and profile["shellcore_patch"])


def profile_strategy(value: str | None) -> str:
    profile = get_firmware_profile(value)
    if profile is None:
        return "unsupported"
    return str(profile["strategy"])
