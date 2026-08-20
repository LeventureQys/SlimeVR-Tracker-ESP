'''覆盖层几何测试：圆柱 TRS 与网格拓扑。'''
from __future__ import annotations

import math

from PySide6.QtGui import QQuaternion, QVector3D

from app.overlay_geometry import bone_transform, unit_cylinder, unit_sphere


def test_bone_transform_midpoint_and_length():
    mid, quat, length = bone_transform((0, 0, 0), (0, 2, 0))
    assert abs(mid.x()) < 1e-9 and abs(mid.y() - 1.0) < 1e-9 and abs(mid.z()) < 1e-9
    assert abs(length - 2.0) < 1e-9
    q = QQuaternion(*quat)
    # +Y → +Y：单位四元数
    assert abs(q.scalar() - 1.0) < 1e-6


def test_bone_transform_axis_parallel():
    start = (1.0, -2.0, 3.0)
    end = (4.0, 2.0, 1.0)
    mid, quat, length = bone_transform(start, end)
    q = QQuaternion(*quat)
    direction = QVector3D(*(end[i] - start[i] for i in range(3))).normalized()
    # 圆柱轴（+Y）经 q 旋转后与方向平行
    axis = q.rotatedVector(QVector3D(0, 1, 0))
    cross = QVector3D.crossProduct(axis.normalized(), direction)
    assert cross.length() < 1e-6
    # 中点正确、长度正确
    assert (mid - (QVector3D(*start) + QVector3D(*end)) / 2).length() < 1e-6
    assert abs(length - (QVector3D(*end) - QVector3D(*start)).length()) < 1e-6


def test_bone_transform_degenerate():
    mid, quat, length = bone_transform((1, 1, 1), (1, 1, 1))
    assert abs(length) < 1e-9
    assert abs(QQuaternion(*quat).scalar() - 1.0) < 1e-6


def test_cylinder_geometry_valid():
    positions, normals, indices = unit_cylinder(0.075, 0.06, 1.0, 14)
    assert len(positions) == len(normals)
    assert len(positions) == 2 + 2 * 14
    assert len(indices) == 6 * 14  # 每段 2 三角形
    assert all(0 <= i < len(positions) for i in indices)
    # 法线单位
    for n in normals:
        length = math.sqrt(sum(c * c for c in n))
        assert abs(length - 1.0) < 1e-6


def test_sphere_geometry_valid():
    positions, normals, indices = unit_sphere(1.0, 16, 12)
    assert len(positions) == (16 + 1) * (12 + 1)
    assert len(indices) == 6 * 16 * 12
    assert all(0 <= i < len(positions) for i in indices)
    # 顶点均在球面
    for p in positions:
        r = math.sqrt(sum(c * c for c in p))
        assert abs(r - 1.0) < 1e-4
