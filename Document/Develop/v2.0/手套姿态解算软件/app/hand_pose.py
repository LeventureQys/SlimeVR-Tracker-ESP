'''手部姿态映射：frame → HandPoseResult。

逐语句移植 web/js/hand-model.js（关节链、bend/sway→关节角、基变换、根变换、覆盖层）。
契约：设计文档 6.2 节；算法规格：设计文档 5.2/5.3/5.4/5.5 节。
'''
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Mapping, Sequence

from PySide6.QtGui import QQuaternion, QVector3D

from app.gltf.loader import GltfAsset, decode_accessor
from app.quaternion import (
    algorithm_quaternion_to_hand,
    mat4_mul,
    mat4_translation,
    mat4_vec3,
    quat_from_hamilton,
    quat_to_mat4,
    scale_mat4,
    translation_mat4,
    trs_to_mat4,
    vec3_from_tuple,
    vec3_to_tuple,
)

FINGER_ORDER = ('thumb', 'index', 'middle', 'ring', 'little')
MODEL_PREFIX = {
    'thumb': 'thumb',
    'index': 'index-finger',
    'middle': 'middle-finger',
    'ring': 'ring-finger',
    'little': 'pinky-finger',
}
FULL_FIST_BEND_DEG = {'thumb': 85.0, 'index': 155.0, 'middle': 160.0, 'ring': 120.0, 'little': 145.0}
JOINT_BENDS_DEG = {'thumb': (38.0, 52.0, 62.0), 'long': (75.0, 100.0, 55.0)}
SWAY_CLAMP_DEG = 30.0
DISPLAY_MAX_DIM = 7.05
CENTER_OFFSET_Y = 0.35

REQUIRED_JOINT_NAMES = ['wrist']
for _finger in FINGER_ORDER:
    _prefix = MODEL_PREFIX[_finger]
    REQUIRED_JOINT_NAMES.append(f'{_prefix}-metacarpal')
    REQUIRED_JOINT_NAMES.append(f'{_prefix}-phalanx-proximal')
    if _finger != 'thumb':
        REQUIRED_JOINT_NAMES.append(f'{_prefix}-phalanx-intermediate')
    REQUIRED_JOINT_NAMES.append(f'{_prefix}-phalanx-distal')
    REQUIRED_JOINT_NAMES.append(f'{_prefix}-tip')


@dataclass
class HandPoseResult:
    skin_matrices: list          # 25×16 行主序，jointWorld × inverseBind（顺序 = skin_joint_names）
    overlay_positions: dict      # 25 关节 scene 空间位置（含根变换与腕姿态）
    overlay_bones: list          # 19 条骨段 (start_name, end_name)
    root_transform: tuple        # 16 行主序：总根变换 R(腕姿) · 固定变换


class HandPoseModel:
    '''由 GltfAsset 构建解剖链，输出蒙皮矩阵与覆盖层几何。'''

    def __init__(self, asset: GltfAsset) -> None:
        self._asset = asset
        for name in REQUIRED_JOINT_NAMES:
            if name not in asset.nodes:
                raise ValueError(f'真实手模型缺少标准关节：{name}')
        if len(asset.skin_joint_names) != 25:
            raise ValueError(f'skin 关节数应为 25，实得 {len(asset.skin_joint_names)}')

        self._rest_positions: dict[str, QVector3D] = {}
        self._rest_quats: dict[str, QQuaternion] = {}
        self._rest_scales: dict[str, QVector3D] = {}
        for name in REQUIRED_JOINT_NAMES:
            node = asset.nodes[name]
            self._rest_positions[name] = vec3_from_tuple(node.translation)
            self._rest_quats[name] = quat_from_hamilton(node.rotation).normalized()
            self._rest_scales[name] = vec3_from_tuple(node.scale)

        self._posed_positions = dict(self._rest_positions)
        self._posed_quats = dict(self._rest_quats)

        # 关节链（设计文档 5.3）
        self._chains: dict[str, dict] = {}
        for finger in FINGER_ORDER:
            prefix = MODEL_PREFIX[finger]
            names = [f'{prefix}-metacarpal', f'{prefix}-phalanx-proximal']
            if finger != 'thumb':
                names.append(f'{prefix}-phalanx-intermediate')
            names += [f'{prefix}-phalanx-distal', f'{prefix}-tip']
            pivot_index = 0 if finger == 'thumb' else 1
            direction = (self._rest_positions[names[pivot_index + 1]] - self._rest_positions[names[pivot_index]]).normalized()
            self._chains[finger] = {'names': names, 'pivot_index': pivot_index, 'direction': direction}

        across = (self._rest_positions['pinky-finger-metacarpal'] - self._rest_positions['index-finger-metacarpal']).normalized()
        along = (self._rest_positions['middle-finger-phalanx-proximal'] - self._rest_positions['middle-finger-metacarpal']).normalized()
        self._palm_normal = QVector3D.crossProduct(across, along).normalized()

        self._overlay_bones = []
        for finger in FINGER_ORDER:
            names = self._chains[finger]['names']
            for i in range(len(names) - 1):
                self._overlay_bones.append((names[i], names[i + 1]))

        # 父子关系（节点索引层面）
        self._nodes_in_order = list(asset.nodes.keys())
        self._parent_of: dict[int, int] = {}
        for idx, node in enumerate(asset.nodes.values()):
            for child in node.children:
                self._parent_of[child] = idx

        # 各节点局部矩阵（绑定/静止姿态）
        self._local_matrices: dict[str, tuple] = {}
        for name, node in asset.nodes.items():
            if node.matrix is not None:
                self._local_matrices[name] = node.matrix
            else:
                self._local_matrices[name] = trs_to_mat4(
                    vec3_from_tuple(node.translation),
                    quat_from_hamilton(node.rotation).normalized(),
                    vec3_from_tuple(node.scale),
                )

        # 固定根变换（设计文档 5.2）：R = Rz(π) ⊗ Ry(-π/2)，缩放 7.05/maxdim，居中 +0.35y
        fingers_up = QQuaternion.fromAxisAndAngle(QVector3D(0.0, 0.0, 1.0), 180.0)
        face_camera = QQuaternion.fromAxisAndAngle(QVector3D(0.0, 1.0, 0.0), -90.0)
        fixed_rotation = (fingers_up * face_camera).normalized()
        positions = decode_accessor(asset.mesh.attributes['POSITION'])
        rotated = [fixed_rotation.rotatedVector(QVector3D(*p)) for p in positions]
        max_dim = max(
            max(v.x() for v in rotated) - min(v.x() for v in rotated),
            max(v.y() for v in rotated) - min(v.y() for v in rotated),
            max(v.z() for v in rotated) - min(v.z() for v in rotated),
        )
        scale = DISPLAY_MAX_DIM / max_dim if max_dim > 0 else 1.0
        scaled = [v * scale for v in rotated]
        center = QVector3D(
            (max(v.x() for v in scaled) + min(v.x() for v in scaled)) / 2.0,
            (max(v.y() for v in scaled) + min(v.y() for v in scaled)) / 2.0,
            (max(v.z() for v in scaled) + min(v.z() for v in scaled)) / 2.0,
        )
        offset = QVector3D(-center.x(), -center.y() + CENTER_OFFSET_Y, -center.z())
        self.fixed_transform = mat4_mul(translation_mat4(offset), mat4_mul(quat_to_mat4(fixed_rotation), scale_mat4(scale)))

        # 逆绑定矩阵（GLB 原始数据，行主序）
        self._inverse_bind = asset.skin_inverse_bind_matrices
        self._joint_index_by_name = {name: i for i, name in enumerate(asset.skin_joint_names)}

        self._current_result = self._compute_result()

    # ------------------------------------------------------------------ 公共

    def apply_pose(self, wrist_wxyz: Sequence[float], fingers: Mapping[str, Mapping[str, float]]) -> HandPoseResult:
        '''应用一帧：恢复绑定姿态 → 逐指 bend/sway → 根四元数（基变换后）。'''
        self._restore_bind_pose()
        for finger in FINGER_ORDER:
            angles = fingers.get(finger) or {}
            bend = float(angles.get('bendDeg', 0.0) or 0.0)
            sway = float(angles.get('swayDeg', 0.0) or 0.0)
            self._pose_finger(finger, bend, sway)
        wrist_q = algorithm_quaternion_to_hand(wrist_wxyz)
        self._current_result = self._compute_result(wrist_q)
        return self._current_result

    def reset_pose(self) -> HandPoseResult:
        zero = {f: {'bendDeg': 0.0, 'swayDeg': 0.0} for f in FINGER_ORDER}
        return self.apply_pose((1.0, 0.0, 0.0, 0.0), zero)

    @property
    def rest_positions(self) -> dict:
        return {name: vec3_to_tuple(pos) for name, pos in self._rest_positions.items()}

    @property
    def chains(self) -> dict:
        return {k: dict(v) for k, v in self._chains.items()}

    @property
    def palm_normal(self) -> QVector3D:
        return QVector3D(self._palm_normal)

    # ------------------------------------------------------------------ 内部

    def _restore_bind_pose(self) -> None:
        self._posed_positions = dict(self._rest_positions)
        self._posed_quats = dict(self._rest_quats)

    def _pose_finger(self, finger: str, bend_deg: float, sway_deg: float) -> None:
        chain = self._chains[finger]
        completion = min(1.0, max(0.0, bend_deg / FULL_FIST_BEND_DEG[finger]))
        sway = math.radians(min(SWAY_CLAMP_DEG, max(-SWAY_CLAMP_DEG, sway_deg)))
        joint_bends_deg = JOINT_BENDS_DEG['thumb' if finger == 'thumb' else 'long']
        bend_sign = 1.0  # 正值 = 向掌心弯曲
        joint_bends = [bend_sign * math.radians(angle * completion) for angle in joint_bends_deg]

        long_finger_axis = QVector3D.crossProduct(chain['direction'], self._palm_normal).normalized()
        if finger == 'thumb':
            bend_axis = QVector3D(self._palm_normal)
            sway_axis = long_finger_axis
        else:
            bend_axis = long_finger_axis
            sway_axis = QVector3D(self._palm_normal)

        sway_rotation = QQuaternion.fromAxisAndAngle(sway_axis, math.degrees(sway))
        first_bend_axis = sway_rotation.rotatedVector(bend_axis)
        first_bend = QQuaternion.fromAxisAndAngle(first_bend_axis, math.degrees(joint_bends[0]))
        accumulated = (first_bend * sway_rotation).normalized()

        names = chain['names']
        pivot_index = chain['pivot_index']
        pivot_name = names[pivot_index]
        self._posed_quats[pivot_name] = (accumulated * self._rest_quats[pivot_name]).normalized()
        previous_position = QVector3D(self._rest_positions[pivot_name])

        for index in range(pivot_index + 1, len(names)):
            name = names[index]
            previous_name = names[index - 1]
            rest_vector = self._rest_positions[name] - self._rest_positions[previous_name]
            position = previous_position + accumulated.rotatedVector(rest_vector)
            self._posed_positions[name] = position
            self._posed_quats[name] = (accumulated * self._rest_quats[name]).normalized()
            previous_position = position

            weight_index = index - pivot_index
            if weight_index < len(joint_bends):
                current_axis = accumulated.rotatedVector(bend_axis)
                next_bend = QQuaternion.fromAxisAndAngle(current_axis, math.degrees(joint_bends[weight_index]))
                accumulated = (next_bend * accumulated).normalized()

    def _node_local_posed(self, name: str) -> tuple:
        '''当前姿态下的节点局部矩阵（关节用 posed，其余用绑定值）。'''
        if name in self._posed_positions:
            return trs_to_mat4(self._posed_positions[name], self._posed_quats[name], self._rest_scales[name])
        return self._local_matrices[name]

    def _joint_world_matrices(self, root: tuple) -> dict:
        '''所有节点的 world 矩阵（父子链递推，含根变换）。'''
        worlds: dict[str, tuple] = {}

        def walk(idx: int, parent_world: tuple) -> None:
            name = self._nodes_in_order[idx]
            local = self._node_local_posed(name)
            worlds[name] = mat4_mul(parent_world, local)
            for child in self._asset.nodes[name].children:
                walk(child, worlds[name])

        for root_idx in self._asset.scene_roots:
            walk(root_idx, root)
        return worlds

    def _compute_result(self, wrist_q: QQuaternion | None = None) -> HandPoseResult:
        root = self.fixed_transform if wrist_q is None else mat4_mul(quat_to_mat4(wrist_q), self.fixed_transform)
        worlds = self._joint_world_matrices(root)
        skin_matrices = []
        for name in self._asset.skin_joint_names:
            joint_world = worlds[name]
            inv = self._inverse_bind[self._joint_index_by_name[name]]
            skin_matrices.append(mat4_mul(joint_world, inv))
        overlay = {name: vec3_to_tuple(mat4_translation(worlds[name])) for name in REQUIRED_JOINT_NAMES}
        return HandPoseResult(
            skin_matrices=skin_matrices,
            overlay_positions=overlay,
            overlay_bones=list(self._overlay_bones),
            root_transform=root,
        )
