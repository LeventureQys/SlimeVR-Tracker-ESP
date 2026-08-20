'''pytest 公共配置：sys.path、共享 fixtures、合成帧工具。'''
from __future__ import annotations

import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
for _p in (str(ROOT), str(ROOT / 'tools')):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from processed_pipeline import modbus_crc  # noqa: E402

FINGER_NODE_IDS = {'thumb': 0x51, 'index': 0x52, 'middle': 0x53, 'ring': 0x54, 'little': 0x55}


def make_node_bytes(node_id: int, sequence: int, floats) -> bytes:
    '''单节点 31 字节 AA55 帧（含 CRC16-Modbus）。'''
    body = bytes((0xAA, 0x55, node_id)) + struct.pack('<H', sequence) + struct.pack('<6f', *floats)
    return body + struct.pack('<H', modbus_crc(body))


def make_pose_bytes(sequence: int = 0, wrist=(1.0, 0.0, 0.0, 0.0), time_s: float = 0.0,
                    fingers: dict | None = None) -> bytes:
    '''六节点完整姿态字节流（腕 + 五指）。'''
    out = make_node_bytes(0x50, sequence, (wrist[0], wrist[1], wrist[2], wrist[3], time_s, 0.0))
    fingers = fingers or {}
    for name, node_id in FINGER_NODE_IDS.items():
        spec = fingers.get(name, {})
        quat = spec.get('quat', (1.0, 0.0, 0.0, 0.0))
        bend = float(spec.get('bend_deg', 0.0))
        sway = float(spec.get('sway_deg', 0.0))
        out += make_node_bytes(node_id, sequence, (quat[0], quat[1], quat[2], quat[3], bend, sway))
    return out


@pytest.fixture(scope='session')
def asset():
    from app.gltf.loader import GltfLoader
    return GltfLoader(ROOT / 'app' / 'assets' / 'generic-hand-left.glb').load()


@pytest.fixture(scope='session')
def pose_model(asset):
    from app.hand_pose import HandPoseModel
    return HandPoseModel(asset)
