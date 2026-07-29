#!/usr/bin/env python3

import argparse
import re
from pathlib import Path


NPU_SIGNALS = [
    "ingress_event",
    "dispatch_event",
    "engine_start_event",
    "engine_done_event",
    "fault_event",
    "sync_event",
    "mte4_busy",
    "mte1_busy",
    "mte2_busy",
    "vcu_busy",
    "cube_busy",
    "fixpipe_busy",
    "gm_file_io_busy",
    "scheduler_queue_size",
    "mte4_queue_size",
    "mte1_queue_size",
    "mte2_queue_size",
    "vcu_queue_size",
    "cube_queue_size",
    "fixpipe_queue_size",
    "gm_file_io_queue_size",
    "mte4_instruction",
    "mte1_instruction",
    "mte2_instruction",
    "vcu_instruction",
    "cube_instruction",
    "fixpipe_instruction",
    "gm_file_io_instruction",
]


def parse_integer(value: str) -> int:
    return int(value, 0)


def read_ascii_vcd(path: Path) -> str:
    data = path.read_bytes()
    if any(byte > 0x7F for byte in data):
        raise SystemExit(1)
    return data.decode("ascii")


def require_needles(text: str, needles: list[str]) -> None:
    for needle in needles:
        if needle not in text:
            raise SystemExit(1)


def check_structure(args: argparse.Namespace) -> None:
    text = read_ascii_vcd(args.vcd_file)
    require_needles(
        text,
        [
            "$timescale",
            "$scope module cluster $end",
            "$scope module cpu $end",
            "npu_cmd_event",
            "npu_backpressure_event",
            "npu_clock",
            "commit_event",
            "commit_valid",
            "commit_pc",
            "commit_insn",
        ],
    )

    for signal in ["commit_pc", "commit_insn"]:
        pattern = rf"\$var\s+wire\s+32\s+\S+\s+{signal}\s+\[31:0\]\s+\$end"
        if not re.search(pattern, text):
            raise SystemExit(1)

    commit_insn = re.search(
        r"\$var\s+wire\s+32\s+(\S+)\s+commit_insn\s+\[31:0\]\s+\$end",
        text,
    )
    if not commit_insn or not re.search(
        rf"\bb[01]*1[01]*\s+{re.escape(commit_insn.group(1))}\b", text
    ):
        raise SystemExit(1)

    for npu_id in range(args.npu_count):
        require_needles(text, [f"$scope module npu{npu_id} $end"])
        require_needles(text, NPU_SIGNALS)


def check_signal_asserted(args: argparse.Namespace) -> None:
    text = read_ascii_vcd(args.vcd_file)
    match = re.search(
        rf"\$var\s+\w+\s+\d+\s+(\S+)\s+{re.escape(args.signal_name)}\s+\$end",
        text,
    )
    if not match:
        raise SystemExit(1)

    identifier = re.escape(match.group(1))
    if not re.search(rf"(?m)^1{identifier}$", text):
        raise SystemExit(1)


def check_signal_minimum(args: argparse.Namespace) -> None:
    text = read_ascii_vcd(args.vcd_file)
    match = re.search(
        rf"\$var\s+\w+\s+32\s+(\S+)\s+{re.escape(args.signal_name)}\s+\[31:0\]\s+\$end",
        text,
    )
    if not match:
        raise SystemExit(1)

    identifier = re.escape(match.group(1))
    values = [
        int(bits, 2)
        for bits in re.findall(rf"(?m)^b([01]+)\s+{identifier}$", text)
    ]
    if not values or max(values) < args.minimum_value:
        raise SystemExit(1)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check Xai NPU VCD traces.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    structure = subparsers.add_parser("structure")
    structure.add_argument("--vcd-file", required=True, type=Path)
    structure.add_argument("--npu-count", required=True, type=parse_integer)
    structure.set_defaults(func=check_structure)

    asserted = subparsers.add_parser("signal-asserted")
    asserted.add_argument("--vcd-file", required=True, type=Path)
    asserted.add_argument("--signal-name", required=True)
    asserted.set_defaults(func=check_signal_asserted)

    minimum = subparsers.add_parser("signal-minimum")
    minimum.add_argument("--vcd-file", required=True, type=Path)
    minimum.add_argument("--signal-name", required=True)
    minimum.add_argument("--minimum-value", required=True, type=parse_integer)
    minimum.set_defaults(func=check_signal_minimum)

    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
