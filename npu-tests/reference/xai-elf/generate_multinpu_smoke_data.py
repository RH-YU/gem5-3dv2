#!/usr/bin/env python3

import argparse
import struct
from pathlib import Path


def write_u32le(path: Path, values: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = b"".join(struct.pack("<I", value) for value in values)
    path.write_bytes(data)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate single-CPU multi-NPU Xai smoke data."
    )
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
        help="Directory for per-NPU expected output binaries.",
    )
    parser.add_argument(
        "--npu-count",
        default=4,
        type=int,
        help="Number of NPU fixtures to generate.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.npu_count <= 0:
        raise SystemExit("--npu-count must be positive")

    for npu_id in range(args.npu_count):
        lhs = [100 * npu_id + 1, 100 * npu_id + 2,
               100 * npu_id + 3, 100 * npu_id + 4]
        rhs = [1000 * npu_id + 10, 1000 * npu_id + 20,
               1000 * npu_id + 30, 1000 * npu_id + 40]
        expected = [left + right for left, right in zip(lhs, rhs)]

        input_path = (
            args.file_io_root
            / f"npu{npu_id}"
            / "GMInputFile_0_0.bin"
        )
        expected_path = args.expected_root / f"xai_multinpu_expected_npu{npu_id}.bin"
        write_u32le(input_path, lhs + rhs)
        write_u32le(expected_path, expected)


if __name__ == "__main__":
    main()
