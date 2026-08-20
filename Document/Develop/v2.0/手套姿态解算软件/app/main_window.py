'''主窗口：左 3D 视口 + 右 390px 面板，接线串口会话 ↔ 面板 ↔ 3D 手（设计文档 4/7 节）。'''
from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QCheckBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPushButton,
    QScrollArea,
    QVBoxLayout,
    QWidget,
)

from app.chart_panel import ChartPanel
from app.data_recorder import DataRecorder
from app.frame_window import FrameWindow
from app.hand_pose import FINGER_ORDER, HandPoseModel
from app.hand_view import HandViewWidget
from app.panel_utils import display_sway_deg, phase_badge_text
from app.readout_panel import ReadoutPanel
from app.recorder_panel import RecorderPanel
from app.serial_panel import DEMO_PORT_KEY, SerialPanel
from tools.serial_live import SerialLiveSession

FRAME_DRAIN_MS = 33
STATUS_POLL_MS = 250
RECORDER_REFRESH_MS = 200


class MainWindow(QMainWindow):
    def __init__(self, asset, auto_demo: bool = False, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle('灵巧手上位机')
        self.resize(1200, 760)
        self.setMinimumSize(1024, 640)

        self._session = SerialLiveSession()
        self._frame_window = FrameWindow(400)
        self._pose_model = HandPoseModel(asset)
        self._recorder = DataRecorder()
        try:
            self._view = HandViewWidget(asset)
        except Exception as error:  # GL 上下文创建失败等
            raise RuntimeError(f'3D 视口初始化失败：{error}') from error

        self._serial_panel = SerialPanel()
        self._recorder_panel = RecorderPanel()
        self._readout_panel = ReadoutPanel()
        self._chart_panel = ChartPanel()

        # ---- 左视口叠加层 ----
        viewport = QWidget()
        viewport.setObjectName('viewportRoot')
        viewport_layout = QVBoxLayout(viewport)
        viewport_layout.setContentsMargins(0, 0, 0, 0)
        viewport_layout.setSpacing(0)
        viewport_layout.addWidget(self._view, 1)

        header = QHBoxLayout()
        header.setContentsMargins(34, 30, 32, 0)
        title_box = QVBoxLayout()
        title_box.setSpacing(0)
        eyebrow = QLabel('DEXTEROUS HAND')
        eyebrow.setProperty('kicker', True)
        title = QLabel('灵巧手上位机')
        title.setProperty('title', True)
        subtitle = QLabel('有线串口实时 · bend / sway')
        subtitle.setProperty('subtitle', True)
        title_box.addWidget(eyebrow)
        title_box.addWidget(title)
        title_box.addWidget(subtitle)
        header.addLayout(title_box)
        header.addStretch(1)
        badges = QWidget()
        badges.setProperty('statusBadge', True)
        badges_layout = QHBoxLayout(badges)
        badges_layout.setContentsMargins(11, 8, 11, 8)
        badges_layout.setSpacing(9)
        self._status_dot = QLabel()
        self._status_dot.setObjectName('statusDot')
        self._status_dot.setFixedSize(7, 7)
        self._scene_status = QLabel('等待数据')
        self._scene_status.setProperty('subtitle', True)
        divider = QFrame()
        divider.setFixedSize(1, 13)
        divider.setStyleSheet('background: rgba(159,183,210,0.28);')
        self._fps_label = QLabel('-- FPS')
        self._fps_label.setProperty('subtitle', True)
        badges_layout.addWidget(self._status_dot)
        badges_layout.addWidget(self._scene_status)
        badges_layout.addWidget(divider)
        badges_layout.addWidget(self._fps_label)
        header.addWidget(badges)
        viewport_layout.addLayout(header)

        tools = QHBoxLayout()
        tools.setContentsMargins(34, 0, 0, 0)
        tools.setSpacing(7)
        reset_button = QPushButton('重置视角')
        reset_button.setProperty('tool', True)
        self._skin_button = QPushButton('软组织')
        self._skin_button.setProperty('tool', True)
        self._skin_button.setCheckable(True)
        self._skin_button.setChecked(True)
        self._grid_button = QPushButton('网格')
        self._grid_button.setProperty('tool', True)
        self._grid_button.setCheckable(True)
        self._grid_button.setChecked(True)
        tools.addWidget(reset_button)
        tools.addWidget(self._skin_button)
        tools.addWidget(self._grid_button)
        tools.addStretch(1)
        viewport_layout.addLayout(tools)

        axis_legend = QHBoxLayout()
        axis_legend.setContentsMargins(35, 0, 0, 28)
        axis_legend.setSpacing(6)
        for letter, color in (('X', '#ff7685'), ('Y', '#67dda0'), ('Z', '#73aaff')):
            label = QLabel(letter)
            label.setProperty('axisLegend', True)
            label.setStyleSheet(f'color: {color};')
            label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            axis_legend.addWidget(label)
        axis_legend.addStretch(1)
        viewport_layout.addLayout(axis_legend)

        # ---- 右面板 ----
        panel_root = QWidget()
        panel_root.setObjectName('controlPanel')
        panel_root.setFixedWidth(390)
        panel_layout = QVBoxLayout(panel_root)
        panel_layout.setContentsMargins(0, 0, 0, 0)
        panel_layout.setSpacing(0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.viewport().setAutoFillBackground(False)
        scroll.setStyleSheet('QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }')
        inner = QWidget()
        inner_layout = QVBoxLayout(inner)
        inner_layout.setContentsMargins(0, 0, 0, 0)
        inner_layout.setSpacing(0)
        inner_layout.addWidget(self._serial_panel)
        inner_layout.addWidget(self._section_border())
        inner_layout.addWidget(self._recorder_panel)
        inner_layout.addWidget(self._section_border())
        inner_layout.addWidget(self._readout_panel)
        inner_layout.addWidget(self._section_border())
        inner_layout.addWidget(self._chart_panel)
        inner_layout.addWidget(self._section_border())
        footer = QLabel('上位机只呈现主控输出的姿态结果')
        footer.setProperty('note', True)
        footer.setContentsMargins(24, 14, 24, 20)
        inner_layout.addWidget(footer)
        inner_layout.addStretch(1)
        scroll.setWidget(inner)
        panel_layout.addWidget(scroll)

        central = QWidget()
        central.setObjectName('centralRoot')
        central_layout = QHBoxLayout(central)
        central_layout.setContentsMargins(0, 0, 0, 0)
        central_layout.setSpacing(0)
        central_layout.addWidget(viewport, 1)
        central_layout.addWidget(panel_root)
        self.setCentralWidget(central)

        # ---- 接线 ----
        self._serial_panel.connectRequested.connect(self._connect)
        self._serial_panel.disconnectRequested.connect(self._disconnect)
        self._serial_panel.calibrateRequested.connect(self._calibrate)
        self._serial_panel.refreshRequested.connect(self._refresh_ports)
        self._recorder_panel.startRequested.connect(self._start_recording)
        self._recorder_panel.pauseRequested.connect(self._toggle_recording_pause)
        self._recorder_panel.stopRequested.connect(self._stop_recording)
        self._readout_panel.fingerSelected.connect(self._on_finger_selected)
        self._view.fpsChanged.connect(self._on_fps)
        reset_button.clicked.connect(self._view.reset_view)
        self._skin_button.toggled.connect(self._view.set_skin_visible)
        self._grid_button.toggled.connect(self._view.set_grid_visible)

        self._drain_timer = QTimer(self)
        self._drain_timer.setInterval(FRAME_DRAIN_MS)
        self._drain_timer.timeout.connect(self._drain_frames)
        self._drain_timer.start()
        self._status_timer = QTimer(self)
        self._status_timer.setInterval(STATUS_POLL_MS)
        self._status_timer.timeout.connect(self._poll_status)
        self._status_timer.start()
        self._recorder_timer = QTimer(self)
        self._recorder_timer.setInterval(RECORDER_REFRESH_MS)
        self._recorder_timer.timeout.connect(self._refresh_recorder_status)
        self._recorder_timer.start()

        self._apply_demo = False
        self._scene_status_base = None
        self._refresh_ports()
        if auto_demo:
            self._connect_demo()

    @staticmethod
    def _section_border() -> QFrame:
        line = QFrame()
        line.setProperty('sectionBorder', True)
        line.setFixedHeight(1)
        return line

    # ------------------------------------------------------------------ 会话交互

    def _refresh_ports(self) -> None:
        try:
            ports = self._session.list_ports()
        except Exception as error:
            self._serial_panel.set_status({'phase': 'error', 'message': f'刷新端口失败：{error}'})
            return
        self._serial_panel.set_ports(ports)
        # 仅一个真实端口时自动连接（复刻 viewer.js autoConnect）
        real_ports = [p for p in ports if p.get('device')]
        if len(real_ports) == 1 and not self._session.running:
            port, baud = real_ports[0]['device'], 921600
            self._connect(port, baud)

    def _connect(self, port: str, baud: int) -> None:
        self._serial_panel.set_status({'phase': 'connecting', 'message': f'正在连接 {port} @ {baud}…'})
        try:
            if port == DEMO_PORT_KEY:
                self._session.start_demo()
            else:
                self._session.start(port, baud)
        except Exception as error:
            self._serial_panel.set_status({'phase': 'error', 'message': f'连接失败：{error}'})
            self._apply_demo = False
            return
        self._frame_window.clear()
        self._chart_panel.clear()
        self._readout_panel.reset()
        self._apply_demo = port == DEMO_PORT_KEY
        self._serial_panel.set_connected(True, self._apply_demo)
        self._scene_status.setText('主控姿态控制')
        self._scene_status_base = None

    def _connect_demo(self) -> None:
        self._connect(DEMO_PORT_KEY, 921600)

    def _disconnect(self) -> None:
        self._session.stop()
        self._apply_demo = False
        self._serial_panel.set_connected(False, False)
        self._serial_panel.set_status({'phase': 'idle', 'message': '串口未连接'})
        self._readout_panel.reset()
        self._chart_panel.clear()
        self._frame_window.clear()
        # 断开时若在录制：自动结束并保存
        if self._recorder.state != 'idle':
            self._session.set_raw_sink(None)
            status = self._recorder.status()
            folder = self._recorder.stop(self._session.status_dict())
            self._recorder_panel.set_status(self._recorder.status())
            self._recorder_panel.show_stopped_summary(folder, status.frame_count, status.raw_bytes)
        self._scene_status.setText('等待数据')
        self._status_dot.setProperty('ready', False)
        self._status_dot.style().unpolish(self._status_dot)
        self._status_dot.style().polish(self._status_dot)

    def _calibrate(self) -> None:
        if not self._session.running:
            self._serial_panel.set_status({'phase': 'idle', 'message': '请先连接串口'})
            return
        try:
            self._session.request_calibrate()
        except Exception as error:
            self._serial_panel.set_status({'phase': 'error', 'message': f'标定请求失败：{error}'})

    # ------------------------------------------------------------------ 录像控制

    def _start_recording(self) -> None:
        try:
            folder = self._recorder.start()
        except Exception as error:
            self._recorder_panel.show_stopped_summary('', 0)
            self._recorder_panel.set_status(self._recorder.status())
            self._serial_panel.set_status({'phase': 'error', 'message': f'开始录像失败：{error}'})
            return
        # 串口原始字节流 1:1 接入录制器（串口线程内调用，线程安全）
        self._session.set_raw_sink(self._recorder.write_raw_bytes)
        self._recorder_panel.set_status(self._recorder.status())

    def _toggle_recording_pause(self) -> None:
        if self._recorder.state == 'recording':
            self._recorder.pause()
        elif self._recorder.state == 'paused':
            self._recorder.resume()
        self._recorder_panel.set_status(self._recorder.status())

    def _stop_recording(self) -> None:
        self._session.set_raw_sink(None)
        status = self._recorder.status()
        folder = self._recorder.stop(self._session.status_dict())
        self._recorder_panel.set_status(self._recorder.status())
        self._recorder_panel.show_stopped_summary(folder, status.frame_count, status.raw_bytes)

    def _refresh_recorder_status(self) -> None:
        self._recorder_panel.set_status(self._recorder.status())

    # ------------------------------------------------------------------ 定时

    def _drain_frames(self) -> None:
        frames = self._session.drain_frames()
        if not frames:
            return
        for frame in frames:
            self._frame_window.push(frame)
            self._chart_panel.push_frame(frame)
            self._recorder.write_frame(frame)  # 录像：解析后的原始帧直接写入
        latest = self._frame_window.latest()
        index = self._frame_window.index_of_latest()
        all_frames = self._frame_window.frames()
        angles = {}
        for name in FINGER_ORDER:
            finger = (latest.get('fingers') or {}).get(name) or {}
            angles[name] = {
                'bendDeg': float(finger.get('bend_deg', 0.0) or 0.0),
                'swayDeg': display_sway_deg(name, float(finger.get('sway_deg', 0.0) or 0.0)),
            }
        pose = self._pose_model.apply_pose(latest['wrist_quaternion_wxyz'], angles)
        self._view.set_pose(pose)
        self._readout_panel.apply_frame(latest, index, all_frames)
        self._chart_panel.mark_current(index)

    def _poll_status(self) -> None:
        status = self._session.status_dict()
        self._serial_panel.set_status(status)
        phase = status.get('phase', 'idle')
        ready = phase in ('live', 'calibrating')
        self._status_dot.setProperty('ready', ready)
        self._status_dot.style().unpolish(self._status_dot)
        self._status_dot.style().polish(self._status_dot)
        if not self._apply_demo:
            self._serial_panel.set_calibrate_allowed(phase not in ('calibrating', 'error'))
        if phase == 'calibrating':
            self._scene_status.setText('串口标定中')
        elif phase == 'live' and self._scene_status.text() not in ('真实手模型就绪', '主控姿态控制'):
            self._scene_status.setText('主控姿态控制')
        if phase == 'error':
            self._apply_demo = False
            self._serial_panel.set_connected(False, False)

    # ------------------------------------------------------------------ 面板信号

    def _on_finger_selected(self, name: str) -> None:
        self._chart_panel.set_finger(name)
        self._chart_panel.set_frames(self._frame_window.frames())
        self._chart_panel.mark_current(self._frame_window.index_of_latest())

    def _on_fps(self, fps: float) -> None:
        self._fps_label.setText(f'{round(fps)} FPS')

    def closeEvent(self, event) -> None:
        self._session.stop()
        if self._recorder.state != 'idle':
            self._session.set_raw_sink(None)
            self._recorder.stop(self._session.status_dict())
        super().closeEvent(event)
