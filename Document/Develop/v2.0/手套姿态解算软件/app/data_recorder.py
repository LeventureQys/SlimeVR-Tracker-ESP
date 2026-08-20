'''IMU 数据录制：串口原始字节流 + 解析后帧 CSV（开始/暂停/继续/停止状态机 + 计时）。

契约：每次开始录制创建一个时间戳文件夹 recordings/rec_YYYYMMDD_HHMMSS/，
- raw_stream.bin：串口收到的原始字节流 1:1 落盘（含被解析层丢弃的坏帧，用于链路与漂移诊断）；
- imu_data.csv：解析后的全部字段（腕四元数、五指四元数、bend/sway 度数与弧度、sequence、time_s），一行一帧；
- session_info.json：结束时的协议统计（坏帧/跳过/样本数等）。
暂停期间三者均不写入。
'''
from __future__ import annotations

import csv
import json
import threading
import time
from dataclasses import dataclass
from pathlib import Path

FINGER_ORDER = ('thumb', 'index', 'middle', 'ring', 'little')

CSV_FIELDS = (
    'timestamp_ms',
    'time_s',
    'sequence',
    'wrist_qw', 'wrist_qx', 'wrist_qy', 'wrist_qz',
) + tuple(
    f'{finger}_{field}'
    for finger in FINGER_ORDER
    for field in ('bend_deg', 'sway_deg', 'qw', 'qx', 'qy', 'qz')
)


@dataclass
class RecorderStatus:
    state: str        # idle / recording / paused
    folder: str       # 当前录制文件夹（idle 时为空）
    frame_count: int  # 已写入帧数
    raw_bytes: int    # 已写入原始字节数
    elapsed_ms: int   # 实际录制时长（不含暂停）


class DataRecorder:
    '''串口原始流 + 解析帧录制器。write_raw_bytes 供串口线程调用（线程安全）。'''

    def __init__(self, base_dir: str | Path = 'recordings') -> None:
        self._base_dir = Path(base_dir)
        self._lock = threading.Lock()
        self._state = 'idle'
        self._folder = None
        self._file = None
        self._writer = None
        self._raw_file = None
        self._frame_count = 0
        self._raw_bytes = 0
        self._accumulated_s = 0.0
        self._segment_start = None  # perf_counter，本段录制开始时刻

    # ------------------------------------------------------------------ 状态机

    def start(self) -> str:
        '''开始新录制：创建时间戳文件夹并打开 CSV 与原始流。返回文件夹路径。'''
        if self._state != 'idle':
            raise RuntimeError('已有录制在进行中')
        self._base_dir.mkdir(parents=True, exist_ok=True)
        folder = self._base_dir / time.strftime('rec_%Y%m%d_%H%M%S')
        folder.mkdir(parents=True, exist_ok=False)
        csv_path = folder / 'imu_data.csv'
        self._file = open(csv_path, 'w', newline='', encoding='utf-8')
        self._writer = csv.DictWriter(self._file, fieldnames=CSV_FIELDS)
        self._writer.writeheader()
        self._file.flush()
        self._raw_file = open(folder / 'raw_stream.bin', 'wb')
        self._folder = folder
        self._frame_count = 0
        self._raw_bytes = 0
        self._accumulated_s = 0.0
        self._segment_start = time.perf_counter()
        self._state = 'recording'
        return str(folder)

    def pause(self) -> None:
        with self._lock:
            if self._state != 'recording':
                return
            self._accumulated_s += time.perf_counter() - self._segment_start
            self._segment_start = None
            self._state = 'paused'

    def resume(self) -> None:
        with self._lock:
            if self._state != 'paused':
                return
            self._segment_start = time.perf_counter()
            self._state = 'recording'

    def stop(self, stats: dict | None = None) -> str:
        '''结束录制：写 session_info.json、关文件，返回文件夹路径（idle 返回空串）。'''
        with self._lock:
            if self._state == 'recording':
                self._accumulated_s += time.perf_counter() - self._segment_start
            self._segment_start = None
            if self._file is not None:
                self._file.flush()
                self._file.close()
                self._file = None
            self._writer = None
            if self._raw_file is not None:
                self._raw_file.flush()
                self._raw_file.close()
                self._raw_file = None
            folder = self._folder
            if folder is not None and stats is not None:
                payload = dict(stats)
                payload.setdefault('recorded_frames', self._frame_count)
                payload.setdefault('recorded_raw_bytes', self._raw_bytes)
                payload.setdefault('recorded_elapsed_ms',
                                   int(self._accumulated_s * 1000.0))
                (folder / 'session_info.json').write_text(
                    json.dumps(payload, ensure_ascii=False, indent=2), encoding='utf-8')
            self._folder = None
            self._state = 'idle'
            return str(folder) if folder is not None else ''

    # ------------------------------------------------------------------ 数据

    def write_raw_bytes(self, data: bytes) -> None:
        '''串口原始字节流落盘（串口线程调用）。仅 recording 状态写入。'''
        with self._lock:
            if self._state != 'recording' or self._raw_file is None:
                return
            self._raw_file.write(data)
            self._raw_bytes += len(data)

    def write_frame(self, frame: dict) -> None:
        '''写入一帧解析后数据。仅 recording 状态写入；字段缺失按空处理。'''
        with self._lock:
            if self._state != 'recording' or self._writer is None:
                return
            row = {'timestamp_ms': int(time.time() * 1000.0)}
            row['time_s'] = _fmt(frame.get('time_s'))
            row['sequence'] = frame.get('sequence', '')
            wrist = frame.get('wrist_quaternion_wxyz') or []
            for i, key in enumerate(('wrist_qw', 'wrist_qx', 'wrist_qy', 'wrist_qz')):
                row[key] = _fmt(wrist[i]) if i < len(wrist) else ''
            fingers = frame.get('fingers') or {}
            for finger in FINGER_ORDER:
                entry = fingers.get(finger) or {}
                row[f'{finger}_bend_deg'] = _fmt(entry.get('bend_deg'))
                row[f'{finger}_sway_deg'] = _fmt(entry.get('sway_deg'))
                quat = entry.get('palm_relative_quaternion_wxyz') or []
                for i, key in enumerate(('qw', 'qx', 'qy', 'qz')):
                    row[f'{finger}_{key}'] = _fmt(quat[i]) if i < len(quat) else ''
            self._writer.writerow(row)
            self._frame_count += 1

    # ------------------------------------------------------------------ 查询

    def status(self) -> RecorderStatus:
        with self._lock:
            elapsed = self._accumulated_s
            if self._state == 'recording' and self._segment_start is not None:
                elapsed += time.perf_counter() - self._segment_start
            return RecorderStatus(
                state=self._state,
                folder=str(self._folder) if self._folder is not None else '',
                frame_count=self._frame_count,
                raw_bytes=self._raw_bytes,
                elapsed_ms=int(elapsed * 1000.0),
            )

    @property
    def state(self) -> str:
        with self._lock:
            return self._state


def _fmt(value) -> str:
    if value is None:
        return ''
    if isinstance(value, float):
        return f'{value:.6f}'
    if isinstance(value, int):
        return str(value)
    return str(value)
