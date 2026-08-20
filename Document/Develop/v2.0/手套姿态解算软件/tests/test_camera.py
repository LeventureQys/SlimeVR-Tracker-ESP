'''轨道相机测试：默认位/钳制/阻尼/reset/投影正确性。'''
from __future__ import annotations

import math

from PySide6.QtGui import QVector3D

from app.camera import (
    DEFAULT_POSITION,
    DEFAULT_TARGET,
    MAX_DISTANCE,
    MIN_DISTANCE,
    PHI_MAX,
    PHI_MIN,
    OrbitCamera,
    clamp_phi,
    clamp_radius,
    look_at_view_matrix,
    perspective_matrix,
)
from app.quaternion import mat4_vec3


def near(v: QVector3D, expected, tol=1e-5) -> None:
    assert abs(v.x() - expected[0]) < tol
    assert abs(v.y() - expected[1]) < tol
    assert abs(v.z() - expected[2]) < tol


def test_default_position_and_target():
    cam = OrbitCamera()
    near(cam.position(), DEFAULT_POSITION, 1e-4)
    near(cam.target, DEFAULT_TARGET)


def test_reset_restores_defaults():
    cam = OrbitCamera()
    cam.rotate(200, -150, 800, 600)
    cam.zoom(-240)
    cam.reset()
    near(cam.position(), DEFAULT_POSITION, 1e-4)
    near(cam.target, DEFAULT_TARGET)


def test_radius_clamped():
    cam = OrbitCamera()
    for _ in range(200):
        cam.zoom(1200)  # 大幅放大
    assert cam.radius <= MAX_DISTANCE + 1e-6
    for _ in range(2000):
        cam.zoom(-1200)  # 大幅缩小
    assert cam.radius >= MIN_DISTANCE - 1e-6
    assert clamp_radius(0.0) == MIN_DISTANCE
    assert clamp_radius(1e9) == MAX_DISTANCE


def test_phi_clamped():
    cam = OrbitCamera()
    for _ in range(100):
        cam.rotate(0, 10000, 800, 600)
    assert cam.phi <= PHI_MAX + 1e-6
    for _ in range(100):
        cam.rotate(0, -10000, 800, 600)
    assert cam.phi >= PHI_MIN - 1e-6
    assert clamp_phi(-5.0) == PHI_MIN
    assert clamp_phi(5.0) == PHI_MAX


def test_damping_converges_monotonically():
    cam = OrbitCamera()
    cam.step(1.0)  # 先收敛到默认
    cam.rotate(150, -100, 800, 600)
    movements = []
    prev = cam.position()
    for _ in range(600):
        cam.step(1 / 60)
        pos = cam.position()
        movements.append((pos - prev).length())
        prev = pos
    assert movements[0] > 1e-4  # 初始在移动
    assert movements[-1] < 1e-4  # 基本静止（收敛；QVector3D 为 float32，噪声 ~1e-7）
    assert all(movements[i + 1] <= movements[i] + 5e-6 for i in range(len(movements) - 1))


def test_rotate_changes_theta():
    cam = OrbitCamera()
    cam.step(1.0)  # 先收敛
    before = cam.position()
    cam.rotate(120, 0, 800, 600)
    cam.step(1.0)
    after = cam.position()
    assert (before - after).length() > 0.1


def test_view_matrix_looks_at_target():
    eye = QVector3D(0, 0, 10)
    target = QVector3D(0, 0, 0)
    m = look_at_view_matrix(eye, target, QVector3D(0, 1, 0))
    p = mat4_vec3(m, target)
    near(p, (0, 0, -10))  # 目标在相机前方 -Z


def test_projection_maps_frustum():
    m = perspective_matrix(34.0, 1.5, 0.1, 100.0)
    # 近平面中心点 → 透视除法后 NDC (0,0,-1)
    v = QVector3D(0, 0, -0.1)
    clip = mat4_vec3(m, v)
    w = m[12] * v.x() + m[13] * v.y() + m[14] * v.z() + m[15]
    p = clip / w
    assert abs(p.x()) < 1e-5 and abs(p.y()) < 1e-5
    assert abs(p.z() + 1.0) < 1e-5


def test_pan_moves_target():
    cam = OrbitCamera()
    before = QVector3D(cam.target)
    cam.pan(50, 0, 800, 600)
    after = cam.target
    assert (before - after).length() > 0.05
