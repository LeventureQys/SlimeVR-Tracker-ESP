'''主控已解算姿态帧解析：31 字节 × 6 节点 → 网页 trajectory 帧。

协议：AA 55 | node_id | sequence(u16 LE) | 6×float32 LE | CRC16-Modbus(u16 LE)。
0x50：wrist_wxyz + time_s + reserved；0x51–0x55：相对四元数 + bend_deg/sway_deg。
'''
from __future__ import annotations

import math
import struct
from typing import Any

POSE_SYNC0 = 170
POSE_SYNC1 = 85
POSE_FRAME_SIZE = 31
POSE_NODE_IDS = frozenset(range(80, 86))
FINGER_BY_NODE = {
    81: 'thumb',
    82: 'index',
    83: 'middle',
    84: 'ring',
    85: 'little',
}
FINGER_ORDER = ('thumb', 'index', 'middle', 'ring', 'little')


def modbus_crc(data: bytes) -> int:
    '''CRC16-Modbus（多项式 0xA001），用于姿态帧校验与标定命令。'''
    crc = 65535
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = crc >> 1 ^ 40961 if crc & 1 else crc >> 1
    return crc


def _u16_le(data: bytes, offset: int) -> int:
    return data[offset] | data[offset + 1] << 8


def _f32_le_six(data: bytes, offset: int) -> tuple[float, float, float, float, float, float]:
    return struct.unpack_from('<6f', data, offset)


class ProcessedPoseAssembler:
    '''从字节流切出姿态结果帧，按 sequence 拼成整手网页帧。'''

    def __init__(self, max_pending: int = 8) -> None:
        self._buffer = bytearray()
        self._pending = {}
        self._max_pending = max_pending
        self.skipped = 0
        self.bad_crc = 0
        self.node_count = 0
        self.output_frame_count = 0
        self.phase = 'idle'
        self.message = '未启动'

    def reset(self) -> None:
        self._buffer.clear()
        self._pending.clear()
        self.skipped = 0
        self.bad_crc = 0
        self.node_count = 0
        self.output_frame_count = 0
        self.phase = 'connecting'
        self.message = '等待主控姿态结果帧（AA 55）…'

    def push(self, data: bytes) -> list[dict[str, Any]]:
        frames = []
        self._buffer.extend(data)
        while True:
            node = self._pop_one_node()
            if node is None:
                break
            self.node_count += 1
            assembled = self._ingest_node(node)
            if assembled is not None:
                frames.append(assembled)
        return frames

    def _pop_one_node(self) -> dict[str, Any] | None:
        buf = self._buffer
        while len(buf) >= 2:
            if buf[0] == POSE_SYNC0 and buf[1] == POSE_SYNC1:
                break
            del buf[0]
            self.skipped += 1
        if len(buf) < POSE_FRAME_SIZE:
            return None
        if not (buf[0] == POSE_SYNC0 and buf[1] == POSE_SYNC1):
            del buf[0]
            self.skipped += 1
            return None
        frame = bytes(buf[:POSE_FRAME_SIZE])
        node_id = frame[2]
        if node_id not in POSE_NODE_IDS:
            del buf[0]
            self.skipped += 1
            return None
        received = _u16_le(frame, POSE_FRAME_SIZE - 2)
        calculated = modbus_crc(frame[:POSE_FRAME_SIZE - 2])
        if received != calculated:
            self.bad_crc += 1
            del buf[0]
            self.skipped += 1
            return None
        del buf[:POSE_FRAME_SIZE]
        sequence = _u16_le(frame, 3)
        floats = _f32_le_six(frame, 5)
        return {
            'node_id': node_id,
            'sequence': sequence,
            'floats': floats,
            'crc_ok': True,
        }

    def _ingest_node(self, node: dict[str, Any]) -> dict[str, Any] | None:
        sequence = int(node['sequence'])
        node_id = int(node['node_id'])
        bucket = self._pending.setdefault(sequence, {})
        bucket[node_id] = node
        if len(self._pending) > self._max_pending:
            for key in sorted(self._pending.keys())[:-self._max_pending]:
                if key != sequence:
                    del self._pending[key]
        if not POSE_NODE_IDS.issubset(bucket.keys()):
            self.phase = 'live' if self.output_frame_count else 'connecting'
            missing = sorted(POSE_NODE_IDS - bucket.keys())
            if self.output_frame_count == 0:
                self.message = f'已收 sequence={sequence}，缺节点 {[hex(x) for x in missing]}'
            else:
                self.message = f'实时接收中 · 最近 sequence={sequence}'
            return None
        frame = self._to_viewer_frame(bucket)
        del self._pending[sequence]
        self.output_frame_count += 1
        self.phase = 'live'
        self.message = f'主控处理数据实时输出 · sequence={sequence}'
        return frame

    def _to_viewer_frame(self, bucket: dict[int, dict[str, Any]]) -> dict[str, Any]:
        palm = bucket[80]['floats']
        fingers = {}
        for node_id, name in FINGER_BY_NODE.items():
            values = bucket[node_id]['floats']
            bend_deg = float(values[4])
            sway_deg = float(values[5])
            fingers[name] = {
                'palm_relative_quaternion_wxyz': [float(values[0]), float(values[1]), float(values[2]), float(values[3])],
                'bend_rad': math.radians(bend_deg),
                'sway_rad': math.radians(sway_deg),
                'bend_deg': bend_deg,
                'sway_deg': sway_deg,
            }
        return {
            'time_s': float(palm[4]),
            'wrist_quaternion_wxyz': [float(palm[0]), float(palm[1]), float(palm[2]), float(palm[3])],
            'fingers': fingers,
            'sequence': int(bucket[80]['sequence']),
        }
