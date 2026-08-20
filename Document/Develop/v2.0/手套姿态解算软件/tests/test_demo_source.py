'''模拟数据源测试：协议布局、CRC、经真实解析器逐值一致。'''
from __future__ import annotations

import struct

from app.demo_source import DEMO_PHASES, demo_frame_bytes, finger_angles, wrist_quaternion_wxyz
from processed_pipeline import ProcessedPoseAssembler, modbus_crc


def test_frame_layout_and_crc():
    data = demo_frame_bytes(1.25, 7)
    assert len(data) == 31 * 6
    for i in range(6):
        frame = data[i * 31:(i + 1) * 31]
        assert frame[0] == 0xAA and frame[1] == 0x55
        assert frame[2] in (0x50, 0x51, 0x52, 0x53, 0x54, 0x55)
        assert struct.unpack_from('<H', frame, 3)[0] == 7
        assert modbus_crc(frame[:29]) == struct.unpack_from('<H', frame, 29)[0]


def test_parser_roundtrip_matches_script():
    '''经 ProcessedPoseAssembler 解析后，帧字段与脚本公式在 t 处逐值一致（<1e-6）。'''
    t = 2.0
    assembler = ProcessedPoseAssembler()
    frames = assembler.push(demo_frame_bytes(t, 3))
    assert len(frames) == 1
    frame = frames[0]
    assert frame['sequence'] == 3
    assert frame['time_s'] == t  # demo 帧 time_s 由脚本写入
    wrist = wrist_quaternion_wxyz(t)
    for got, expected in zip(frame['wrist_quaternion_wxyz'], wrist):
        assert abs(got - expected) < 1e-5  # 协议线载为 float32，量化误差 ~5e-6@85°
    for finger in ('thumb', 'index', 'middle', 'ring', 'little'):
        quat, bend, sway = finger_angles(t, finger)
        f = frame['fingers'][finger]
        assert abs(f['bend_deg'] - bend) < 1e-5
        assert abs(f['sway_deg'] - sway) < 1e-5
        for got, expected in zip(f['palm_relative_quaternion_wxyz'], quat):
            assert abs(got - expected) < 1e-5


def test_sequence_increments_and_multi_frame_stream():
    assembler = ProcessedPoseAssembler()
    frames = assembler.push(demo_frame_bytes(0.1, 0) + demo_frame_bytes(0.2, 1))
    assert len(frames) == 2
    assert frames[0]['sequence'] == 0
    assert frames[1]['sequence'] == 1


def test_deterministic_and_phases():
    assert demo_frame_bytes(1.0, 0) == demo_frame_bytes(1.0, 0)
    assert DEMO_PHASES == {'thumb': 0.0, 'index': 0.7, 'middle': 1.4, 'ring': 2.1, 'little': 2.8}
    # 满握拳范围检查：bend 值域 [0, FULL_FIST]
    from app.hand_pose import FULL_FIST_BEND_DEG
    for finger in DEMO_PHASES:
        for t in (0.0, 0.5, 1.0, 2.0, 3.0):
            _, bend, sway = finger_angles(t, finger)
            assert 0.0 <= bend <= FULL_FIST_BEND_DEG[finger]
            assert -20.0 <= sway <= 20.0
