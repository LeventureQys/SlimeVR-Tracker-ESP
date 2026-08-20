'''CRC16-Modbus 回归：已知向量 + 独立参考实现交叉验证。'''
from __future__ import annotations

import random

from conftest import make_node_bytes
from processed_pipeline import modbus_crc


def reference_modbus_crc(data: bytes) -> int:
    '''独立参考实现（按位、多项式 0xA001、初值 0xFFFF）。'''
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def test_known_modbus_vector():
    # Modbus 协议标准示例：01 03 00 00 00 01 → CRC 0x0A84（LE 字节序 84 0A）
    assert modbus_crc(bytes((0x01, 0x03, 0x00, 0x00, 0x00, 0x01))) == 0x0A84


def test_reference_implementation_agreement():
    rng = random.Random(20260817)
    for _ in range(200):
        data = bytes(rng.randrange(256) for _ in range(rng.randrange(0, 64)))
        assert modbus_crc(data) == reference_modbus_crc(data)


def test_aa55_frame_crc_roundtrip():
    frame = make_node_bytes(0x53, 42, (0.1, 0.2, 0.3, 0.4, 15.0, -3.0))
    assert modbus_crc(frame[:29]) == frame[29] | (frame[30] << 8)
