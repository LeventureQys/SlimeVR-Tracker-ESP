'''四元数基变换与矩阵工具测试。'''
from __future__ import annotations

import math

import pytest
from PySide6.QtGui import QQuaternion, QVector3D

from app.quaternion import (
    ALGORITHM_TO_HAND,
    algorithm_quaternion_to_hand,
    mat4_mul,
    mat4_vec3,
    quat_from_hamilton,
    quat_to_mat4,
    trs_to_mat4,
    vec3_to_tuple,
)


def near(v: QVector3D, expected, tol=1e-5) -> None:
    assert abs(v.x() - expected[0]) < tol, f'x: {v.x()} != {expected[0]}'
    assert abs(v.y() - expected[1]) < tol, f'y: {v.y()} != {expected[1]}'
    assert abs(v.z() - expected[2]) < tol, f'z: {v.z()} != {expected[2]}'


def test_identity_maps_identity():
    q = algorithm_quaternion_to_hand((1.0, 0.0, 0.0, 0.0))
    near(q.rotatedVector(QVector3D(1, 0, 0)), (1, 0, 0))
    near(q.rotatedVector(QVector3D(0, 1, 0)), (0, 1, 0))
    near(q.rotatedVector(QVector3D(0, 0, 1)), (0, 0, 1))


def test_base_change_axis_mapping():
    # 算法 +X 轴 90° 旋转 → 模型 -Y 轴 90° 旋转（X→-Y、Y→X、Z→Z）
    q_plus_x = QQuaternion.fromAxisAndAngle(QVector3D(1, 0, 0), 90.0)
    q_h = algorithm_quaternion_to_hand((q_plus_x.scalar(), q_plus_x.x(), q_plus_x.y(), q_plus_x.z()))
    axis = q_h.vector()
    near(axis.normalized(), (0, -1, 0))

    q_plus_y = QQuaternion.fromAxisAndAngle(QVector3D(0, 1, 0), 90.0)
    q_h2 = algorithm_quaternion_to_hand((q_plus_y.scalar(), q_plus_y.x(), q_plus_y.y(), q_plus_y.z()))
    near(q_h2.vector().normalized(), (1, 0, 0))

    q_plus_z = QQuaternion.fromAxisAndAngle(QVector3D(0, 0, 1), 90.0)
    q_h3 = algorithm_quaternion_to_hand((q_plus_z.scalar(), q_plus_z.x(), q_plus_z.y(), q_plus_z.z()))
    near(q_h3.vector().normalized(), (0, 0, 1))


def test_result_is_unit_quaternion():
    q = algorithm_quaternion_to_hand((0.7, 0.3, -0.4, 0.5))
    length = math.sqrt(q.scalar() ** 2 + q.x() ** 2 + q.y() ** 2 + q.z() ** 2)
    assert abs(length - 1.0) < 1e-6


def test_c_is_rz_minus_90():
    # C 本身：绕 +Z 转 -90°，应把 +X 转到 -Y
    near(ALGORITHM_TO_HAND.rotatedVector(QVector3D(1, 0, 0)), (0, -1, 0))
    near(ALGORITHM_TO_HAND.rotatedVector(QVector3D(0, 1, 0)), (1, 0, 0))


def test_quat_to_mat4_matches_rotation():
    q = QQuaternion.fromAxisAndAngle(QVector3D(0, 0, 1), 90.0)
    m = quat_to_mat4(q)
    v = mat4_vec3(m, QVector3D(1, 0, 0))
    near(v, (0, 1, 0))


def test_trs_order():
    # M = T · R · S：先缩放，再旋转，再平移（列向量语义）
    m = trs_to_mat4(QVector3D(10, 0, 0), QQuaternion.fromAxisAndAngle(QVector3D(0, 0, 1), 90.0), QVector3D(2, 1, 1))
    v = mat4_vec3(m, QVector3D(1, 0, 0))
    near(v, (10, 2, 0))


def test_mat4_mul_associates_translation():
    t1 = trs_to_mat4(QVector3D(1, 2, 3), quat_from_hamilton((1, 0, 0, 0)), QVector3D(1, 1, 1))
    t2 = trs_to_mat4(QVector3D(4, 5, 6), quat_from_hamilton((1, 0, 0, 0)), QVector3D(1, 1, 1))
    v = mat4_vec3(mat4_mul(t1, t2), QVector3D(0, 0, 0))
    near(v, (5, 7, 9))
