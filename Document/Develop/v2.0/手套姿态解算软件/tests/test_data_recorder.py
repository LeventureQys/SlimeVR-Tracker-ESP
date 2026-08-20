'''DataRecorder 单元测试：状态机、CSV 输出、计时、文件夹区分。'''
from __future__ import annotations

import csv
import time

import pytest

from app.data_recorder import CSV_FIELDS, DataRecorder
from app.recorder_panel import format_elapsed


def _sample_frame(sequence: int = 7) -> dict:
    return {
        'time_s': 1.2345,
        'sequence': sequence,
        'wrist_quaternion_wxyz': [0.98, 0.0, 0.0, 0.19],
        'fingers': {
            'thumb': {'bend_deg': 12.3, 'sway_deg': -4.5,
                      'palm_relative_quaternion_wxyz': [0.9, 0.1, 0.2, 0.3]},
            'index': {'bend_deg': 45.6, 'sway_deg': 7.8,
                      'palm_relative_quaternion_wxyz': [0.8, 0.1, 0.1, 0.5]},
            'middle': {'bend_deg': 50.0, 'sway_deg': 1.0,
                       'palm_relative_quaternion_wxyz': [0.8, 0.2, 0.1, 0.5]},
            'ring': {'bend_deg': 40.0, 'sway_deg': -2.0,
                     'palm_relative_quaternion_wxyz': [0.8, 0.3, 0.1, 0.5]},
            'little': {'bend_deg': 30.0, 'sway_deg': 3.0,
                       'palm_relative_quaternion_wxyz': [0.8, 0.4, 0.1, 0.5]},
        },
    }


class TestRecorderStateMachine:
    def test_initial_idle(self, tmp_path):
        recorder = DataRecorder(tmp_path)
        status = recorder.status()
        assert status.state == 'idle'
        assert status.frame_count == 0
        assert status.raw_bytes == 0
        assert status.elapsed_ms == 0

    def test_start_creates_folder_csv_and_raw(self, tmp_path):
        recorder = DataRecorder(tmp_path)
        folder = recorder.start()
        assert recorder.state == 'recording'
        # 文件夹名包含时间戳前缀
        import re
        assert re.search(r'rec_\d{8}_\d{6}$', folder.split('/')[-1].split('\\')[-1])
        actual = list((tmp_path).glob('rec_*/imu_data.csv'))
        assert len(actual) == 1
        raw = list((tmp_path).glob('rec_*/raw_stream.bin'))
        assert len(raw) == 1

    def test_idle_ignores_write(self, tmp_path):
        recorder = DataRecorder(tmp_path)
        recorder.write_frame(_sample_frame())
        recorder.write_raw_bytes(b'\xaa\x55')
        status = recorder.status()
        assert status.frame_count == 0
        assert status.raw_bytes == 0

    def test_record_pause_resume_stop(self, tmp_path):
        recorder = DataRecorder(tmp_path)
        recorder.start()
        recorder.write_frame(_sample_frame(1))
        recorder.write_raw_bytes(b'\xaa\x55\x00')
        recorder.write_frame(_sample_frame(2))
        assert recorder.status().frame_count == 2
        assert recorder.status().raw_bytes == 3

        recorder.pause()
        assert recorder.state == 'paused'
        recorder.write_frame(_sample_frame(3))  # 暂停期不写入
        recorder.write_raw_bytes(b'\x00')
        assert recorder.status().frame_count == 2
        assert recorder.status().raw_bytes == 3

        recorder.resume()
        assert recorder.state == 'recording'
        recorder.write_frame(_sample_frame(4))
        assert recorder.status().frame_count == 3

        folder = recorder.stop()
        assert recorder.state == 'idle'
        assert folder
        import os
        assert os.path.isdir(folder)

    def test_stop_writes_session_info(self, tmp_path):
        recorder = DataRecorder(tmp_path)
        recorder.start()
        recorder.write_frame(_sample_frame())
        recorder.write_raw_bytes(b'\xaa\x55\x01\x02')
        folder = recorder.stop({'bad_crc': 3, 'skipped': 12, 'sample_count': 99})
        import json
        import os
        info = json.load(open(os.path.join(folder, 'session_info.json'), encoding='utf-8'))
        assert info['bad_crc'] == 3
        assert info['skipped'] == 12
        assert info['sample_count'] == 99
        assert info['recorded_frames'] == 1
        assert info['recorded_raw_bytes'] == 4
        assert info['recorded_elapsed_ms'] >= 0

    def test_raw_bytes_persisted_to_bin(self, tmp_path):
        recorder = DataRecorder(tmp_path)
        folder = recorder.start()
        payload = bytes(range(64))
        recorder.write_raw_bytes(payload)
        recorder.stop()
        import os
        data = open(os.path.join(folder, 'raw_stream.bin'), 'rb').read()
        assert data == payload

    def test_elapsed_excludes_pause(self, tmp_path):
        recorder = DataRecorder(tmp_path)
        recorder.start()
        time.sleep(0.05)
        recorder.pause()
        elapsed_at_pause = recorder.status().elapsed_ms
        time.sleep(0.1)
        assert recorder.status().elapsed_ms == elapsed_at_pause
        recorder.resume()
        time.sleep(0.05)
        assert recorder.status().elapsed_ms > elapsed_at_pause

    def test_second_start_fails_while_recording(self, tmp_path):
        recorder = DataRecorder(tmp_path)
        recorder.start()
        with pytest.raises(RuntimeError):
            recorder.start()
        recorder.stop()


class TestRecorderCsv:
    def test_header_and_row(self, tmp_path):
        recorder = DataRecorder(tmp_path)
        folder = recorder.start()
        recorder.write_frame(_sample_frame())
        recorder.stop()
        import os
        rows = list(csv.reader(open(os.path.join(folder, 'imu_data.csv'), encoding='utf-8')))
        assert rows[0] == list(CSV_FIELDS)
        assert len(rows) == 2
        header = dict(zip(rows[0], rows[1]))
        assert header['sequence'] == '7'
        assert header['thumb_bend_deg'] == '12.300000'
        assert header['wrist_qw'] == '0.980000'

    def test_missing_fields_written_empty(self, tmp_path):
        recorder = DataRecorder(tmp_path)
        folder = recorder.start()
        recorder.write_frame({'time_s': 0.5})
        recorder.stop()
        import os
        rows = list(csv.reader(open(os.path.join(folder, 'imu_data.csv'), encoding='utf-8')))
        assert len(rows) == 2
        assert rows[1][rows[0].index('sequence')] == ''
        assert rows[1][rows[0].index('thumb_bend_deg')] == ''


class TestFormatElapsed:
    def test_basic(self):
        assert format_elapsed(0) == '00:00'
        assert format_elapsed(59999) == '00:59'
        assert format_elapsed(61000) == '01:01'
        assert format_elapsed(600000) == '10:00'
