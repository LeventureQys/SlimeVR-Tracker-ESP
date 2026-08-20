'''面板纯函数（复刻 viewer.js 21–22、117–150、248–271 行语义），可 headless 单测。'''
from __future__ import annotations

LATERAL_DISPLAY_SIGN = {'thumb': 1, 'index': -1, 'middle': 1, 'ring': -1, 'little': 1}

PHASE_BADGE_TEXT = {
    'idle': '未连接',
    'connecting': '连接中',
    'calibrating': '标定中',
    'live': '处理实时',
    'error': '错误',
}


def display_sway_deg(finger: str, sway_deg: float) -> float:
    '''食指/无名指左右对调（显示层符号翻转），弯曲不改。'''
    return float(sway_deg) * (LATERAL_DISPLAY_SIGN.get(finger, 1))


def phase_badge_text(phase: str) -> str:
    return PHASE_BADGE_TEXT.get(phase, phase)


def status_line_text(status: dict) -> str:
    '''`{message} · 样本 {n} · 输出帧 {m}` + （有端口时）` · {port}@{baud}`。'''
    message = status.get('message') or ''
    parts = [
        message,
        f'样本 {status.get("sample_count", 0)}',
        f'输出帧 {status.get("output_frame_count", 0)}',
    ]
    text = ' · '.join(parts)
    if status.get('port'):
        text += f' · {status["port"]}@{status.get("baud")}'
    return text


def frame_summary(frame: dict, index: int, frames: list) -> dict:
    '''renderFrame 摘要：相对时间/采样间隔/最大弯曲/最大侧摆/帧计数。'''
    relative_time = float(frame['time_s']) - float(frames[0]['time_s'])
    interval_ms = None
    if index > 0:
        interval_ms = (float(frame['time_s']) - float(frames[index - 1]['time_s'])) * 1000.0
    max_bend = 0.0
    max_sway = 0.0
    for name in ('thumb', 'index', 'middle', 'ring', 'little'):
        finger = frame.get('fingers', {}).get(name) or {}
        bend = float(finger.get('bend_deg', 0.0) or 0.0)
        sway = display_sway_deg(name, float(finger.get('sway_deg', 0.0) or 0.0))
        max_bend = max(max_bend, bend)
        max_sway = max(max_sway, abs(sway))
    return {
        'relative_time_s': relative_time,
        'interval_ms': interval_ms,
        'max_bend_deg': max_bend,
        'max_sway_deg': max_sway,
        'counter_text': f'{index + 1} / {len(frames)}',
    }


def clamp_chart_value(value: float, lo: float = -30.0, hi: float = 90.0) -> float:
    return max(lo, min(hi, value))
