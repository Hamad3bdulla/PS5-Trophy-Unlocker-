import argparse
import asyncio

from ps4debug import PS4Debug


def parse_address(text):
    return int(text, 0)


def normalize_rax(value):
    if isinstance(value, int):
        return value
    if isinstance(value, (bytes, bytearray)):
        return int.from_bytes(value[:8], "little", signed=False)
    if isinstance(value, tuple) and value:
        return normalize_rax(value[0])
    return int(value)


async def main():
    parser = argparse.ArgumentParser(
        description="Install PS4Debug RPC in a target process and call getpid() as a safe execution probe."
    )
    parser.add_argument("host", help="PS5 IP address")
    parser.add_argument("--port", type=int, default=744, help="PS5Debug port")
    parser.add_argument("--pid", type=int, required=True, help="Target process ID")
    parser.add_argument(
        "--address",
        type=parse_address,
        required=True,
        help="Remote getpid() address, e.g. 0x80abcdef0",
    )
    args = parser.parse_args()

    ps5 = PS4Debug(args.host, args.port)

    print(f"[connect] {args.host}:{args.port}")
    version = await ps5.get_version()
    print(f"[version] {version}")
    print(f"[rpc] target pid={args.pid}")
    print(f"[rpc] getpid address=0x{args.address:x}")

    try:
        rpc_stub = await ps5.install_rpc(args.pid)
    except Exception as exc:
        print(f"[rpc] install failed: {type(exc).__name__}: {exc}")
        return 2

    print(f"[rpc] stub=0x{int(rpc_stub):x}")

    try:
        result = await ps5.call(
            args.pid,
            args.address,
            rpc_stub=rpc_stub,
        )
    except Exception as exc:
        print(f"[rpc] call failed: {type(exc).__name__}: {exc}")
        return 3

    rax = normalize_rax(result)
    print(f"[rpc] getpid returned={rax}")

    if rax != args.pid:
        print(f"[rpc] FAIL: expected pid {args.pid}, got {rax}")
        return 4

    print("[rpc] SUCCESS: code executed inside the requested process")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
