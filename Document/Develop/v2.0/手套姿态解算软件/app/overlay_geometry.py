'''覆盖层几何纯函数：骨段圆柱 TRS 与单位圆柱/球顶点生成（设计文档 5.5）。'''
from __future__ import annotations

import math

from PySide6.QtGui import QQuaternion, QVector3D

BONE_RADIUS_TOP = 0.075
BONE_RADIUS_BOTTOM = 0.06
BONE_HEIGHT = 1.0
BONE_SEGMENTS = 14
JOINT_RADIUS_WRIST = 0.13
JOINT_RADIUS = 0.095
SPHERE_SEGMENTS = 16
SPHERE_RINGS = 12
Y_AXIS = QVector3D(0.0, 1.0, 0.0)


def bone_transform(start, end) -> tuple:
    '''端点 → (中点, 单位四元数 (w,x,y,z), 长度)。圆柱轴与方向向量严格平行。'''
    s = QVector3D(*start)
    e = QVector3D(*end)
    direction = e - s
    length = direction.length()
    if length < 1e-9:
        return (s, (1.0, 0.0, 0.0, 0.0), 0.0)
    q = QQuaternion.rotationTo(Y_AXIS, direction / length)
    return ((s + e) / 2.0, (q.scalar(), q.x(), q.y(), q.z()), length)


def unit_cylinder(top_radius: float, bottom_radius: float, height: float, radial_segments: int) -> tuple:
    '''沿 +Y 的单位圆柱（高 height，中心在原点）：(positions, normals, indices)。'''
    half = height / 2.0
    positions = []
    normals = []
    # 顶部圆环（含中心点法线朝上）
    positions.append((0.0, half, 0.0)); normals.append((0.0, 1.0, 0.0))
    # 底部圆环（含中心点法线朝下）
    positions.append((0.0, -half, 0.0)); normals.append((0.0, -1.0, 0.0))
    for i in range(radial_segments):
        angle = 2.0 * math.pi * i / radial_segments
        c, s = math.cos(angle), math.sin(angle)
        nx, nz = c, s
        positions.append((top_radius * c, half, top_radius * s)); normals.append((nx, 0.0, nz))
        positions.append((bottom_radius * c, -half, bottom_radius * s)); normals.append((nx, 0.0, nz))
    indices = []
    for i in range(radial_segments):
        j = (i + 1) % radial_segments
        top_i = 2 + 2 * i
        bot_i = 3 + 2 * i
        top_j = 2 + 2 * j
        bot_j = 3 + 2 * j
        indices.extend([top_i, top_j, bot_i, bot_i, top_j, bot_j])
    return positions, normals, indices


def unit_sphere(radius: float, width_segments: int, height_segments: int) -> tuple:
    '''单位球（中心在原点）：(positions, normals, indices)。'''
    positions = []
    normals = []
    for y in range(height_segments + 1):
        v = y / height_segments
        phi = v * math.pi
        sin_phi = math.sin(phi)
        cos_phi = math.cos(phi)
        for x in range(width_segments + 1):
            u = x / width_segments
            theta = u * 2.0 * math.pi
            nx, ny, nz = sin_phi * math.cos(theta), cos_phi, sin_phi * math.sin(theta)
            positions.append((radius * nx, radius * ny, radius * nz))
            normals.append((nx, ny, nz))
    indices = []
    for y in range(height_segments):
        for x in range(width_segments):
            a = y * (width_segments + 1) + x
            b = a + width_segments + 1
            indices.extend([a, b, a + 1, a + 1, b, b + 1])
    return positions, normals, indices
