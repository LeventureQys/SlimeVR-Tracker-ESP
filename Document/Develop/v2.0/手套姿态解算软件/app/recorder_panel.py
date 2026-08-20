'''录像面板（契约：界面上的开始/暂停/结束 + 录制时间显示）。

与 SerialPanel 风格一致；只发信号，状态由 MainWindow 用 set_status 回写。
'''
from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)


def format_elapsed(ms: int) -> str:
    '''毫秒 → mm:ss。'''
    seconds = max(0, ms) // 1000
    return f'{seconds // 60:02d}:{seconds % 60:02d}'


class RecorderPanel(QWidget):
    '''数据录像控制节。'''

    startRequested = Signal()
    pauseRequested = Signal()
    stopRequested = Signal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._start_button = QPushButton('开始录像')
        self._start_button.setProperty('primary', True)
        self._pause_button = QPushButton('暂停')
        self._stop_button = QPushButton('结束')
        self._pause_button.setEnabled(False)
        self._stop_button.setEnabled(False)

        self._time_label = QLabel('00:00')
        self._time_label.setProperty('recordingTime', True)
        self._time_label.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self._status_label = QLabel('解析后的姿态帧将写入 recordings/ 下的时间戳文件夹')
        self._status_label.setProperty('fileStatus', True)
        self._status_label.setWordWrap(True)

        self._start_button.clicked.connect(self.startRequested)
        self._pause_button.clicked.connect(self.pauseRequested)
        self._stop_button.clicked.connect(self.stopRequested)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(24, 22, 24, 22)
        layout.setSpacing(10)
        heading = QHBoxLayout()
        heading.addWidget(self._make_heading())
        heading.addStretch(1)
        heading.addWidget(self._time_label)
        layout.addLayout(heading)
        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(self._start_button)
        actions.addWidget(self._pause_button)
        actions.addWidget(self._stop_button)
        layout.addLayout(actions)
        layout.addWidget(self._status_label)

    @staticmethod
    def _make_heading() -> QWidget:
        box = QVBoxLayout()
        box.setSpacing(3)
        kicker = QLabel('DATA RECORDER')
        kicker.setProperty('kicker', True)
        title = QLabel('数据录像')
        title.setProperty('heading', True)
        box.addWidget(kicker)
        box.addWidget(title)
        widget = QWidget()
        widget.setLayout(box)
        return widget

    # ------------------------------------------------------------------ 契约

    def set_status(self, status) -> None:
        '''RecorderStatus → 按钮启用态/文字/时间/状态行。'''
        state = status.state
        self._time_label.setText(format_elapsed(status.elapsed_ms))
        if state == 'idle':
            self._start_button.setEnabled(True)
            self._start_button.setText('开始录像')
            self._pause_button.setEnabled(False)
            self._pause_button.setText('暂停')
            self._stop_button.setEnabled(False)
            self._status_label.setText('原始字节流 + 解析帧将写入 recordings/ 下的时间戳文件夹')
        elif state == 'recording':
            self._start_button.setEnabled(False)
            self._pause_button.setEnabled(True)
            self._pause_button.setText('暂停')
            self._stop_button.setEnabled(True)
            self._status_label.setText(
                f'录制中 · {status.frame_count} 帧 / {_format_bytes(status.raw_bytes)} · {status.folder}')
        else:  # paused
            self._start_button.setEnabled(False)
            self._pause_button.setEnabled(True)
            self._pause_button.setText('继续')
            self._stop_button.setEnabled(True)
            self._status_label.setText(
                f'已暂停 · {status.frame_count} 帧 / {_format_bytes(status.raw_bytes)} · {status.folder}')

    def show_stopped_summary(self, folder: str, frame_count: int, raw_bytes: int = 0) -> None:
        if folder:
            self._status_label.setText(
                f'已保存 {frame_count} 帧 / {_format_bytes(raw_bytes)} 原始流 → {folder}')
        else:
            self._status_label.setText('未录制任何数据')


def _format_bytes(count: int) -> str:
    if count >= 1024 * 1024:
        return f'{count / (1024 * 1024):.1f} MB'
    if count >= 1024:
        return f'{count / 1024:.1f} KB'
    return f'{count} B'
