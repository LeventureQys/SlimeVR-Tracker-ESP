'''手部姿态映射测试：bind 不变性（硬门禁）、关节角、钳制、确定性、覆盖层。'''
from __future__ import annotations

import math

import pytest
from PySide6.QtGui import QQuaternion, QVector3D

from app.gltf.loader import decode_accessor
from app.hand_pose import FULL_FIST_BEND_DEG, HandPoseModel
from app.quaternion import mat4_vec3, quat_from_hamilton, vec3_from_tuple

FINGERS_ZERO = {f: {'bendDeg': 0.0, 'swayDeg': 0.0} for f in ('thumb', 'index', 'middle', 'ring', 'little')}


def distance(a, b) -> float:
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5


def test_required_names_and_chains(pose_model: HandPoseModel):
    chains = pose_model.chains
    assert set(chains) == {'thumb', 'index', 'middle', 'ring', 'little'}
    assert len(chains['thumb']['names']) == 4 and chains['thumb']['pivot_index'] == 0
    for f in ('index', 'middle', 'ring', 'little'):
        assert len(chains[f]['names']) == 5 and chains[f]['pivot_index'] == 1


def test_bind_pose_skinning_invariance(asset, pose_model: HandPoseModel):
    '''硬门禁：bind 姿态下 Σ w_j·skinMatrix_j·v 必须精确还原为根变换后的原始顶点。'''
    result = pose_model.reset_pose()
    positions = decode_accessor(asset.mesh.attributes['POSITION'])
    joints = decode_accessor(asset.mesh.attributes['JOINTS_0'])
    weights = decode_accessor(asset.mesh.attributes['WEIGHTS_0'])
    max_err = 0.0
    for v, js, ws in zip(positions, joints, weights):
        p = QVector3D(*v)
        acc = QVector3D(0, 0, 0)
        for j, w in zip(js, ws):
            if w == 0.0:
                continue
            acc += mat4_vec3(result.skin_matrices[j], p) * w
        expected = mat4_vec3(result.root_transform, p)
        err = (acc - expected).length()
        max_err = max(max_err, err)
    assert max_err < 1e-4, f'bind 不变性失败，最大误差 {max_err}'


def test_bind_overlay_positions_match_rest(pose_model: HandPoseModel):
    '''bind 姿态覆盖层关节位置 == 根变换后的 rest 位置。'''
    result = pose_model.reset_pose()
    for name, rest in pose_model.rest_positions.items():
        expected = mat4_vec3(result.root_transform, vec3_from_tuple(rest))
        got = result.overlay_positions[name]
        assert distance(got, (expected.x(), expected.y(), expected.z())) < 1e-4, name


def test_root_transform_centers_model(pose_model: HandPoseModel):
    '''固定根变换：关节群位于原点附近，跨度不超过 7.05（设计文档 5.2）。'''
    result = pose_model.reset_pose()
    pts = [QVector3D(*p) for p in result.overlay_positions.values()]
    center = sum(pts, QVector3D(0, 0, 0)) / len(pts)
    assert abs(center.x()) < 1.5 and abs(center.z()) < 1.5
    assert abs(center.y() - 0.35) < 1.5
    span = max(
        max(p.x() for p in pts) - min(p.x() for p in pts),
        max(p.y() for p in pts) - min(p.y() for p in pts),
        max(p.z() for p in pts) - min(p.z() for p in pts),
    )
    assert 3.0 < span <= 7.06


def test_full_fist_joint_angles(pose_model: HandPoseModel):
    '''满握拳：拇指链发生显著运动，且各骨段长度保持刚性（长度守恒）。'''
    result = pose_model.apply_pose((1.0, 0.0, 0.0, 0.0), {
        'thumb': {'bendDeg': FULL_FIST_BEND_DEG['thumb'], 'swayDeg': 0.0},
        'index': {'bendDeg': 0.0, 'swayDeg': 0.0},
        'middle': {'bendDeg': 0.0, 'swayDeg': 0.0},
        'ring': {'bendDeg': 0.0, 'swayDeg': 0.0},
        'little': {'bendDeg': 0.0, 'swayDeg': 0.0},
    })
    bind = pose_model.reset_pose()
    chain = ['thumb-metacarpal', 'thumb-phalanx-proximal', 'thumb-phalanx-distal', 'thumb-tip']
    moved = 0.0
    for start, end in zip(chain, chain[1:]):
        d_bind = distance(bind.overlay_positions[start], bind.overlay_positions[end])
        d_fist = distance(result.overlay_positions[start], result.overlay_positions[end])
        assert abs(d_bind - d_fist) < 1e-4, f'{start}-{end} 骨长不守恒'
        moved += distance(bind.overlay_positions[end], result.overlay_positions[end])
    assert moved > 0.5  # 满握拳必须产生显著关节位移


def test_long_finger_full_fist_curls(pose_model: HandPoseModel):
    '''长指满握拳：指尖-掌骨距离显著缩短；半弯曲介于中间。'''
    bind = pose_model.reset_pose()

    def apply(index_bend):
        angles = dict(FINGERS_ZERO)
        angles['index'] = {'bendDeg': index_bend, 'swayDeg': 0.0}
        return pose_model.apply_pose((1.0, 0.0, 0.0, 0.0), angles)

    d0 = distance(bind.overlay_positions['index-finger-tip'], bind.overlay_positions['index-finger-metacarpal'])
    full = FULL_FIST_BEND_DEG['index']
    d_half = distance(apply(full * 0.5).overlay_positions['index-finger-tip'], apply(full * 0.5).overlay_positions['index-finger-metacarpal'])
    d_full = distance(apply(full).overlay_positions['index-finger-tip'], apply(full).overlay_positions['index-finger-metacarpal'])
    assert d_full < d_half < d0


def test_bend_completion_clamped(pose_model: HandPoseModel):
    '''bendDeg 超过满握拳角度时完成度钳制为 1：结果与满握拳一致。'''
    full = FULL_FIST_BEND_DEG['middle']
    angles_a = dict(FINGERS_ZERO); angles_a['middle'] = {'bendDeg': full, 'swayDeg': 0.0}
    angles_b = dict(FINGERS_ZERO); angles_b['middle'] = {'bendDeg': full + 100.0, 'swayDeg': 0.0}
    ra = pose_model.apply_pose((1.0, 0.0, 0.0, 0.0), angles_a)
    rb = pose_model.apply_pose((1.0, 0.0, 0.0, 0.0), angles_b)
    for ma, mb in zip(ra.skin_matrices, rb.skin_matrices):
        for x, y in zip(ma, mb):
            assert abs(x - y) < 1e-6


def test_sway_clamped_to_30(pose_model: HandPoseModel):
    '''sway ±30° 钳制：45° 与 30° 结果一致，-45° 与 -30° 一致。'''
    def apply(sway):
        angles = dict(FINGERS_ZERO)
        angles['ring'] = {'bendDeg': 0.0, 'swayDeg': sway}
        return pose_model.apply_pose((1.0, 0.0, 0.0, 0.0), angles)
    r30, r45 = apply(30.0), apply(45.0)
    for a, b in zip(r30.skin_matrices, r45.skin_matrices):
        for x, y in zip(a, b):
            assert abs(x - y) < 1e-6


def test_repeated_apply_deterministic(pose_model: HandPoseModel):
    angles = {f: {'bendDeg': 40.0 + i * 12.0, 'swayDeg': -10.0 + i * 6.0} for i, f in enumerate(('thumb', 'index', 'middle', 'ring', 'little'))}
    r1 = pose_model.apply_pose((0.9, 0.2, -0.1, 0.3), angles)
    r2 = pose_model.apply_pose((0.9, 0.2, -0.1, 0.3), angles)
    for a, b in zip(r1.skin_matrices, r2.skin_matrices):
        for x, y in zip(a, b):
            assert abs(x - y) < 1e-9


def test_wrist_base_change_in_root_transform(pose_model: HandPoseModel):
    '''腕绕算法 +X 转 90° → 根变换把固定变换后的 +X 方向转到 -Y（X→-Y 基变换）。'''
    from app.quaternion import algorithm_quaternion_to_hand, mat4_mul, mat4_vec3
    import math as _m
    q_wrist = QQuaternion.fromAxisAndAngle(QVector3D(1, 0, 0), 90.0)
    result = pose_model.apply_pose((q_wrist.scalar(), q_wrist.x(), q_wrist.y(), q_wrist.z()), FINGERS_ZERO)
    fixed = pose_model.fixed_transform
    v_fixed = mat4_vec3(fixed, QVector3D(1, 0, 0))
    v_root = mat4_vec3(result.root_transform, QVector3D(1, 0, 0))
    q_h = algorithm_quaternion_to_hand((q_wrist.scalar(), q_wrist.x(), q_wrist.y(), q_wrist.z()))
    expected = q_h.rotatedVector(v_fixed)
    assert (v_root - expected).length() < 1e-5


def test_overlay_bones_count_and_names(pose_model: HandPoseModel):
    result = pose_model.reset_pose()
    assert len(result.overlay_bones) == 19
    names = {n for edge in result.overlay_bones for n in edge}
    assert len(names) == 24  # 25 关节中腕部无骨段，仅作关节球标记
    assert 'wrist' not in names
