'''模拟数据源：合成 AA55 姿态字节流（契约 6.4，走真实协议路径）。

确定性脚本（t 为相对起始秒）：
- 腕：绕 +X 转 25°·sin(0.5t)；
- 每指 bend = FULL_FIST[f] × (0.5 + 0.5·sin(0.8t + φ))，φ = 0/0.7/1.4/2.1/2.8；
- 每指 sway = 20·sin(0.3t + φ + 1.1)；
- 手指相对四元数：绕 +X 转 bend_rad。
'''
from __future__ import annotations

import math
import struct

from app.hand_pose import FULL_FIST_BEND_DEG
from processed_pipeline import FINGER_BY_NODE, modbus_crc

DEMO_HZ = 200.0
DEMO_WRIST_SWING_DEG = 25.0
DEMO_SWAY_DEG = 20.0
DEMO_PHASES = {'thumb': 0.0, 'index': 0.7, 'middle': 1.4, 'ring': 2.1, 'little': 2.8}


def wrist_quaternion_wxyz(t: float) -> tuple:
    angle = math.radians(DEMO_WRIST_SWING_DEG * math.sin(0.5 * t))
    return (math.cos(angle / 2.0), math.sin(angle / 2.0), 0.0, 0.0)


def finger_angles(t: float, finger: str) -> tuple:
    '''→ (quat_wxyz, bend_deg, sway_deg)。'''
    phase = DEMO_PHASES[finger]
    bend_deg = FULL_FIST_BEND_DEG[finger] * (0.5 + 0.5 * math.sin(0.8 * t + phase))
    sway_deg = DEMO_SWAY_DEG * math.sin(0.3 * t + phase + 1.1)
    bend_rad = math.radians(bend_deg)
    quat = (math.cos(bend_rad / 2.0), math.sin(bend_rad / 2.0), 0.0, 0.0)
    return quat, bend_deg, sway_deg


def demo_frame_bytes(now_s: float, sequence: int) -> bytes:
    '''六节点 31×6 字节（腕 0x50 + 五指 0x51–0x55，同 sequence，CRC16-Modbus）。'''
    out = bytearray()

    def node(node_id: int, floats) -> None:
        body = bytes((0xAA, 0x55, node_id)) + struct.pack('<H', sequence) + struct.pack('<6f', *floats)
        out.extend(body)
        out.extend(struct.pack('<H', modbus_crc(body)))

    wrist = wrist_quaternion_wxyz(now_s)
    node(0x50, (wrist[0], wrist[1], wrist[2], wrist[3], now_s, 0.0))
    for node_id, finger in FINGER_BY_NODE.items():
        quat, bend_deg, sway_deg = finger_angles(now_s, finger)
        node(node_id, (quat[0], quat[1], quat[2], quat[3], bend_deg, sway_deg))
    return bytes(out)
