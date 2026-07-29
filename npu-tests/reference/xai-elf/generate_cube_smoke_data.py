#!/usr/bin/env python3

import argparse
import random
import struct
from pathlib import Path


def parse_integer(value: str) -> int:
    return int(value, 0)


def gm_file_name(prefix: str, hart_id: int, index: int) -> str:
    return f"{prefix}_{hart_id}_{index}.bin"


def write_f32le(path: Path, values: list[float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"".join(struct.pack("<f", value) for value in values))


def generate_inputs() -> tuple[list[float], list[float]]:
    rng = random.Random(0x43425545)
    m, k, n = 8, 16, 16
    lhs = [float(rng.randint(-8, 8)) for _ in range(m * k)]
    rhs = [float(rng.randint(-8, 8)) for _ in range(k * n)]
    return lhs, rhs


def matmul_8x16_16x16(lhs: list[float], rhs: list[float]) -> list[float]:
    m, k, n = 8, 16, 16
    result = []
    for row in range(m):
        for col in range(n):
            total = 0.0
            for inner in range(k):
                total += lhs[row * k + inner] * rhs[inner * n + col]
            result.append(total)
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate Xai cube smoke random input and expected output binaries."
    )
    parser.add_argument("--gm-file-io-root", required=True, type=Path)
    parser.add_argument("--hart-id", required=True, type=parse_integer)
    parser.add_argument("--index", required=True, type=parse_integer)
    parser.add_argument("--expected-bin", required=True, type=Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    lhs, rhs = generate_inputs()
    expected = matmul_8x16_16x16(lhs, rhs)

    input_file = args.gm_file_io_root / gm_file_name(
        "GMInputFile", args.hart_id, args.index
    )
    write_f32le(input_file, lhs + rhs)
    write_f32le(args.expected_bin, expected)


if __name__ == "__main__":
    main()
