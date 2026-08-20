'''四元数基变换与矩阵工具（Hamilton 语义，与 three.js 一致）。

基变换：C = Rz(-90°)，q_h = C ⊗ q_a ⊗ C⁻¹。
矩阵约定：全部 4×4 以 16 元组行主序表示，列向量变换 v' = M · v。
'''
from __future__ import annotations

import math

from PySide6.QtGui import QQuaternion, QVector3D

# C：算法坐标 → 左手模型显示坐标（设计文档 5.1）
ALGORITHM_TO_HAND = QQuaternion.fromAxisAndAngle(QVector3D(0.0, 0.0, 1.0), -90.0)

IDENTITY_QUAT = (1.0, 0.0, 0.0, 0.0)


def algorithm_quaternion_to_hand(wxyz) -> QQuaternion:
    '''算法 Hamilton 四元数 [w,x,y,z] → 模型显示四元数（共轭基变换，归一化）。'''
    w, x, y, z = (float(v) for v in wxyz)
    q = QQuaternion(w, x, y, z).normalized()
    return (ALGORITHM_TO_HAND * q * ALGORITHM_TO_HAND.conjugated()).normalized()


def quat_to_mat4(q: QQuaternion) -> tuple:
    '''单位四元数 → 4×4 旋转矩阵（行主序 16 元组）。'''
    w, x, y, z = q.scalar(), q.x(), q.y(), q.z()
    return (
        1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y), 0.0,
        2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x), 0.0,
        2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y), 0.0,
        0.0, 0.0, 0.0, 1.0,
    )


def translation_mat4(t: QVector3D) -> tuple:
    return (
        1.0, 0.0, 0.0, t.x(),
        0.0, 1.0, 0.0, t.y(),
        0.0, 0.0, 1.0, t.z(),
        0.0, 0.0, 0.0, 1.0,
    )


def scale_mat4(s: float) -> tuple:
    return (
        s, 0.0, 0.0, 0.0,
        0.0, s, 0.0, 0.0,
        0.0, 0.0, s, 0.0,
        0.0, 0.0, 0.0, 1.0,
    )


def trs_to_mat4(t: QVector3D, q: QQuaternion, s: QVector3D) -> tuple:
    '''M = T(t) · R(q) · S(s)（行主序；列向量语义，等价 three.js position/rotation/scale）。'''
    r = quat_to_mat4(q)
    sx, sy, sz = s.x(), s.y(), s.z()
    return (
        r[0] * sx, r[1] * sy, r[2] * sz, t.x(),
        r[4] * sx, r[5] * sy, r[6] * sz, t.y(),
        r[8] * sx, r[9] * sy, r[10] * sz, t.z(),
        0.0, 0.0, 0.0, 1.0,
    )


def mat4_mul(a: tuple, b: tuple) -> tuple:
    '''行主序 4×4 乘法 a·b。'''
    out = [0.0] * 16
    for r in range(4):
        for c in range(4):
            out[r * 4 + c] = sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4))
    return tuple(out)


def mat4_vec3(m: tuple, v: QVector3D) -> QVector3D:
    '''列向量变换（w=1）。'''
    x = m[0] * v.x() + m[1] * v.y() + m[2] * v.z() + m[3]
    y = m[4] * v.x() + m[5] * v.y() + m[6] * v.z() + m[7]
    z = m[8] * v.x() + m[9] * v.y() + m[10] * v.z() + m[11]
    return QVector3D(x, y, z)


def mat4_translation(m: tuple) -> QVector3D:
    return QVector3D(m[3], m[7], m[11])


def vec3_from_tuple(t) -> QVector3D:
    return QVector3D(float(t[0]), float(t[1]), float(t[2]))


def vec3_to_tuple(v: QVector3D) -> tuple:
    return (v.x(), v.y(), v.z())


def quat_from_hamilton(t) -> QQuaternion:
    '''(w,x,y,z) → QQuaternion。'''
    return QQuaternion(float(t[0]), float(t[1]), float(t[2]), float(t[3]))


def quat_to_hamilton(q: QQuaternion) -> tuple:
    return (q.scalar(), q.x(), q.y(), q.z())


def quat_deg_axis(axis: QVector3D, angle_rad: float) -> QQuaternion:
    '''轴角（弧度）→ 单位四元数（QQuaternion.fromAxisAndAngle 接受度）。'''
    return QQuaternion.fromAxisAndAngle(axis.normalized(), math.degrees(angle_rad))
