'''深色主题 QSS 与调色板（复刻 web/style.css 颜色表，设计文档 7 节）。'''
from __future__ import annotations

PALETTE = {
    'bg': '#070b12',
    'panel': '#0c121c',
    'panel_soft': '#111925',
    'line': 'rgba(159, 183, 210, 0.15)',
    'line_strong': 'rgba(159, 183, 210, 0.28)',
    'text': '#edf3f8',
    'muted': '#8292a5',
    'bend': '#49d9d0',
    'sway': '#ffbd66',
    'accent': '#72a7ff',
    'danger': '#ff6c7b',
    'axis_x': '#ff7685',
    'axis_y': '#67dda0',
    'axis_z': '#73aaff',
    'mono': '"Cascadia Mono", "Consolas", "Courier New", monospace',
    'font': '"Segoe UI", "Microsoft YaHei", system-ui, sans-serif',
}

GLOBAL_QSS = f'''
* {{
    font-family: {PALETTE['font']};
    font-size: 12px;
    color: {PALETTE['text']};
}}
QMainWindow, QWidget#centralRoot {{
    background: {PALETTE['bg']};
}}
QWidget#controlPanel {{
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #0d141e, stop:1 #0a1018);
    border-left: 1px solid {PALETTE['line']};
}}
QWidget#viewportRoot {{
    background: qradialgradient(cx:0.48, cy:0.40, radius:0.55,
        stop:0 rgba(72, 104, 145, 0.22), stop:0.38 rgba(72, 104, 145, 0.05),
        stop:1 transparent),
        qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #080d15, stop:0.55 #0b111a, stop:1 #070a10);
}}
QLabel {{
    background: transparent;
    color: {PALETTE['text']};
}}
QLabel[kicker="true"] {{
    color: {PALETTE['bend']};
    font-size: 10px;
    font-weight: 700;
    letter-spacing: 2px;
}}
QLabel[heading="true"] {{
    font-size: 17px;
    letter-spacing: -0.2px;
}}
QLabel[title="true"] {{
    font-size: 28px;
    font-weight: 650;
    letter-spacing: -0.5px;
}}
QLabel[subtitle="true"], QLabel[muted="true"] {{
    color: {PALETTE['muted']};
    font-size: 12px;
}}
QLabel[mono="true"] {{
    font-family: {PALETTE['mono']};
}}
QLabel[badge="true"] {{
    color: {PALETTE['muted']};
    font-family: {PALETTE['mono']};
    font-size: 10px;
    font-weight: 600;
    border: 1px solid {PALETTE['line']};
    border-radius: 6px;
    padding: 5px 8px;
}}
QLabel[statusBadge="true"] {{
    color: #b8c5d3;
    font-size: 11px;
    border: 1px solid {PALETTE['line']};
    border-radius: 12px;
    padding: 7px 11px;
    background: rgba(7, 11, 18, 0.62);
}}
QLabel#statusDot {{
    border-radius: 3px;
    background: #526172;
}}
QLabel#statusDot[ready="true"] {{
    background: {PALETTE['bend']};
}}
QLabel#fileStatus, QLabel[note="true"] {{
    color: #718094;
    font-size: 10px;
}}
QLabel[recordingTime="true"] {{
    color: {PALETTE['bend']};
    font-family: {PALETTE['mono']};
    font-size: 22px;
    font-weight: 700;
}}
QPushButton {{
    border: 1px solid {PALETTE['line_strong']};
    border-radius: 8px;
    padding: 8px 13px;
    background: rgba(12, 18, 28, 0.86);
    color: {PALETTE['text']};
}}
QPushButton:hover:!disabled {{
    border-color: rgba(73, 217, 208, 0.55);
    background: #14202d;
}}
QPushButton:disabled {{
    color: {PALETTE['muted']};
    opacity: 0.38;
}}
QPushButton:checked {{
    border-color: rgba(73, 217, 208, 0.55);
    background: #14202d;
}}
QPushButton[primary="true"] {{
    color: #071012;
    border-color: {PALETTE['bend']};
    background: {PALETTE['bend']};
    font-size: 12px;
    font-weight: 700;
}}
QPushButton[primary="true"]:hover:!disabled {{
    color: #071012;
    background: #6be8e0;
}}
QPushButton[tool="true"] {{
    padding: 8px 12px;
}}
QComboBox {{
    border: 1px solid {PALETTE['line']};
    border-radius: 8px;
    padding: 7px 10px;
    color: #e8eef6;
    background: #0c141d;
}}
QComboBox:disabled {{
    color: {PALETTE['muted']};
}}
QComboBox QAbstractItemView {{
    background: #0c141d;
    color: #e8eef6;
    border: 1px solid {PALETTE['line_strong']};
    selection-background-color: #14202d;
}}
QScrollArea {{
    border: none;
    background: transparent;
}}
QScrollBar:vertical {{
    background: transparent;
    width: 8px;
}}
QScrollBar::handle:vertical {{
    background: #253446;
    border-radius: 4px;
    min-height: 30px;
}}
QScrollBar::add-line, QScrollBar::sub-line {{
    height: 0;
}}
QFrame[sectionBorder="true"] {{
    border-bottom: 1px solid {PALETTE['line']};
}}
QFrame[summaryCell="true"] {{
    border: 1px solid {PALETTE['line']};
    background: transparent;
}}
QLabel[summaryLabel="true"] {{
    color: {PALETTE['muted']};
    font-size: 10px;
}}
QLabel[summaryValue="true"] {{
    font-family: {PALETTE['mono']};
    font-size: 12px;
    font-weight: 650;
}}
QLabel[frameCounter="true"] {{
    color: {PALETTE['muted']};
    font-family: {PALETTE['mono']};
    font-size: 11px;
    font-weight: 600;
}}
QWidget[fingerRow="true"] {{
    background: #0f1722;
    border: 1px solid transparent;
    border-radius: 9px;
}}
QWidget[fingerRow="true"][selected="true"] {{
    border-color: rgba(73, 217, 208, 0.38);
    background: #121e2a;
}}
QLabel[fingerName="true"] {{
    color: #dce6ef;
    font-size: 11px;
    font-weight: 650;
}}
QLabel[angleValue="true"] {{
    color: #afbecd;
    font-family: {PALETTE['mono']};
    font-size: 10px;
    font-weight: 600;
}}
QLabel[axisLegend="true"] {{
    border: 1px solid {PALETTE['line']};
    border-radius: 12px;
    background: rgba(7, 11, 18, 0.72);
    font-family: {PALETTE['mono']};
    font-size: 10px;
    font-weight: 700;
    min-width: 24px;
    max-width: 24px;
    min-height: 24px;
    max-height: 24px;
}}
QLabel[chartLegend="true"] {{
    font-size: 9px;
}}
'''
