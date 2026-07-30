#!/usr/bin/env python3

import argparse
import struct
from pathlib import Path


def parse_integer(value: str) -> int:
    return int(value, 0)


def gm_input_file_name(hart_id: int, index: int) -> str:
    return f"GMInputFile_{hart_id}_{index}.bin"


def write_u32le(path: Path, values: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = b"".join(struct.pack("<I", value) for value in values)
    path.write_bytes(data)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate Xai VCU smoke input and expected output binaries."
    )
    parser.add_argument(
        "--gm-file-io-root",
        required=True,
        type=Path,
        help="Directory containing simulator GM file I/O binaries.",
    )
    parser.add_argument(
        "--hart-id",
        required=True,
        type=parse_integer,
        help="Hart id encoded in the GM input filename.",
    )
    parser.add_argument(
        "--index",
        required=True,
        type=parse_integer,
        help="Per-hart input index encoded in the GM input filename.",
    )
    parser.add_argument(
        "--expected-bin",
        required=True,
        type=Path,
        help="Output path for the expected VADD result data.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    lhs = [1, 2, 3, 4]
    rhs = [10, 20, 30, 40]
    expected = [left + right for left, right in zip(lhs, rhs)]

    write_data_bin = args.gm_file_io_root / gm_input_file_name(args.hart_id, args.index)
    write_u32le(write_data_bin, lhs + rhs)
    write_u32le(args.expected_bin, expected)


if __name__ == "__main__":
    main()
