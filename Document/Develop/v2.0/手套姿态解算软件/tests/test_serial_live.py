'''串口会话测试：合成字节流、坏 CRC、标定命令与窗口、stop 幂等。'''
from __future__ import annotations

import struct
import time

import pytest

from conftest import make_node_bytes, make_pose_bytes
from tools.serial_live import (
    CALIBRATE_STALE_S,
    HOST_CMD_RECALIBRATE_PREFIX,
    SerialLiveSession,
    build_recalibrate_command,
)


@pytest.fixture()
def session():
    return SerialLiveSession()


def test_assembles_full_frame(session):
    '''六节点完整流 → 恰 1 帧且字段正确。'''
    session._process_chunk(make_pose_bytes(
        sequence=7,
        wrist=(0.5, 0.5, 0.5, 0.5),
        time_s=1.25,
        fingers={
            'thumb': {'bend_deg': 10.0, 'sway_deg': 20.0, 'quat': (1.0, 0.0, 0.0, 0.0)},
        },
    ))
    frames = session.drain_frames()
    assert len(frames) == 1
    frame = frames[0]
    assert frame['sequence'] == 7
    assert frame['time_s'] == pytest.approx(1.25)
    assert frame['wrist_quaternion_wxyz'] == pytest.approx((0.5, 0.5, 0.5, 0.5))
    thumb = frame['fingers']['thumb']
    assert thumb['bend_deg'] == pytest.approx(10.0)
    assert thumb['sway_deg'] == pytest.approx(20.0)
    st = session.status_dict()
    assert st['phase'] == 'live'
    assert st['output_frame_count'] == 1


def test_bad_crc_skipped(session):
    good = make_pose_bytes(sequence=1, wrist=(1.0, 0.0, 0.0, 0.0))
    bad = bytearray(make_node_bytes(0x50, 1, (1.0, 0.0, 0.0, 0.0, 0.0, 0.0)))
    bad[-1] ^= 0xFF  # 破坏 CRC
    session._process_chunk(bytes(bad))
    assert session.drain_frames() == []
    assert session._pose_assembler.bad_crc == 1
    # 修好 CRC 后同序列补齐其余节点仍可出帧（按 sequence 聚合）
    session._process_chunk(good[31:])  # 只送 0x51–0x55 五节点
    assert session.drain_frames() == []


def test_partial_sequence_no_frame(session):
    '''缺节点不出帧，但计数与提示正确。'''
    chunk = make_pose_bytes(sequence=3)
    session._process_chunk(chunk[:31 * 3])  # 只送 3 个节点
    assert session.drain_frames() == []
    st = session.status_dict()
    assert st['output_frame_count'] == 0
    assert st['phase'] in ('connecting', 'idle')
    # 补齐剩余节点 → 出帧
    session._process_chunk(chunk[31 * 3:])
    assert len(session.drain_frames()) == 1


def test_recalibrate_command_bytes():
    cmd = build_recalibrate_command()
    assert cmd[:6] == HOST_CMD_RECALIBRATE_PREFIX
    assert len(cmd) == 8
    from processed_pipeline import modbus_crc
    assert cmd[6] | (cmd[7] << 8) == modbus_crc(cmd[:6])


def test_request_calibrate_without_serial_raises(session):
    with pytest.raises(RuntimeError, match='串口未连接'):
        session.request_calibrate()


def test_calibrate_window_drops_frames(session):
    chunk = make_pose_bytes(sequence=9)
    with session._lock:
        session._host_calibrating = True
        session._calibrate_until = time.perf_counter() + CALIBRATE_STALE_S
    session._process_chunk(chunk)
    assert session.drain_frames() == []  # 窗口内丢弃
    with session._lock:
        session._calibrate_until = time.perf_counter() - 0.01  # 窗口过期
    session._process_chunk(chunk)
    assert len(session.drain_frames()) == 1  # 恢复接收


def test_stop_idempotent(session):
    session.stop()
    session.stop()
    assert not session.running
    assert session.status_dict()['phase'] == 'idle'
    assert session.latest_frame() is None


def test_queue_drops_oldest_when_full(session):
    for seq in range(12):
        session._process_chunk(make_pose_bytes(sequence=seq))
    frames = session.drain_frames()
    assert len(frames) <= 8  # 队列容量 8，最旧被丢弃
    assert frames[-1]['sequence'] == 11


def test_raw_sink_receives_all_bytes(session):
    '''原始字节流回调应收到与输入完全一致的字节（包括坏帧），清除后不再回调。'''
    captured = []
    session.set_raw_sink(captured.append)
    chunk = make_pose_bytes(sequence=5)
    bad = bytearray(make_node_bytes(0x50, 1, (1.0, 0.0, 0.0, 0.0, 0.0, 0.0)))
    bad[-1] ^= 0xFF  # 坏 CRC 也必须出现在原始流中
    session._process_chunk(bytes(bad))
    session._process_chunk(chunk)
    assert b''.join(captured) == bytes(bad) + chunk
    session.set_raw_sink(None)
    session._process_chunk(chunk)
    assert b''.join(captured) == bytes(bad) + chunk  # 清除后无新增


def test_status_dict_has_crc_stats(session):
    st = session.status_dict()
    assert 'bad_crc' in st and 'skipped' in st
    bad = bytearray(make_node_bytes(0x50, 1, (1.0, 0.0, 0.0, 0.0, 0.0, 0.0)))
    bad[-1] ^= 0xFF
    session._process_chunk(bytes(bad))
    st = session.status_dict()
    assert st['bad_crc'] == 1
