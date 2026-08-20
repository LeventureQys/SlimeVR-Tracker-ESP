'''glTF 解析器测试：结构计数、必需关节、访问器字节、契约字段。'''
from __future__ import annotations

from app.gltf.loader import decode_accessor
from app.hand_pose import REQUIRED_JOINT_NAMES


def test_asset_version_and_counts(asset):
    assert asset.version == '2.0'
    assert len(asset.nodes) == 30
    assert asset.scene_roots == [29]
    assert asset.nodes['Armature'] is not None


def test_required_joint_names_present(asset):
    assert len(REQUIRED_JOINT_NAMES) == 25
    for name in REQUIRED_JOINT_NAMES:
        assert name in asset.nodes, f'缺少关节 {name}'
        assert name in asset.node_indices


def test_skin_contract(asset):
    assert len(asset.skin_joint_names) == 25
    assert REQUIRED_JOINT_NAMES[0] == 'wrist' and asset.skin_joint_names[-1] == 'wrist'
    assert len(asset.skin_inverse_bind_matrices) == 25
    for m in asset.skin_inverse_bind_matrices:
        assert len(m) == 16
        assert m[15] == 1.0  # 仿射矩阵末元素


def test_mesh_attributes_and_indices(asset):
    attrs = asset.mesh.attributes
    for key in ('POSITION', 'NORMAL', 'TEXCOORD_0', 'JOINTS_0', 'WEIGHTS_0'):
        assert key in attrs, f'缺少属性 {key}'
    positions = decode_accessor(attrs['POSITION'])
    assert len(positions) == 1360 and all(len(p) == 3 for p in positions)
    joints = decode_accessor(attrs['JOINTS_0'])
    assert len(joints) == 1360 and all(len(j) == 4 for j in joints)
    assert all(0 <= j < 25 for row in joints for j in row)
    weights = decode_accessor(attrs['WEIGHTS_0'])
    assert len(weights) == 1360
    indices = decode_accessor(asset.mesh.indices)
    assert len(indices) == 6942 and all(0 <= i[0] < 1360 for i in indices)


def test_mesh_node_local_is_identity(asset):
    node = asset.nodes['l_handMeshNode']
    assert node.matrix is None
    assert node.translation == (0.0, 0.0, 0.0)
    assert node.rotation == (1.0, 0.0, 0.0, 0.0)
    assert node.scale == (1.0, 1.0, 1.0)


def test_armature_parents_children(asset):
    armature = asset.nodes['Armature']
    assert 25 in armature.children  # l_handMeshNode
    for idx in range(25):
        assert idx in armature.children
    for idx in range(25):
        node = [n for n in asset.nodes.values()][idx]
        assert not node.children  # 关节无子节点


def test_rotation_stored_hamilton(asset):
    # glTF 存储 [x,y,z,w]（x=-0.5, y≈0.5, z≈0.5, w=0.5），加载后应为 Hamilton (w,x,y,z)
    wrist = asset.nodes['wrist']
    w, x, y, z = wrist.rotation
    assert abs(w - 0.5) < 1e-4 and abs(x + 0.5) < 1e-4 and abs(y - 0.5) < 1e-4 and abs(z - 0.5) < 1e-4
    assert abs(w * w + x * x + y * y + z * z - 1.0) < 1e-3
