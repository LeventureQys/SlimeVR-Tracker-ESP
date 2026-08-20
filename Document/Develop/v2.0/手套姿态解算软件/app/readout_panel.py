'''读数面板（契约 6.6）：当前帧摘要 + 五指角度行（可点选）。'''
from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QProgressBar,
    QVBoxLayout,
    QWidget,
)

from app.panel_utils import display_sway_deg, frame_summary

FINGER_ORDER = ('thumb', 'index', 'middle', 'ring', 'little')
FINGER_LABELS = {'thumb': '拇指', 'index': '食指', 'middle': '中指', 'ring': '无名指', 'little': '小指'}


class _AngleBar(QProgressBar):
    '''双向 sway 条（±30°→±50%，中心基线；负值向左）或单向 bend 条（0~90°→0~100%）。'''

    def __init__(self, sway_mode: bool, color: str, parent=None) -> None:
        super().__init__(parent)
        self._sway = sway_mode
        self.setTextVisible(False)
        self.setFixedHeight(4)
        self.setStyleSheet(f'''
            QProgressBar {{ background: #263342; border: none; border-radius: 2px; }}
            QProgressBar::chunk {{ background: {color}; border-radius: 2px; }}
        ''')
        if sway_mode:
            self.setRange(-50, 50)
            self.setValue(0)
            self.setInvertedAppearance(False)
            self.setFormat('')
        else:
            self.setRange(0, 100)
            self.setValue(0)

    def set_angle(self, deg: float) -> None:
        if self._sway:
            percent = int(max(-50.0, min(50.0, deg / 30.0 * 50.0)))
            if percent < 0:
                self.setInvertedAppearance(False)
                self.setValue(-percent)
                self.setAlignment(Qt.AlignmentFlag.AlignRight)
            else:
                self.setValue(percent)
                self.setAlignment(Qt.AlignmentFlag.AlignLeft)
        else:
            self.setValue(int(max(0.0, min(100.0, deg / 90.0 * 100.0))))


class FingerRow(QWidget):
    '''单指读数行：名称 + bend 条/值 + sway 条/值；点击发 fingerSelected。'''

    clicked = Signal(str)

    def __init__(self, name: str, parent=None) -> None:
        super().__init__(parent)
        self._name = name
        self.setProperty('fingerRow', True)
        label = QLabel(FINGER_LABELS[name])
        label.setProperty('fingerName', True)
        self._bend_bar = _AngleBar(False, '#49d9d0')
        self._bend_value = QLabel('0.0')
        self._bend_value.setProperty('angleValue', True)
        self._bend_value.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        self._sway_bar = _AngleBar(True, '#ffbd66')
        self._sway_value = QLabel('+0.0')
        self._sway_value.setProperty('angleValue', True)
        self._sway_value.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(9, 8, 9, 8)
        layout.setSpacing(8)
        layout.addWidget(label, 0)
        layout.addWidget(self._bend_bar, 1)
        layout.addWidget(self._bend_value, 0)
        layout.addWidget(self._sway_bar, 1)
        layout.addWidget(self._sway_value, 0)
        label.setFixedWidth(48)
        self._bend_value.setFixedWidth(38)
        self._sway_value.setFixedWidth(40)

    def mousePressEvent(self, event) -> None:
        self.clicked.emit(self._name)
        super().mousePressEvent(event)

    def set_selected(self, selected: bool) -> None:
        self.setProperty('selected', selected)
        self.style().unpolish(self)
        self.style().polish(self)

    def set_angles(self, bend_deg: float, sway_deg: float) -> None:
        self._bend_bar.set_angle(bend_deg)
        self._bend_value.setText(f'{bend_deg:.1f}')
        sign = '+' if sway_deg >= 0 else ''
        self._sway_value.setText(f'{sign}{sway_deg:.1f}')
        self._sway_bar.set_angle(sway_deg)


class ReadoutPanel(QWidget):
    '''当前帧 + 五指角度。'''

    fingerSelected = Signal(str)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._frame_counter = QLabel('0 / 0')
        self._frame_counter.setProperty('frameCounter', True)
        self._summary_labels = {}
        self._rows = {}
        self._selected = 'index'

        layout = QVBoxLayout(self)
        layout.setContentsMargins(24, 22, 24, 22)
        layout.setSpacing(12)

        heading = QHBoxLayout()
        heading.addWidget(self._make_heading('FRAME READOUT', '当前帧'))
        heading.addStretch(1)
        heading.addWidget(self._frame_counter)
        layout.addLayout(heading)

        grid = QGridLayout()
        grid.setSpacing(0)
        for i, (key, label_text) in enumerate((
            ('time', '时间'), ('interval', '采样间隔'), ('max_bend', '最大弯曲'), ('max_sway', '最大侧摆'),
        )):
            cell = QFrame()
            cell.setProperty('summaryCell', True)
            cell_layout = QVBoxLayout(cell)
            cell_layout.setContentsMargins(12, 11, 12, 11)
            cell_layout.setSpacing(4)
            label = QLabel(label_text)
            label.setProperty('summaryLabel', True)
            value = QLabel('0.000 s' if key == 'time' else '--')
            value.setProperty('summaryValue', True)
            cell_layout.addWidget(label)
            cell_layout.addWidget(value)
            grid.addWidget(cell, i // 2, i % 2)
            self._summary_labels[key] = value
        layout.addLayout(grid)

        layout.addSpacing(6)
        layout.addWidget(self._make_heading('PALM-RELATIVE ANGLES', '五指角度'))
        for name in FINGER_ORDER:
            row = FingerRow(name)
            row.clicked.connect(self._select_finger)
            self._rows[name] = row
            layout.addWidget(row)
        self._rows[self._selected].set_selected(True)

    @staticmethod
    def _make_heading(kicker_text: str, title_text: str) -> QWidget:
        box = QVBoxLayout()
        box.setSpacing(3)
        kicker = QLabel(kicker_text)
        kicker.setProperty('kicker', True)
        title = QLabel(title_text)
        title.setProperty('heading', True)
        box.addWidget(kicker)
        box.addWidget(title)
        widget = QWidget()
        widget.setLayout(box)
        return widget

    def _select_finger(self, name: str) -> None:
        self._selected = name
        for row_name, row in self._rows.items():
            row.set_selected(row_name == name)
        self.fingerSelected.emit(name)

    # ------------------------------------------------------------------ 契约

    def selected_finger(self) -> str:
        return self._selected

    def apply_frame(self, frame: dict, index: int, frames: list) -> None:
        summary = frame_summary(frame, index, frames)
        self._frame_counter.setText(summary['counter_text'])
        self._summary_labels['time'].setText(f"{summary['relative_time_s']:.3f} s")
        if summary['interval_ms'] is None:
            self._summary_labels['interval'].setText('-- ms')
        else:
            self._summary_labels['interval'].setText(f"{summary['interval_ms']:.2f} ms")
        self._summary_labels['max_bend'].setText(f"{summary['max_bend_deg']:.1f}°")
        self._summary_labels['max_sway'].setText(f"{summary['max_sway_deg']:.1f}°")
        for name in FINGER_ORDER:
            finger = (frame.get('fingers') or {}).get(name) or {}
            bend = float(finger.get('bend_deg', 0.0) or 0.0)
            sway = display_sway_deg(name, float(finger.get('sway_deg', 0.0) or 0.0))
            self._rows[name].set_angles(bend, sway)

    def reset(self) -> None:
        self._frame_counter.setText('0 / 0')
        self._summary_labels['time'].setText('0.000 s')
        self._summary_labels['interval'].setText('-- ms')
        self._summary_labels['max_bend'].setText('0.0°')
        self._summary_labels['max_sway'].setText('0.0°')
        for name in FINGER_ORDER:
            self._rows[name].set_angles(0.0, 0.0)
