'''串口面板（契约 6.6）：端口/波特率下拉、连接/断开/标定/刷新、状态行。'''
from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QComboBox,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from app.panel_utils import phase_badge_text, status_line_text

DEMO_PORT_KEY = 'demo'
BAUD_OPTIONS = ('115200', '460800', '921600', '3000000')
DEFAULT_BAUD = '921600'


class SerialPanel(QWidget):
    '''有线串口控制节。'''

    connectRequested = Signal(str, int)
    disconnectRequested = Signal()
    calibrateRequested = Signal()
    refreshRequested = Signal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._badge = QLabel('未连接')
        self._badge.setProperty('badge', True)
        self._badge.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self._port_combo = QComboBox()
        self._baud_combo = QComboBox()
        for baud in BAUD_OPTIONS:
            self._baud_combo.addItem(baud, int(baud))
        index = self._baud_combo.findText(DEFAULT_BAUD)
        self._baud_combo.setCurrentIndex(index)

        self._refresh_button = QPushButton('刷新端口')
        self._connect_button = QPushButton('连接串口')
        self._connect_button.setProperty('primary', True)
        self._disconnect_button = QPushButton('断开')
        self._calibrate_button = QPushButton('重新标定')
        self._disconnect_button.setEnabled(False)
        self._calibrate_button.setEnabled(False)

        self._status_label = QLabel('接收主控姿态帧（AA55）。连接后可点「重新标定」，张开手静止约 3 s。')
        self._status_label.setProperty('fileStatus', True)
        self._status_label.setWordWrap(True)

        self._refresh_button.clicked.connect(self.refreshRequested)
        self._connect_button.clicked.connect(self._emit_connect)
        self._disconnect_button.clicked.connect(self.disconnectRequested)
        self._calibrate_button.clicked.connect(self.calibrateRequested)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(24, 22, 24, 22)
        layout.setSpacing(10)
        heading = QHBoxLayout()
        heading.addWidget(self._make_heading())
        heading.addStretch(1)
        heading.addWidget(self._badge)
        layout.addLayout(heading)
        row = QHBoxLayout()
        row.setSpacing(8)
        port_field = QVBoxLayout()
        port_field.setSpacing(4)
        port_label = QLabel('端口')
        port_label.setProperty('muted', True)
        port_field.addWidget(port_label)
        port_field.addWidget(self._port_combo, 1)
        baud_field = QVBoxLayout()
        baud_field.setSpacing(4)
        baud_label = QLabel('波特率')
        baud_label.setProperty('muted', True)
        baud_field.addWidget(baud_label)
        baud_field.addWidget(self._baud_combo, 1)
        row.addLayout(port_field, 3)
        row.addLayout(baud_field, 2)
        layout.addLayout(row)
        actions = QHBoxLayout()
        actions.setSpacing(8)
        actions.addWidget(self._refresh_button)
        actions.addWidget(self._connect_button)
        actions.addWidget(self._disconnect_button)
        actions.addWidget(self._calibrate_button)
        layout.addLayout(actions)
        layout.addWidget(self._status_label)

    @staticmethod
    def _make_heading() -> QWidget:
        box = QVBoxLayout()
        box.setSpacing(3)
        kicker = QLabel('WIRED SERIAL')
        kicker.setProperty('kicker', True)
        title = QLabel('有线串口')
        title.setProperty('heading', True)
        box.addWidget(kicker)
        box.addWidget(title)
        widget = QWidget()
        widget.setLayout(box)
        return widget

    def _emit_connect(self) -> None:
        port = self._port_combo.currentData()
        if port is None:
            self._status_label.setText('请先选择串口端口')
            return
        baud = int(self._baud_combo.currentData() or 921600)
        self.connectRequested.emit(str(port), baud)

    # ------------------------------------------------------------------ 契约

    def set_ports(self, ports: list) -> None:
        previous = self._port_combo.currentData()
        self._port_combo.blockSignals(True)
        self._port_combo.clear()
        self._port_combo.addItem('模拟数据', DEMO_PORT_KEY)
        if not ports:
            self._port_combo.addItem('未发现串口', None)
        else:
            for port in ports:
                text = f"{port['device']} · {port.get('description', '')}".strip()
                self._port_combo.addItem(text, port['device'])
        if previous is not None:
            found = self._port_combo.findData(previous)
            if found >= 0:
                self._port_combo.setCurrentIndex(found)
        self._port_combo.blockSignals(False)

    def set_status(self, status: dict) -> None:
        phase = status.get('phase', 'idle')
        self._badge.setText(phase_badge_text(phase))
        self._status_label.setText(status_line_text(status))

    def set_connected(self, connected: bool, demo: bool) -> None:
        self._port_combo.setEnabled(not connected)
        self._baud_combo.setEnabled(not connected)
        self._connect_button.setEnabled(not connected)
        self._disconnect_button.setEnabled(connected)
        if demo:
            self._calibrate_button.setEnabled(False)
            self._calibrate_button.setToolTip('模拟数据源不支持标定')
        else:
            self._calibrate_button.setEnabled(connected)
            self._calibrate_button.setToolTip('')

    def set_calibrate_allowed(self, allowed: bool) -> None:
        '''连接态下按 phase（calibrating/error 时禁用）微调标定钮。'''
        if self._calibrate_button.toolTip():
            return  # demo 模式永远禁用
        self._calibrate_button.setEnabled(allowed)

    def current_selection(self) -> tuple:
        port = self._port_combo.currentData()
        baud = int(self._baud_combo.currentData() or 921600)
        return (str(port) if port is not None else '', baud)
