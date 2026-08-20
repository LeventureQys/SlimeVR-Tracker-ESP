'''glTF2（GLB 容器）解析：generic-hand-left.glb → GltfAsset。

契约：设计文档 6.1 节（GltfNode/GltfBufferAccessor/GltfPrimitive/GltfAsset）。
'''
from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

GLB_MAGIC = 0x46546C67      # 'glTF'
JSON_CHUNK_TYPE = 0x4E4F534A  # 'JSON'
BIN_CHUNK_TYPE = 0x004E4942   # 'BIN\0'

COMPONENT_FORMAT = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5126: 'f'}
TYPE_ELEMENTS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


@dataclass
class GltfNode:
    name: str
    children: list = field(default_factory=list)
    translation: tuple = (0.0, 0.0, 0.0)
    rotation: tuple = (1.0, 0.0, 0.0, 0.0)  # Hamilton (w,x,y,z)
    scale: tuple = (1.0, 1.0, 1.0)
    matrix: tuple | None = None             # 行主序 16 元组（源 JSON 为列主序，已转置）


@dataclass
class GltfBufferAccessor:
    data: bytes
    component_type: int
    count: int
    type: str
    byte_stride: int = 0


@dataclass
class GltfPrimitive:
    attributes: dict
    indices: GltfBufferAccessor
    mode: int = 4  # TRIANGLES


@dataclass
class GltfAsset:
    nodes: dict
    node_indices: dict
    scene_roots: list
    mesh: GltfPrimitive
    skin_joint_names: list
    skin_inverse_bind_matrices: list
    material_base_color: tuple
    version: str = '2.0'


def _decode_typed(data: bytes, component_type: int, count: int) -> tuple:
    '''按 component_type 解码 count 个标量。'''
    fmt = COMPONENT_FORMAT[component_type]
    return struct.unpack_from(f'<{count}{fmt}', data, 0)


def _accessor_elements(acc: GltfBufferAccessor, element_count: int):
    '''按 stride 逐元素解码（支持紧凑与带 stride 两种布局）。返回 float 元组列表。'''
    fmt = COMPONENT_FORMAT[acc.component_type]
    if acc.component_type == 5126:
        convert = float
    else:
        convert = int
    size = struct.calcsize(fmt)
    stride = acc.byte_stride or size * element_count
    out = []
    for i in range(acc.count):
        base = i * stride
        out.append(tuple(convert(v) for v in _decode_typed(acc.data[base:base + size * element_count], acc.component_type, element_count)))
    return out


def decode_accessor(acc: GltfBufferAccessor) -> list:
    '''解码访问器为元素元组列表；MAT4 转置为行主序。'''
    n = TYPE_ELEMENTS[acc.type]
    elements = _accessor_elements(acc, n)
    if acc.type == 'MAT4':
        return [tuple(elements[i][c * 4 + r] for r in range(4) for c in range(4)) for i in range(acc.count)]
    return elements


class GltfLoader:
    '''GLB 文件解析器；仅支持本资产形态（单场景/单网格/单 primitive/单 skin）。'''

    def __init__(self, path: str | Path) -> None:
        self._path = Path(path)

    def load(self) -> GltfAsset:
        raw = self._path.read_bytes()
        json_doc, bin_data = self._split_glb(raw)
        return self._build_asset(json_doc, bin_data)

    @staticmethod
    def _split_glb(raw: bytes) -> tuple:
        magic, version, _length = struct.unpack_from('<III', raw, 0)
        if magic != GLB_MAGIC:
            raise ValueError(f'非 GLB 文件（magic={magic:#x}）：{magic:#x}')
        if version != 2:
            raise ValueError(f'不支持的 glTF 版本：{version}')
        json_doc = None
        bin_data = None
        offset = 12
        while offset + 8 <= len(raw):
            chunk_len, chunk_type = struct.unpack_from('<II', raw, offset)
            chunk = raw[offset + 8:offset + 8 + chunk_len]
            if chunk_type == JSON_CHUNK_TYPE:
                json_doc = json.loads(chunk.decode('utf-8'))
            elif chunk_type == BIN_CHUNK_TYPE:
                bin_data = chunk
            offset += 8 + chunk_len
        if json_doc is None:
            raise ValueError('GLB 缺少 JSON chunk')
        return json_doc, bin_data or b''

    def _build_asset(self, doc: dict, bin_data: bytes) -> GltfAsset:
        buffer_views = doc.get('bufferViews', [])
        accessors_doc = doc.get('accessors', [])

        def accessor(index: int) -> GltfBufferAccessor:
            acc = accessors_doc[index]
            bv = buffer_views[acc['bufferView']]
            start = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
            length = _accessor_byte_length(acc, bv)
            data = bin_data[start:start + length]
            if len(data) < length:
                raise ValueError(f'accessor {index} 越界：需要 {length} 字节，实得 {len(data)}')
            return GltfBufferAccessor(
                data=data,
                component_type=acc['componentType'],
                count=acc['count'],
                type=acc['type'],
                byte_stride=bv.get('byteStride', 0),
            )

        nodes: dict[str, GltfNode] = {}
        node_indices: dict[str, int] = {}
        for idx, n in enumerate(doc.get('nodes', [])):
            name = n.get('name', '')
            has_matrix = 'matrix' in n
            has_trs = any(k in n for k in ('translation', 'rotation', 'scale'))
            if has_matrix and has_trs:
                raise ValueError(f'节点 {name} 同时包含 matrix 与 TRS（glTF 规范禁止）')
            matrix = None
            if has_matrix:
                column = n['matrix']
                matrix = tuple(column[r * 4 + c] for r in range(4) for c in range(4))  # 转置为行主序
            rotation = (1.0, 0.0, 0.0, 0.0)
            if 'rotation' in n:
                r = tuple(n['rotation'])  # glTF [x,y,z,w] → Hamilton (w,x,y,z)
                rotation = (r[3], r[0], r[1], r[2])
            nodes[name] = GltfNode(
                name=name,
                children=list(n.get('children', [])),
                translation=tuple(n['translation']) if 'translation' in n else (0.0, 0.0, 0.0),
                rotation=rotation,
                scale=tuple(n['scale']) if 'scale' in n else (1.0, 1.0, 1.0),
                matrix=matrix,
            )
            node_indices[name] = idx

        meshes = doc.get('meshes', [])
        if len(meshes) != 1:
            raise ValueError(f'仅支持单网格资产，实得 {len(meshes)} 个网格')
        primitives = meshes[0].get('primitives', [])
        if len(primitives) != 1:
            raise ValueError(f'仅支持单 primitive 网格，实得 {len(primitives)} 个')
        prim = primitives[0]
        attributes = {k: accessor(v) for k, v in prim.get('attributes', {}).items()}
        for required in ('POSITION', 'NORMAL'):
            if required not in attributes:
                raise ValueError(f'网格缺少 {required} 属性')
        if 'indices' not in prim:
            raise ValueError('网格缺少索引')
        mesh = GltfPrimitive(attributes=attributes, indices=accessor(prim['indices']), mode=prim.get('mode', 4))

        skins = doc.get('skins', [])
        if not skins:
            raise ValueError('资产缺少 skin（无法蒙皮）')
        skin = skins[0]
        joint_indices = skin.get('joints', [])
        if len(joint_indices) != 25:
            raise ValueError(f'skin.joints 应为 25 个关节，实得 {len(joint_indices)}')
        skin_joint_names = [doc['nodes'][i].get('name', '') for i in joint_indices]
        ibm = accessor(skin['inverseBindMatrices'])
        inverse_bind = decode_accessor(ibm)  # 25 × 16 行主序

        materials = doc.get('materials', [])
        base_color = (1.0, 1.0, 1.0, 1.0)
        if materials:
            base_color = tuple(materials[0].get('pbrMetallicRoughness', {}).get('baseColorFactor', base_color))

        scenes = doc.get('scenes', [])
        scene_roots = list(scenes[0].get('nodes', [])) if scenes else [0]

        return GltfAsset(
            nodes=nodes,
            node_indices=node_indices,
            scene_roots=scene_roots,
            mesh=mesh,
            skin_joint_names=skin_joint_names,
            skin_inverse_bind_matrices=inverse_bind,
            material_base_color=base_color,
            version='2.0',
        )


def _accessor_byte_length(acc: dict, bv: dict) -> int:
    from struct import calcsize
    fmt = COMPONENT_FORMAT[acc['componentType']]
    n = TYPE_ELEMENTS[acc['type']]
    element = calcsize(fmt) * n
    stride = bv.get('byteStride', 0)
    if stride and stride > element:
        return stride * (acc['count'] - 1) + element
    return element * acc['count']
