#!/usr/bin/env python3

import argparse
import re
from pathlib import Path


NPU_SIGNALS = [
    "scheduler.ingress_event",
    "scheduler.dispatch_event",
    "scheduler.queue_size",
    "fault.event",
    "sync.event",
    "mte4.start_event",
    "mte4.done_event",
    "mte4.busy",
    "mte4.queue_size",
    "mte4.instruction",
    "mte1.start_event",
    "mte1.done_event",
    "mte1.busy",
    "mte1.queue_size",
    "mte1.instruction",
    "mte2.start_event",
    "mte2.done_event",
    "mte2.busy",
    "mte2.queue_size",
    "mte2.instruction",
    "vcu.start_event",
    "vcu.done_event",
    "vcu.busy",
    "vcu.queue_size",
    "vcu.instruction",
    "cube.start_event",
    "cube.done_event",
    "cube.busy",
    "cube.queue_size",
    "cube.instruction",
    "fixpipe.start_event",
    "fixpipe.done_event",
    "fixpipe.busy",
    "fixpipe.queue_size",
    "fixpipe.instruction",
    "file_io.start_event",
    "file_io.done_event",
    "file_io.busy",
    "file_io.queue_size",
    "file_io.instruction",
    "niu.start_event",
    "niu.done_event",
    "niu.packet_sent_event",
    "niu.packet_received_event",
    "niu.ack_event",
    "niu.busy",
    "niu.command_queue_size",
    "niu.tx_queue_size",
    "niu.rx_queue_size",
    "niu.instruction",
]


class VcdSignal:
    def __init__(self, path: str, identifier: str, width: int):
        self.path = path
        self.identifier = identifier
        self.width = width


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


def declaration_name(raw_name: str) -> str:
    return re.sub(r"\s+\[[^\]]+\]$", "", raw_name).strip()


def parse_signals(text: str) -> list[VcdSignal]:
    signals = []
    scopes = []
    for line in text.splitlines():
        scope = re.match(r"\$scope\s+\S+\s+(\S+)\s+\$end", line)
        if scope:
            scopes.append(scope.group(1))
            continue
        if line.startswith("$upscope"):
            if scopes:
                scopes.pop()
            continue
        variable = re.match(r"\$var\s+\w+\s+(\d+)\s+(\S+)\s+(.+?)\s+\$end", line)
        if not variable:
            continue
        width = int(variable.group(1))
        identifier = variable.group(2)
        name = declaration_name(variable.group(3))
        signals.append(VcdSignal(".".join(scopes + [name]), identifier, width))
    return signals


def find_signal(text: str, signal_path: str, width: int | None = None) -> VcdSignal:
    matches = [
        signal for signal in parse_signals(text)
        if (signal.path == signal_path or signal.path.endswith("." + signal_path))
        and (width is None or signal.width == width)
    ]
    if len(matches) != 1:
        raise SystemExit(1)
    return matches[0]


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

    find_signal(text, "cpu.commit_pc", 32)
    commit_insn = find_signal(text, "cpu.commit_insn", 32)
    if not re.search(
        rf"\bb[01]*1[01]*\s+{re.escape(commit_insn.identifier)}\b", text
    ):
        raise SystemExit(1)

    for npu_id in range(args.npu_count):
        require_needles(text, [f"$scope module npu{npu_id} $end"])
        for signal in NPU_SIGNALS:
            find_signal(text, f"npu{npu_id}.{signal}")


def check_signal_asserted(args: argparse.Namespace) -> None:
    text = read_ascii_vcd(args.vcd_file)
    identifier = re.escape(find_signal(text, args.signal_name).identifier)
    if not re.search(rf"(?m)^1{identifier}$", text):
        raise SystemExit(1)


def check_signal_minimum(args: argparse.Namespace) -> None:
    text = read_ascii_vcd(args.vcd_file)
    identifier = re.escape(find_signal(text, args.signal_name, 32).identifier)
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
