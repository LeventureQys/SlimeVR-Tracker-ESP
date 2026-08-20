'''轨道相机纯函数模块（OrbitControls 同参数，设计文档 5.6）。

球坐标状态：theta/phi/radius/target；阻尼指数趋近；行主序 view/projection 矩阵。
'''
from __future__ import annotations

import math

from PySide6.QtGui import QVector3D

FOV_DEG = 34.0
NEAR = 0.1
FAR = 100.0
DEFAULT_POSITION = (-4.2, 0.7, 12.2)
DEFAULT_TARGET = (0.0, 0.3, 0.0)
MIN_DISTANCE = 7.2
MAX_DISTANCE = 18.0
DAMPING_FACTOR = 0.07
PHI_MIN = 0.01
PHI_MAX = math.pi - 0.01


def spherical_to_vec3(target: QVector3D, theta: float, phi: float, radius: float) -> QVector3D:
    '''球坐标 → 相机位置（OrbitControls 约定：phi 为极角，theta 为方位角）。'''
    sin_phi = math.sin(phi)
    return QVector3D(
        target.x() + radius * sin_phi * math.sin(theta),
        target.y() + radius * math.cos(phi),
        target.z() + radius * sin_phi * math.cos(theta),
    )


def vec3_to_spherical(position: QVector3D, target: QVector3D) -> tuple:
    '''位置 → (theta, phi, radius)。'''
    offset = position - target
    radius = offset.length()
    if radius < 1e-9:
        return 0.0, PHI_MIN, 0.0
    phi = math.acos(max(-1.0, min(1.0, offset.y() / radius)))
    theta = math.atan2(offset.x(), offset.z())
    return theta, phi, radius


def clamp_phi(phi: float) -> float:
    return max(PHI_MIN, min(PHI_MAX, phi))


def clamp_radius(radius: float) -> float:
    return max(MIN_DISTANCE, min(MAX_DISTANCE, radius))


def damping_step(current: float, desired: float, damping: float, dt: float) -> float:
    '''OrbitControls dampingFactor 指数趋近：1 - exp(-k·dt)，k 与 0.07 等效。'''
    factor = 1.0 - math.exp(-(damping * 60.0) * dt)
    return current + (desired - current) * factor


def look_at_view_matrix(eye: QVector3D, target: QVector3D, up: QVector3D) -> tuple:
    '''lookAt 视图矩阵（行主序 16 元组）。'''
    forward = (target - eye).normalized()
    right = QVector3D.crossProduct(forward, up).normalized()
    true_up = QVector3D.crossProduct(right, forward)
    return (
        right.x(), right.y(), right.z(), -QVector3D.dotProduct(right, eye),
        true_up.x(), true_up.y(), true_up.z(), -QVector3D.dotProduct(true_up, eye),
        -forward.x(), -forward.y(), -forward.z(), QVector3D.dotProduct(forward, eye),
        0.0, 0.0, 0.0, 1.0,
    )


def perspective_matrix(fov_deg: float, aspect: float, near: float, far: float) -> tuple:
    '''透视投影矩阵（行主序，列向量，OpenGL 深度 [-1,1]）。'''
    f = 1.0 / math.tan(math.radians(fov_deg) / 2.0)
    return (
        f / aspect, 0.0, 0.0, 0.0,
        0.0, f, 0.0, 0.0,
        0.0, 0.0, (far + near) / (near - far), (2.0 * far * near) / (near - far),
        0.0, 0.0, -1.0, 0.0,
    )


class OrbitCamera:
    '''带阻尼的轨道相机状态（纯数据，供 HandViewWidget 与测试使用）。'''

    def __init__(self, position=None, target=None) -> None:
        target = target if target is not None else QVector3D(*DEFAULT_TARGET)
        position = position if position is not None else QVector3D(*DEFAULT_POSITION)
        self.target = QVector3D(target)
        theta, phi, radius = vec3_to_spherical(position, self.target)
        self.theta = theta
        self.phi = phi
        self.radius = radius
        # 阻尼用“当前”状态：初始与目标一致
        self._cur_theta = theta
        self._cur_phi = phi
        self._cur_radius = radius
        self._cur_target = QVector3D(target)

    def position(self) -> QVector3D:
        return spherical_to_vec3(self._cur_target, self._cur_theta, self._cur_phi, self._cur_radius)

    def rotate(self, dx: float, dy: float, width: float, height: float) -> None:
        '''鼠标拖动旋转：dx/dy 为像素位移。'''
        if height <= 0:
            return
        self.theta -= 2.0 * math.pi * dx / width
        self.phi = clamp_phi(self.phi - 2.0 * math.pi * dy / height)

    def zoom(self, wheel_delta: float) -> None:
        '''滚轮缩放（wheel_delta 为 QWheelEvent.angleDelta().y()）。'''
        factor = math.pow(0.95, wheel_delta / 120.0)
        self.radius = clamp_radius(self.radius * factor)

    def pan(self, dx: float, dy: float, width: float, height: float) -> None:
        '''右键平移：沿相机右/上向量移动目标，距离比例。'''
        eye = self.position()
        forward = (self.target - eye).normalized()
        right = QVector3D.crossProduct(forward, QVector3D(0, 1, 0)).normalized()
        up = QVector3D.crossProduct(right, forward)
        scale = self._cur_radius * 2.0 * math.tan(math.radians(FOV_DEG) / 2.0)
        self.target += right * (-dx / max(1.0, width)) * scale
        self.target += up * (dy / max(1.0, height)) * scale

    def step(self, dt: float) -> None:
        '''阻尼趋近一步（每帧调用）。'''
        k = DAMPING_FACTOR
        self._cur_theta = damping_step(self._cur_theta, self.theta, k, dt)
        self._cur_phi = damping_step(self._cur_phi, self.phi, k, dt)
        self._cur_radius = damping_step(self._cur_radius, self.radius, k, dt)
        self._cur_target = QVector3D(
            damping_step(self._cur_target.x(), self.target.x(), k, dt),
            damping_step(self._cur_target.y(), self.target.y(), k, dt),
            damping_step(self._cur_target.z(), self.target.z(), k, dt),
        )

    def reset(self) -> None:
        theta, phi, radius = vec3_to_spherical(QVector3D(*DEFAULT_POSITION), QVector3D(*DEFAULT_TARGET))
        self.theta = theta
        self.phi = phi
        self.radius = radius
        self.target = QVector3D(*DEFAULT_TARGET)
        self._cur_theta = theta
        self._cur_phi = phi
        self._cur_radius = radius
        self._cur_target = QVector3D(self.target)

    def view_matrix(self) -> tuple:
        return look_at_view_matrix(self.position(), self._cur_target, QVector3D(0, 1, 0))

    def projection_matrix(self, aspect: float) -> tuple:
        return perspective_matrix(FOV_DEG, aspect, NEAR, FAR)
