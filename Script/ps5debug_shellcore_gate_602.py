import argparse
import asyncio
import sys

from ps4debug import PS4Debug
from ps4debug.core import VMProtection

from firmware_profiles import (
    get_firmware_profile,
    normalize_firmware,
    shellcore_patch_allowed,
)


PORT = 744
TEXT_FILE_OFFSET = 0x4000
PAGE_SIZE = 0x4000
EXIT_UNSUPPORTED = 20
EXIT_ERROR = 1


class RawVMProtection:
    def __init__(self, value):
        self.value = int(value)


class ShellcoreBaseError(RuntimeError):
    def __init__(self, message, attempts):
        super().__init__(message)
        self.attempts = attempts


def field(obj, name, default=""):
    try:
        return getattr(obj, name)
    except Exception:
        try:
            return obj[name]
        except Exception:
            return default


def clean(value):
    if value is None:
        return ""
    text = str(value)
    if "\x00" in text:
        text = text.split("\x00", 1)[0]
    return text.strip()


def hex_bytes(data):
    if data is None:
        return "<none>"
    return bytes(data).hex(" ")


def diff_hex(actual, expected):
    if actual is None:
        return "<none>"
    actual = bytes(actual)
    expected = bytes(expected)
    out = []
    for index, (a, e) in enumerate(zip(actual, expected)):
        marker = "==" if a == e else "!="
        out.append(f"{index:02d}:{a:02x}{marker}{e:02x}")
    if len(actual) != len(expected):
        out.append(f"len:{len(actual)}!={len(expected)}")
    return " ".join(out)


def align_down(value, align):
    return value & ~(align - 1)


def prot_to_vm(prot):
    try:
        return VMProtection(prot)
    except ValueError:
        return RawVMProtection(prot & int(VMProtection.VM_PROT_ALL))


def decode_patches(profile):
    rows = []
    for patch in profile["patches"]:
        rows.append(
            {
                "name": patch["name"],
                "file_offset": int(patch["file_offset"]),
                "original": bytes.fromhex(patch["original_hex"]),
                "patched": bytes.fromhex(patch["patched_hex"]),
            }
        )
    return tuple(rows)


async def find_shellcore_pid(ps5, forced_pid):
    if forced_pid is not None:
        return forced_pid, None

    processes = await ps5.get_processes()
    best = None
    for proc in processes:
        pid = int(field(proc, "pid", 0))
        try:
            info = await ps5.get_process_info(pid)
        except Exception:
            continue

        name = clean(field(info, "name")) or clean(field(proc, "name"))
        path = clean(field(info, "path"))
        joined = f"{name} {path}".lower()
        if "sceshellcore" in joined:
            best = (pid, info)
            break

    if best is None:
        raise RuntimeError("SceShellCore not found. Verify that PS5Debug is active.")
    return best


async def read_patch_bytes(ps5, pid, base, patches):
    rows = []
    for patch in patches:
        delta = patch["file_offset"] - TEXT_FILE_OFFSET
        addr = base + delta
        current = await ps5.read_memory(pid, addr, len(patch["original"]))
        rows.append((patch, addr, bytes(current) if current is not None else None))
    return rows


async def find_shellcore_base(ps5, pid, maps, patches):
    candidates = []
    min_size = patches[-1]["file_offset"] - TEXT_FILE_OFFSET + 0x1000
    for m in maps:
        name = clean(field(m, "name"))
        start = int(field(m, "start", 0))
        end = int(field(m, "end", 0))
        prot = int(field(m, "prot", 0))
        size = end - start
        if not (prot & int(VMProtection.VM_PROT_EXECUTE)):
            continue
        if size < min_size:
            continue
        candidates.append((m, name, start, end, prot, size))

    preferred = [
        item for item in candidates
        if "sceshellcore" in item[1].lower() or "shellcore" in item[1].lower()
    ]
    ordered = preferred + [item for item in candidates if item not in preferred]

    attempts = []
    for m, name, start, end, prot, size in ordered:
        try:
            rows = await read_patch_bytes(ps5, pid, start, patches)
        except Exception as exc:
            attempts.append((name, start, end, prot, f"read error {type(exc).__name__}: {exc}"))
            continue

        score = 0
        for patch, _addr, data in rows:
            if data == patch["original"] or data == patch["patched"]:
                score += 1

        attempts.append((name, start, end, prot, ", ".join(hex_bytes(row[2]) for row in rows)))
        if score == len(patches):
            return m, rows, attempts

    raise ShellcoreBaseError(
        "SceShellCore base not found with the expected firmware bytes. "
        "Patch refused because the signature does not match or ShellCore is not ready.",
        attempts,
    )


async def set_protection(ps5, pid, address, length, prot):
    page = align_down(address, PAGE_SIZE)
    end = address + length
    page_len = align_down(end + PAGE_SIZE - 1, PAGE_SIZE) - page
    return await ps5.change_protection(pid, page, page_len, prot)


async def main():
    parser = argparse.ArgumentParser(
        description=(
            "Firmware-aware SceShellCore gate helper. "
            "Only firmware profiles with verified byte signatures are writable."
        )
    )
    parser.add_argument("host", nargs="?", default="192.168.1.131")
    parser.add_argument("--port", type=int, default=PORT, help="PS5Debug/ps4debug port.")
    parser.add_argument("--pid", type=int, help="Forced SceShellCore PID; otherwise auto-detected.")
    parser.add_argument(
        "--firmware",
        default="auto",
        help="Firmware profile to use, for example 6.02 or 13.60. Default: auto.",
    )
    parser.add_argument(
        "--mode",
        choices=("check", "patch", "restore"),
        default="check",
        help="check reads only, patch applies a verified RAM patch, restore restores verified original bytes.",
    )
    parser.add_argument("--debug-diff", action="store_true", help="Show a PC-side diff of expected/read bytes.")
    parser.add_argument(
        "--force",
        action="store_true",
        help=(
            "Legacy compatibility flag. It cannot enable writes for a firmware "
            "profile that has no verified patch."
        ),
    )
    args = parser.parse_args()

    firmware = normalize_firmware(args.firmware)
    profile = get_firmware_profile(firmware)

    print(f"[connect] {args.host}:{args.port}")
    print(f"[firmware] requested={firmware}")

    if profile is None:
        print("[result] STOP: firmware profile is unknown. No ShellCore write will be attempted.")
        return EXIT_UNSUPPORTED

    print(f"[profile] strategy={profile['strategy']}")
    print(f"[profile] {profile['notes']}")

    if not shellcore_patch_allowed(firmware):
        print(
            f"[safe] FW {profile['label']} has no verified ShellCore patch in this project. "
            "No memory write will be attempted."
        )
        if args.mode in {"patch", "restore"}:
            print("[result] SAFE-SKIP: continue with Trophy2/UDS payload diagnostics instead.")
        else:
            print("[result] SAFE: profile is supported without a ShellCore patch.")
        return 0

    patches = decode_patches(profile)

    ps5 = PS4Debug(args.host, args.port)
    try:
        print(f"[version] {await ps5.get_version()}")
    except Exception as exc:
        print(f"[version] error {type(exc).__name__}: {exc}")

    pid, info = await find_shellcore_pid(ps5, args.pid)
    if info is None:
        try:
            info = await ps5.get_process_info(pid)
        except Exception:
            info = None

    if info is not None:
        print(
            f"[shellcore] pid={pid} name={clean(field(info, 'name'))} "
            f"path={clean(field(info, 'path'))}"
        )
    else:
        print(f"[shellcore] pid={pid}")

    maps = await ps5.get_process_maps(pid)
    print(f"[maps] count={len(maps)}")

    try:
        base_map, rows, attempts = await find_shellcore_base(ps5, pid, maps, patches)
    except Exception as exc:
        print(f"[base] error {type(exc).__name__}: {exc}")
        print("[result] STOP: patch not applied. Firmware signature or ShellCore layout did not match.")
        if args.debug_diff and isinstance(exc, ShellcoreBaseError):
            print("[debug-diff] executable-base attempts")
            for name, start, end, prot, observed in exc.attempts:
                print(
                    f"  map={name or '<anon>'} start=0x{start:016x} "
                    f"end=0x{end:016x} prot=0x{prot:x} observed={observed}"
                )
        return EXIT_UNSUPPORTED

    base = int(field(base_map, "start", 0))
    prot = int(field(base_map, "prot", 0))
    print(
        f"[base] 0x{base:016x} prot=0x{prot:x} "
        f"name={clean(field(base_map, 'name'))}"
    )

    all_ok = True
    already_patched = True
    for patch, addr, data in rows:
        status = "unknown"
        if data == patch["original"]:
            status = "original"
            already_patched = False
        elif data == patch["patched"]:
            status = "patched"
        else:
            all_ok = False
            already_patched = False
        print(
            f"[check] {patch['name']} file=0x{patch['file_offset']:08x} "
            f"addr=0x{addr:016x} bytes={hex_bytes(data)} status={status}"
        )
        if args.debug_diff:
            print(f"[debug-diff] {patch['name']}")
            print(f"  expected_original={hex_bytes(patch['original'])}")
            print(f"  expected_patched ={hex_bytes(patch['patched'])}")
            print(f"  actual           ={hex_bytes(data)}")
            print(f"  diff_vs_original={diff_hex(data, patch['original'])}")
            print(f"  diff_vs_patched ={diff_hex(data, patch['patched'])}")

    if args.mode == "check":
        if all_ok:
            print("[result] OK: all verified patch sites were located.")
            return 0
        print("[result] STOP: unexpected bytes; no write was attempted.")
        return EXIT_UNSUPPORTED

    target_key = "patched" if args.mode == "patch" else "original"
    expected_key = "original" if args.mode == "patch" else "patched"

    if args.mode == "patch" and already_patched:
        print("[result] already patched.")
        return 0

    writes = []
    for patch, addr, data in rows:
        expected = patch[expected_key]
        target = patch[target_key]
        if data != expected:
            print(
                f"[stop] {patch['name']} expected={hex_bytes(expected)} "
                f"read={hex_bytes(data)}. Signature mismatch; refusing write."
            )
            return EXIT_UNSUPPORTED
        writes.append((patch, addr, target))

    first_addr = min(addr for _patch, addr, _target in writes)
    last_addr = max(addr + len(target) for _patch, addr, target in writes)

    print("[protect] temporary RWX")
    print(await set_protection(ps5, pid, first_addr, last_addr - first_addr, VMProtection.VM_PROT_ALL))

    for patch, addr, target in writes:
        result = await ps5.write_memory(pid, addr, target)
        print(f"[write] {patch['name']} addr=0x{addr:016x} -> {hex_bytes(target)} result={result}")

    print("[protect] restoring protection")
    print(await set_protection(ps5, pid, first_addr, last_addr - first_addr, prot_to_vm(prot)))

    print("[verify]")
    rows = await read_patch_bytes(ps5, pid, base, patches)
    ok = True
    for patch, addr, data in rows:
        expected = patch[target_key]
        match = data == expected
        ok = ok and match
        print(f"  addr=0x{addr:016x} bytes={hex_bytes(data)} match={match}")

    if ok:
        print(f"[result] {args.mode} OK")
        return 0

    print(f"[result] {args.mode} incomplete")
    return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
