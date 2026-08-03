#!/usr/bin/env python3

import argparse
import random
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate NIU/NOC smoke data.")
    parser.add_argument(
        "--file-io-root",
        required=True,
        type=Path,
        help="Root directory containing per-NPU file I/O directories.",
    )
    parser.add_argument(
        "--expected-root",
        required=True,
        type=Path,
        help="Directory for expected NIU output binaries.",
    )
    parser.add_argument(
        "--byte-count",
        default=256,
        type=int,
        help="Number of bytes transferred by each NIU command.",
    )
    return parser.parse_args()


def deterministic_payload(byte_count: int) -> bytes:
    rng = random.Random(0x4E4955)
    return bytes(rng.randrange(0, 256) for _ in range(byte_count))


def write_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main() -> None:
    args = parse_args()
    if args.byte_count <= 0:
        raise SystemExit("--byte-count must be positive")

    payload = deterministic_payload(args.byte_count)
    write_bytes(args.file_io_root / "npu0" / "GMInputFile_0_0.bin", payload)
    write_bytes(args.expected_root / "xai_niu_expected_remote_ub.bin", payload)
    write_bytes(args.expected_root / "xai_niu_expected_remote_gm.bin", payload)


if __name__ == "__main__":
    main()
