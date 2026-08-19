#!/usr/bin/env python3
"""Parse the six-IMU glove serial protocol described in 串口输出解析协议文档.md."""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from collections import Counter, OrderedDict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import BinaryIO, Iterable, Iterator

HEADER = b"\xAA\x55"
FRAME_SIZE = 25
PAYLOAD_LENGTH = 0x12
SENSOR_NAMES = {
    0x50: "wrist",
    0x51: "thumb",
    0x52: "index",
    0x53: "middle",
    0x54: "ring",
    0x55: "pinky",
}
EXPECTED_ADDRESSES = frozenset(SENSOR_NAMES)


@dataclass(frozen=True)
class ImuFrame:
    address: int
    sensor: str
    sequence: int
    ax: int
    ay: int
    az: int
    gx: int
    gy: int
    gz: int
    mx: int
    my: int
    mz: int

    def to_dict(self) -> dict[str, int | str]:
        result = asdict(self)
        result["address"] = f"0x{self.address:02X}"
        return result


class FrameStreamParser:
    """Incrementally recover valid 25-byte frames from an arbitrary byte stream."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.bytes_discarded = 0
        self.header_candidates = 0
        self.invalid_length = 0
        self.invalid_crc = 0
        self.valid_frames = 0

    def feed(self, data: bytes) -> list[ImuFrame]:
        self.buffer.extend(data)
        frames: list[ImuFrame] = []

        while True:
            header_index = self.buffer.find(HEADER)
            if header_index < 0:
                keep = 1 if self.buffer.endswith(HEADER[:1]) else 0
                discard = len(self.buffer) - keep
                self.bytes_discarded += discard
                if discard:
                    del self.buffer[:discard]
                break

            if header_index:
                self.bytes_discarded += header_index
                del self.buffer[:header_index]

            if len(self.buffer) < FRAME_SIZE:
                break

            self.header_candidates += 1
            candidate = bytes(self.buffer[:FRAME_SIZE])
            if candidate[4] != PAYLOAD_LENGTH:
                self.invalid_length += 1
                self.bytes_discarded += 1
                del self.buffer[0]
                continue

            expected_crc = candidate[23] | (candidate[24] << 8)
            actual_crc = crc16_modbus(candidate[:23])
            if actual_crc != expected_crc:
                self.invalid_crc += 1
                self.bytes_discarded += 1
                del self.buffer[0]
                continue

            del self.buffer[:FRAME_SIZE]
            self.valid_frames += 1
            frames.append(decode_frame(candidate))

        return frames


class SequenceGrouper:
    """Group sensor frames by their shared 8-bit sampling sequence."""

    def __init__(self, max_pending: int = 8) -> None:
        self.pending: OrderedDict[int, dict[int, ImuFrame]] = OrderedDict()
        self.max_pending = max_pending
        self.duplicate_frames = 0

    def add(self, frame: ImuFrame) -> list[tuple[int, dict[int, ImuFrame], bool]]:
        emitted: list[tuple[int, dict[int, ImuFrame], bool]] = []
        group = self.pending.setdefault(frame.sequence, {})
        if frame.address in group:
            self.duplicate_frames += 1
        group[frame.address] = frame
        self.pending.move_to_end(frame.sequence)

        if EXPECTED_ADDRESSES.issubset(group):
            self.pending.pop(frame.sequence)
            emitted.append((frame.sequence, group, True))

        while len(self.pending) > self.max_pending:
            sequence, stale_group = self.pending.popitem(last=False)
            emitted.append((sequence, stale_group, False))

        return emitted

    def flush(self) -> list[tuple[int, dict[int, ImuFrame], bool]]:
        emitted = [
            (sequence, group, EXPECTED_ADDRESSES.issubset(group))
            for sequence, group in self.pending.items()
        ]
        self.pending.clear()
        return emitted


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def decode_frame(frame: bytes) -> ImuFrame:
    if len(frame) != FRAME_SIZE:
        raise ValueError(f"frame must be {FRAME_SIZE} bytes")
    values = struct.unpack(">9h", frame[5:23])
    address = frame[2]
    return ImuFrame(
        address=address,
        sensor=SENSOR_NAMES.get(address, f"unknown_0x{address:02X}"),
        sequence=frame[3],
        ax=values[0],
        ay=values[1],
        az=values[2],
        gx=values[3],
        gy=values[4],
        gz=values[5],
        mx=values[6],
        my=values[7],
        mz=values[8],
    )


def chunks(stream: BinaryIO, chunk_size: int = 4096) -> Iterator[bytes]:
    while True:
        data = stream.read(chunk_size)
        if not data:
            return
        yield data


def group_to_dict(
    sequence: int, group: dict[int, ImuFrame], complete: bool
) -> dict[str, object]:
    missing = [SENSOR_NAMES[address] for address in sorted(EXPECTED_ADDRESSES - group.keys())]
    sensors = {
        SENSOR_NAMES.get(address, f"unknown_0x{address:02X}"): frame.to_dict()
        for address, frame in sorted(group.items())
    }
    return {
        "sequence": sequence,
        "sequence_hex": f"0x{sequence:02X}",
        "complete": complete,
        "missing": missing,
        "sensors": sensors,
    }


def format_group(sequence: int, group: dict[int, ImuFrame], complete: bool) -> str:
    status = "complete" if complete else "partial"
    missing = EXPECTED_ADDRESSES - group.keys()
    lines = [f"SEQ 0x{sequence:02X} ({sequence:3d}) {status}: {len(group)}/6 sensors"]
    for address in sorted(group):
        frame = group[address]
        zero_marker = " ZERO" if not any(
            (frame.ax, frame.ay, frame.az, frame.gx, frame.gy, frame.gz, frame.mx, frame.my, frame.mz)
        ) else ""
        lines.append(
            f"  0x{address:02X} {frame.sensor:<6} "
            f"acc=({frame.ax:6d},{frame.ay:6d},{frame.az:6d}) "
            f"gyro=({frame.gx:6d},{frame.gy:6d},{frame.gz:6d}) "
            f"mag=({frame.mx:6d},{frame.my:6d},{frame.mz:6d}){zero_marker}"
        )
    if missing:
        names = ", ".join(SENSOR_NAMES[address] for address in sorted(missing))
        lines.append(f"  missing: {names}")
    return "\n".join(lines)


def process_stream(
    data_chunks: Iterable[bytes],
    *,
    json_lines: bool,
    complete_only: bool,
    max_groups: int | None,
    flush_at_end: bool,
) -> tuple[FrameStreamParser, SequenceGrouper, Counter[str]]:
    parser = FrameStreamParser()
    grouper = SequenceGrouper()
    stats: Counter[str] = Counter()
    stop = False

    def emit(sequence: int, group: dict[int, ImuFrame], complete: bool) -> None:
        nonlocal stop
        stats["groups"] += 1
        stats["complete_groups" if complete else "partial_groups"] += 1
        if complete_only and not complete:
            return
        if json_lines:
            print(json.dumps(group_to_dict(sequence, group, complete), ensure_ascii=False))
        else:
            print(format_group(sequence, group, complete))
        stats["printed_groups"] += 1
        if max_groups is not None and stats["printed_groups"] >= max_groups:
            stop = True

    for data in data_chunks:
        stats["input_bytes"] += len(data)
        for frame in parser.feed(data):
            stats[f"address_0x{frame.address:02X}"] += 1
            if frame.address not in EXPECTED_ADDRESSES:
                stats["unknown_address_frames"] += 1
            for sequence, group, complete in grouper.add(frame):
                emit(sequence, group, complete)
                if stop:
                    break
        if stop:
            break

    if flush_at_end and not stop:
        for sequence, group, complete in grouper.flush():
            emit(sequence, group, complete)
            if stop:
                break

    return parser, grouper, stats


def print_summary(parser: FrameStreamParser, grouper: SequenceGrouper, stats: Counter[str]) -> None:
    print("\nSummary", file=sys.stderr)
    print(f"  input bytes:       {stats['input_bytes']}", file=sys.stderr)
    print(f"  valid frames:      {parser.valid_frames}", file=sys.stderr)
    print(f"  complete groups:   {stats['complete_groups']}", file=sys.stderr)
    print(f"  partial groups:    {stats['partial_groups']}", file=sys.stderr)
    print(f"  invalid CRC:       {parser.invalid_crc}", file=sys.stderr)
    print(f"  invalid LEN:       {parser.invalid_length}", file=sys.stderr)
    print(f"  discarded bytes:   {parser.bytes_discarded}", file=sys.stderr)
    print(f"  duplicate frames:  {grouper.duplicate_frames}", file=sys.stderr)
    address_counts = [
        f"0x{address:02X}={stats[f'address_0x{address:02X}']}"
        for address in sorted(EXPECTED_ADDRESSES)
    ]
    print(f"  address counts:    {', '.join(address_counts)}", file=sys.stderr)
    if parser.valid_frames == 0:
        print(
            "  note: no valid AA 55 / LEN 12 / Modbus-CRC frames were found; "
            "the input may use another protocol or capture format.",
            file=sys.stderr,
        )


def file_command(args: argparse.Namespace) -> int:
    with args.path.open("rb") as stream:
        parser, grouper, stats = process_stream(
            chunks(stream, args.chunk_size),
            json_lines=args.json_lines,
            complete_only=args.complete_only,
            max_groups=args.max_groups,
            flush_at_end=True,
        )
    print_summary(parser, grouper, stats)
    return 0 if parser.valid_frames else 2


def serial_command(args: argparse.Namespace) -> int:
    try:
        import serial
    except ImportError:
        print("pyserial is required: python -m pip install pyserial", file=sys.stderr)
        return 3

    try:
        with serial.Serial(args.port, args.baud, timeout=args.timeout) as port:
            print(
                f"Reading {args.port} at {args.baud} baud; press Ctrl+C to stop.",
                file=sys.stderr,
            )
            started = time.monotonic()

            def serial_chunks() -> Iterator[bytes]:
                while args.duration is None or time.monotonic() - started < args.duration:
                    data = port.read(args.chunk_size)
                    if data:
                        yield data

            parser, grouper, stats = process_stream(
                serial_chunks(),
                json_lines=args.json_lines,
                complete_only=args.complete_only,
                max_groups=args.max_groups,
                flush_at_end=False,
            )
    except KeyboardInterrupt:
        print("\nStopped by user.", file=sys.stderr)
        return 130
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 4

    print_summary(parser, grouper, stats)
    return 0 if parser.valid_frames else 2


def add_output_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--json-lines", action="store_true", help="print one JSON object per group")
    parser.add_argument("--complete-only", action="store_true", help="print only complete six-sensor groups")
    parser.add_argument("--max-groups", type=int, help="stop after printing this many groups")
    parser.add_argument("--chunk-size", type=int, default=4096, help="read size in bytes (default: 4096)")


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Parse six-IMU glove AA 55 serial frames and group them by SEQ."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    file_parser = subparsers.add_parser("file", help="parse a binary capture file")
    file_parser.add_argument("path", type=Path)
    add_output_options(file_parser)
    file_parser.set_defaults(handler=file_command)

    serial_parser = subparsers.add_parser("serial", help="parse a live serial port")
    serial_parser.add_argument("port", help="serial port, for example COM5 or /dev/ttyUSB0")
    serial_parser.add_argument("--baud", type=int, default=921600)
    serial_parser.add_argument("--timeout", type=float, default=0.1)
    serial_parser.add_argument("--duration", type=float, help="stop after this many seconds")
    add_output_options(serial_parser)
    serial_parser.set_defaults(handler=serial_command)
    return parser


def main() -> int:
    args = build_argument_parser().parse_args()
    if args.max_groups is not None and args.max_groups <= 0:
        raise SystemExit("--max-groups must be greater than zero")
    if args.chunk_size <= 0:
        raise SystemExit("--chunk-size must be greater than zero")
    return args.handler(args)


if __name__ == "__main__":
    raise SystemExit(main())
