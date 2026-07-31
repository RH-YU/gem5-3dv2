#!/usr/bin/env python3

import argparse
import struct
from pathlib import Path


def parse_integer(value: str) -> int:
    return int(value, 0)


def file_io_file_name(prefix: str, hart_id: int, index: int) -> str:
    return f"{prefix}_{hart_id}_{index}.bin"


def write_u32le(path: Path, values: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"".join(struct.pack("<I", value) for value in values))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate recursive RVV NPU add backpressure input and expected data."
    )
    parser.add_argument("--file-io-root", required=True, type=Path)
    parser.add_argument("--hart-id", required=True, type=parse_integer)
    parser.add_argument("--index", required=True, type=parse_integer)
    parser.add_argument("--recursive-add-count", required=True, type=parse_integer)
    parser.add_argument("--expected-bin", required=True, type=Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    accumulator = [1, 2, 3, 4]
    addend = [1, 2, 3, 4]
    expected = [
        initial + args.recursive_add_count * step
        for initial, step in zip(accumulator, addend)
    ]

    input_file = args.file_io_root / file_io_file_name(
        "GMInputFile", args.hart_id, args.index
    )
    write_u32le(input_file, accumulator + addend)
    write_u32le(args.expected_bin, expected)


if __name__ == "__main__":
    main()
