'''面板纯函数测试：符号翻转、相位文案、状态行、摘要、钳制。'''
from __future__ import annotations

import pytest

from app.panel_utils import (
    clamp_chart_value,
    display_sway_deg,
    frame_summary,
    phase_badge_text,
    status_line_text,
)


def test_sway_sign_flips():
    assert display_sway_deg('index', 5.0) == -5.0
    assert display_sway_deg('ring', -3.0) == 3.0
    assert display_sway_deg('thumb', 5.0) == 5.0
    assert display_sway_deg('middle', -2.5) == -2.5
    assert display_sway_deg('little', 4.0) == 4.0


def test_phase_badge_text():
    assert phase_badge_text('idle') == '未连接'
    assert phase_badge_text('connecting') == '连接中'
    assert phase_badge_text('calibrating') == '标定中'
    assert phase_badge_text('live') == '处理实时'
    assert phase_badge_text('error') == '错误'
    assert phase_badge_text('unknown') == 'unknown'


def test_status_line_text_with_and_without_port():
    status = {'phase': 'live', 'message': '主控处理数据实时输出 · sequence=3',
              'sample_count': 42, 'output_frame_count': 7, 'port': 'COM5', 'baud': 921600}
    text = status_line_text(status)
    assert '主控处理数据实时输出' in text
    assert '样本 42' in text and '输出帧 7' in text
    assert 'COM5@921600' in text
    no_port = dict(status)
    no_port['port'] = None
    text2 = status_line_text(no_port)
    assert '@' not in text2


def test_frame_summary_values():
    frames = [
        {'time_s': 10.0, 'fingers': {f: {'bend_deg': 0.0, 'sway_deg': 0.0}
                                     for f in ('thumb', 'index', 'middle', 'ring', 'little')}},
        {'time_s': 10.02, 'fingers': {
            'thumb': {'bend_deg': 30.0, 'sway_deg': 10.0},
            'index': {'bend_deg': 40.0, 'sway_deg': 5.0},
            'middle': {'bend_deg': 0.0, 'sway_deg': 0.0},
            'ring': {'bend_deg': 20.0, 'sway_deg': -8.0},
            'little': {'bend_deg': 10.0, 'sway_deg': 0.0},
        }},
    ]
    summary = frame_summary(frames[1], 1, frames)
    assert summary['relative_time_s'] == pytest.approx(0.02)
    assert summary['interval_ms'] == pytest.approx(20.0)
    assert summary['max_bend_deg'] == 40.0
    # sway 显示符号：index +5→-5，ring -8→+8；最大 = thumb 10
    assert summary['max_sway_deg'] == 10.0
    assert summary['counter_text'] == '2 / 2'
    first = frame_summary(frames[0], 0, frames)
    assert first['interval_ms'] is None
    assert first['counter_text'] == '1 / 2'


def test_clamp_chart_value():
    assert clamp_chart_value(150.0) == 90.0
    assert clamp_chart_value(-100.0) == -30.0
    assert clamp_chart_value(45.0) == 45.0
