'''曲线面板（契约 6.6）：QtCharts 复刻 web 端 bend/sway 曲线。'''
from __future__ import annotations

from PySide6.QtCharts import QChart, QChartView, QLineSeries, QValueAxis
from PySide6.QtCore import QMargins, QPointF, Qt
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import QHBoxLayout, QLabel, QVBoxLayout, QWidget

from app.panel_utils import clamp_chart_value, display_sway_deg

FINGER_LABELS = {'thumb': '拇指', 'index': '食指', 'middle': '中指', 'ring': '无名指', 'little': '小指'}
BEND_COLOR = '#49d9d0'
SWAY_COLOR = '#ffbd66'
WINDOW = 400
Y_MIN, Y_MAX = -30.0, 90.0


class ChartPanel(QWidget):
    '''选中手指 bend/sway 曲线 + 当前帧竖线。'''

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._frames = []
        self._finger = 'index'
        self._current_index = 0

        self._title = QLabel('食指曲线')
        self._title.setProperty('heading', True)

        self._chart = QChart()
        self._chart.setBackgroundBrush(QColor('#091019'))
        self._chart.setBackgroundVisible(True)
        self._chart.setMargins(QMargins(0, 0, 0, 0))
        self._chart.legend().setVisible(False)
        self._chart.setTitle('')

        self._bend_series = QLineSeries()
        self._bend_series.setPen(QPen(QColor(BEND_COLOR), 2.5))
        self._sway_series = QLineSeries()
        self._sway_series.setPen(QPen(QColor(SWAY_COLOR), 2.5))
        self._marker_series = QLineSeries()
        marker_pen = QPen(QColor(255, 255, 255, 180))
        marker_pen.setWidthF(1.0)
        self._marker_series.setPen(marker_pen)
        self._chart.addSeries(self._bend_series)
        self._chart.addSeries(self._sway_series)
        self._chart.addSeries(self._marker_series)

        axis_x = QValueAxis()
        axis_x.setRange(0, WINDOW - 1)
        axis_x.setLabelsVisible(False)
        axis_x.setLineVisible(False)
        axis_x.setGridLineVisible(False)
        axis_y = QValueAxis()
        axis_y.setRange(Y_MIN, Y_MAX)
        axis_y.setTickCount(5)
        axis_y.setLabelFormat('%d')
        axis_y.setLabelsColor(QColor('#647487'))
        axis_y.setGridLineColor(QColor(159, 183, 210, 33))
        axis_y.setLineVisible(False)
        self._chart.addAxis(axis_x, Qt.AlignmentFlag.AlignBottom)
        self._chart.addAxis(axis_y, Qt.AlignmentFlag.AlignLeft)
        self._bend_series.attachAxis(axis_x)
        self._bend_series.attachAxis(axis_y)
        self._sway_series.attachAxis(axis_x)
        self._sway_series.attachAxis(axis_y)
        self._marker_series.attachAxis(axis_x)
        self._marker_series.attachAxis(axis_y)

        self._view = QChartView(self._chart)
        self._view.setRenderHint(QPainter.RenderHint.Antialiasing)
        self._view.setStyleSheet('border: 1px solid rgba(159,183,210,0.15); border-radius: 9px; background: #091019;')
        self._view.setFixedHeight(125)

        legend = QHBoxLayout()
        legend.setSpacing(10)
        bend_key = QLabel('● bend')
        bend_key.setProperty('chartLegend', True)
        bend_key.setStyleSheet(f'color: {BEND_COLOR};')
        sway_key = QLabel('● sway')
        sway_key.setProperty('chartLegend', True)
        sway_key.setStyleSheet(f'color: {SWAY_COLOR};')
        legend.addWidget(bend_key)
        legend.addWidget(sway_key)
        legend.addStretch(1)

        note = QLabel('点击上方任意手指切换曲线；竖线表示当前帧。')
        note.setProperty('note', True)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(24, 22, 24, 22)
        layout.setSpacing(8)
        heading = QHBoxLayout()
        heading.addWidget(self._title)
        heading.addStretch(1)
        heading.addLayout(legend)
        layout.addLayout(heading)
        layout.addWidget(self._view)
        layout.addWidget(note)

    # ------------------------------------------------------------------ 契约

    def set_finger(self, name: str) -> None:
        self._finger = name
        self._title.setText(f'{FINGER_LABELS[name]}曲线')

    def set_frames(self, frames: list) -> None:
        '''全量重绘（切指时）。'''
        self._frames = list(frames)
        self._rebuild()

    def push_frame(self, frame: dict) -> None:
        '''追加一帧；窗口上限 400。'''
        self._frames.append(frame)
        if len(self._frames) > WINDOW:
            del self._frames[:len(self._frames) - WINDOW]
        self._rebuild()

    def mark_current(self, index: int) -> None:
        self._current_index = index
        self._marker_series.replace([QPointF(index, Y_MIN), QPointF(index, Y_MAX)])

    def clear(self) -> None:
        self._frames = []
        self._current_index = 0
        self._bend_series.clear()
        self._sway_series.clear()
        self._marker_series.clear()

    # ------------------------------------------------------------------ 内部

    def _rebuild(self) -> None:
        bend_points = []
        sway_points = []
        for i, frame in enumerate(self._frames):
            finger = (frame.get('fingers') or {}).get(self._finger) or {}
            bend = clamp_chart_value(float(finger.get('bend_deg', 0.0) or 0.0))
            sway = clamp_chart_value(display_sway_deg(self._finger, float(finger.get('sway_deg', 0.0) or 0.0)))
            bend_points.append(QPointF(i, bend))
            sway_points.append(QPointF(i, sway))
        self._bend_series.replace(bend_points)
        self._sway_series.replace(sway_points)
        self._marker_series.replace([QPointF(self._current_index, Y_MIN), QPointF(self._current_index, Y_MAX)])
