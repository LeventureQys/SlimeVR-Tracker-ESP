'''OpenGL 渲染器：蒙皮左手 + 骨骼覆盖层 + 网格 + 4 灯 + 雾 + ACES（设计文档 5.4/5.5/5.6）。

契约 6.5：HandViewWidget(QOpenGLWidget)。网格以单位模型矩阵绘制（skin 矩阵已含根变换）。

GL 兼容性说明（PySide6 6.10 绑定缺陷 + Intel 驱动行为，全部实测定位）：
- PySide6 6.10 绑定损坏：GL_* 常量缺失、glVertexAttribPointer/glDrawElements/glGetShaderiv 不可用、
  setUniformValue(loc, float) 损坏（GL_INVALID_OPERATION）、setUniformValueArray 要求 numpy；
- Intel Windows 驱动强制要求绑定 VAO + VBO 后才接受 glVertexAttribPointer（否则 GL_INVALID_OPERATION，
  即使兼容 profile 亦然）；
- 故：程序用 QOpenGLShaderProgram（编译状态/链接自检）；VAO 用 QOpenGLVertexArrayObject；
  顶点属性用 ctypes glVertexAttribPointer（wglGetProcAddress）；几何展开为无索引顶点数组，
  绘制只用 glDrawArrays；缓冲用 QOpenGLBuffer；标量 uniform 走 glUniform1f；
  - 兼容 profile 3.3、samples=0（Intel 驱动 MSAA FBO 缺陷）、alpha 8。
'''
from __future__ import annotations

import ctypes
import math
import struct
import threading

from PySide6.QtCore import QTimer, Qt, Signal
from PySide6.QtGui import (
    QImage,
    QMatrix4x4,
    QOffscreenSurface,
    QOpenGLContext,
    QPainter,
    QSurfaceFormat,
    QVector3D,
)
from PySide6.QtOpenGL import (
    QOpenGLBuffer,
    QOpenGLFramebufferObject,
    QOpenGLShader,
    QOpenGLShaderProgram,
    QOpenGLVersionFunctionsFactory,
    QOpenGLVersionProfile,
    QOpenGLVertexArrayObject,
)
from PySide6.QtWidgets import QWidget

from app.camera import OrbitCamera
from app.gltf.loader import decode_accessor
from app.overlay_geometry import (
    BONE_RADIUS_BOTTOM,
    BONE_RADIUS_TOP,
    BONE_SEGMENTS,
    JOINT_RADIUS,
    JOINT_RADIUS_WRIST,
    SPHERE_RINGS,
    SPHERE_SEGMENTS,
    bone_transform,
    unit_cylinder,
    unit_sphere,
)
from app.quaternion import mat4_mul, quat_from_hamilton, quat_to_mat4, scale_mat4, translation_mat4

# ------------------------------------------------------------------ 着色器（GLSL 150 兼容）

_SKIN_VERT = '''
#version 150
in vec3 aPos;
in vec3 aNormal;
in vec4 aJoints;
in vec4 aWeights;
uniform mat4 uViewProj;
uniform mat4 uSkin[25];
out vec3 vNormal;
out vec3 vWorldPos;
void main() {
    mat4 skin = aWeights.x * uSkin[int(aJoints.x)]
              + aWeights.y * uSkin[int(aJoints.y)]
              + aWeights.z * uSkin[int(aJoints.z)]
              + aWeights.w * uSkin[int(aJoints.w)];
    vec4 world = skin * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalize(mat3(skin) * aNormal);
    gl_Position = uViewProj * world;
}
'''

_OVERLAY_VERT = '''
#version 150
in vec3 aPos;
in vec3 aNormal;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vWorldPos;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);
    gl_Position = uViewProj * world;
}
'''

_GRID_VERT = '''
#version 150
in vec3 aPos;
uniform mat4 uViewProj;
uniform mat4 uModel;
out float vDepth;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vDepth = length(world.xyz);
    gl_Position = uViewProj * world;
}
'''

_LIT_FRAG = '''
#version 150
in vec3 vNormal;
in vec3 vWorldPos;
uniform vec3 uHemSky;
uniform vec3 uHemGround;
uniform float uHemIntensity;
uniform vec3 uKeyDir;
uniform vec3 uKeyColor;
uniform float uKeyIntensity;
uniform vec3 uRimDir;
uniform vec3 uRimColor;
uniform float uRimIntensity;
uniform vec3 uPointPos;
uniform vec3 uPointColor;
uniform float uPointIntensity;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform vec3 uBaseColor;
uniform float uRoughness;
uniform float uAlpha;
uniform vec3 uCamPos;
out vec4 fragColor;

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 n = normalize(vNormal);
    vec3 v = normalize(uCamPos - vWorldPos);
    if (!gl_FrontFacing) {
        n = -n;  // 双面光照：按三角形绕序判定背面翻转（Three.js DoubleSide 同款），
                 // 避免按插值法线点积判定在轮廓线上产生随动画移动的接缝闪烁
    }
    float shininess = max(2.0, 2.0 / pow(uRoughness, 4.0) - 2.0);

    vec3 color = mix(uHemGround, uHemSky, n.y * 0.5 + 0.5) * uHemIntensity;

    vec3 lKey = normalize(uKeyDir);
    float dKey = max(dot(n, lKey), 0.0);
    vec3 hKey = normalize(lKey + v);
    color += uKeyColor * uKeyIntensity * (dKey + pow(max(dot(n, hKey), 0.0), shininess) * 0.6);

    vec3 lRim = normalize(uRimDir);
    float dRim = max(dot(n, lRim), 0.0);
    vec3 hRim = normalize(lRim + v);
    color += uRimColor * uRimIntensity * (dRim + pow(max(dot(n, hRim), 0.0), shininess) * 0.3);

    vec3 toPoint = uPointPos - vWorldPos;
    float dist = length(toPoint);
    vec3 lPoint = toPoint / max(dist, 1e-4);
    float dPoint = max(dot(n, lPoint), 0.0);
    vec3 hPoint = normalize(lPoint + v);
    float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
    color += uPointColor * uPointIntensity * attenuation * (dPoint + pow(max(dot(n, hPoint), 0.0), shininess) * 0.5);

    color *= uBaseColor;

    color = aces(color * 1.0);
    color = pow(color, vec3(1.0 / 2.2));

    float fogFactor = 1.0 - exp(-uFogDensity * uFogDensity * dist * dist);
    color = mix(color, uFogColor, fogFactor);

    fragColor = vec4(color, uAlpha);
}
'''

_GRID_FRAG = '''
#version 150
in float vDepth;
uniform vec3 uColor;
uniform float uOpacity;
uniform vec3 uFogColor;
uniform float uFogDensity;
out vec4 fragColor;
void main() {
    float fogFactor = 1.0 - exp(-uFogDensity * uFogDensity * vDepth * vDepth);
    vec3 color = mix(uColor, uFogColor, fogFactor);
    fragColor = vec4(color, uOpacity);
}
'''

# GL 枚举常量（PySide6 6.10 绑定缺失，GL 规范固定值）
GL_DEPTH_TEST = 0x0B71
GL_BLEND = 0x0BE2
GL_SRC_ALPHA = 0x0302
GL_ONE_MINUS_SRC_ALPHA = 0x0303
GL_MULTISAMPLE = 0x809D
GL_COLOR_BUFFER_BIT = 0x00004000
GL_DEPTH_BUFFER_BIT = 0x00000100
GL_CULL_FACE = 0x0B44
GL_FRONT = 0x0404
GL_BACK = 0x0405
GL_TRIANGLES = 0x0004
GL_LINES = 0x0001
GL_FLOAT = 0x1406
GL_ARRAY_BUFFER = 0x8892
GL_TRUE = 1
GL_FALSE = 0
GL_ZERO = 0
GL_ONE = 1

# 场景常量（设计文档 5.6 / web 参数对照表）
LIGHT = {
    'hem_sky': (0xc8 / 255, 0xe1 / 255, 0xff / 255),
    'hem_ground': (0x17 / 255, 0x20 / 255, 0x2b / 255),
    'hem_intensity': 2.0,
    'key_dir': (-4.5, 3.5, 7.5),
    'key_color': (0xff / 255, 0xf2 / 255, 0xdd / 255),
    'key_intensity': 4.4,
    'rim_dir': (5.5, -1.5, 4.5),
    'rim_color': (0x79 / 255, 0xaa / 255, 0xff / 255),
    'rim_intensity': 3.0,
    'point_pos': (-3.0, -3.0, 3.0),
    'point_color': (0x49 / 255, 0xd9 / 255, 0xd0 / 255),
    'point_intensity': 1.7,
    'fog_color': (0x08 / 255, 0x0d / 255, 0x15 / 255),
    'fog_density': 0.035,
}
SKIN_COLOR = (0xd9 / 255, 0xbc / 255, 0xa4 / 255)
SKIN_ROUGHNESS = 0.58
SKIN_ALPHA = 0.43
BONE_COLOR = (0xf0 / 255, 0xdf / 255, 0xbd / 255)
BONE_ROUGHNESS = 0.62
JOINT_COLOR = (0xcd / 255, 0xb2 / 255, 0x8a / 255)
JOINT_ROUGHNESS = 0.72
GRID_COLOR = (0x29 / 255, 0x43 / 255, 0x5b / 255)
GRID_OPACITY = 0.34
# Blinn-Phong 近似下按 web 端 PBR 强度原值会爆白且冷光过强，分灯标定（观感差异已记录于验收报告）：
# 暖主光权重最高，冷轮廓光/点光压低，保证皮肤呈现暖肤色。
LIGHT_SCALE = {
    'hem': 0.04,
    'key': 0.45,
    'rim': 0.03,
    'point': 0.08,
}


class _GlHybrid:
    '''混合 GL 函数视图：优先版本化 3.3 对象，缺失回退基础对象；附常量。'''

    def __init__(self, core, base) -> None:
        self._core = core
        self._base = base

    def __getattr__(self, name):
        if name.startswith('GL_'):
            return globals()[name]
        if hasattr(self._core, name):
            return getattr(self._core, name)
        if hasattr(self._base, name):
            return getattr(self._base, name)
        raise AttributeError(name)


def _row_major_to_qmatrix4(m: tuple) -> QMatrix4x4:
    '''行主序 16 元组 → QMatrix4x4（Qt 的 float* 构造即按行主序读取，直接传入）。'''
    return QMatrix4x4(*m)


def _build_mesh_vertex_data(asset) -> tuple:
    '''网格顶点：展开索引为无索引数组（14 元素/顶点：pos3 normal3 joints4 weights4）。'''
    positions = decode_accessor(asset.mesh.attributes['POSITION'])
    normals = decode_accessor(asset.mesh.attributes['NORMAL'])
    joints = decode_accessor(asset.mesh.attributes['JOINTS_0'])
    weights = decode_accessor(asset.mesh.attributes['WEIGHTS_0'])
    indices = [i[0] for i in decode_accessor(asset.mesh.indices)]
    flat = []
    for i in indices:
        p, n, j, w = positions[i], normals[i], joints[i], weights[i]
        flat.extend(p)
        flat.extend(n)
        flat.extend(j)
        flat.extend(w)
    return flat, len(indices)


def _build_grid_vertices(size: float = 10.0, divisions: int = 20) -> tuple:
    '''GridHelper(10, 20)：XZ 平面线网（模型空间 y=0），返回 (all_points, center_points)。'''
    half = size / 2.0
    step = size / divisions
    all_points = []
    center_points = []
    for i in range(divisions + 1):
        pos = -half + i * step
        all_points.extend([(pos, 0.0, -half), (pos, 0.0, half)])
        all_points.extend([(-half, 0.0, pos), (half, 0.0, pos)])
        if abs(pos) < step / 2.0:
            center_points.extend([(pos, 0.0, -half), (pos, 0.0, half)])
            center_points.extend([(-half, 0.0, pos), (half, 0.0, pos)])
    return all_points, center_points


def _make_vbo(gl, flat_floats) -> QOpenGLBuffer:
    '''顶点缓冲（float32，无索引）。'''
    buf = QOpenGLBuffer(QOpenGLBuffer.Type.VertexBuffer)
    if not buf.create():
        raise RuntimeError('顶点缓冲创建失败')
    buf.bind()
    data = struct.pack(f'<{len(flat_floats)}f', *flat_floats)
    buf.allocate(data, len(data))
    buf.release()
    return buf


def _build_program(vert_src: str, frag_src: str) -> QOpenGLShaderProgram:
    prog = QOpenGLShaderProgram()
    if not prog.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Vertex, vert_src):
        raise RuntimeError(f'顶点着色器编译失败：{prog.log()}')
    if not prog.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Fragment, frag_src):
        raise RuntimeError(f'片元着色器编译失败：{prog.log()}')
    if not prog.link():
        raise RuntimeError(f'着色器链接失败：{prog.log()}')
    return prog


_ATTRIB_POINTER_CACHE = {}


def _gl_vertex_attrib_pointer(index: int, size: int, type_: int, normalized: int, stride: int, offset: int) -> None:
    '''glVertexAttribPointer（ctypes；PySide6 6.10 绑定不可用，且 Intel 驱动要求 VAO+VBO 已绑）。'''
    if 'fn' not in _ATTRIB_POINTER_CACHE:
        opengl32 = ctypes.WinDLL('opengl32')
        wgl_get = opengl32.wglGetProcAddress
        wgl_get.restype = ctypes.c_void_p
        wgl_get.argtypes = [ctypes.c_char_p]
        addr = wgl_get(b'glVertexAttribPointer')
        if not addr:
            addr = ctypes.cast(getattr(opengl32, 'glVertexAttribPointer'), ctypes.c_void_p).value
        if not addr:
            raise RuntimeError('无法获取 glVertexAttribPointer')
        _ATTRIB_POINTER_CACHE['fn'] = ctypes.CFUNCTYPE(
            None, ctypes.c_uint, ctypes.c_int, ctypes.c_uint, ctypes.c_ubyte, ctypes.c_int, ctypes.c_void_p)(addr)
    _ATTRIB_POINTER_CACHE['fn'](index, size, type_, normalized, stride, ctypes.c_void_p(offset))


class HandViewWidget(QWidget):
    '''三维手视口。

    渲染架构：GL 渲染到离屏 FBO → toImage → QPainter 软呈现。
    原因：Intel 驱动 + PySide6 6.10 下 QOpenGLWidget 的 DWM 合成呈现实测不可用（黑/白屏），
    而普通 QPainter 绘制路径在面板上验证可靠，故手部画面走同一通道保证上屏。
    '''

    fpsChanged = Signal(float)

    def __init__(self, asset, parent=None) -> None:
        super().__init__(parent)
        self._asset = asset
        self._camera = OrbitCamera()
        self._skin_visible = True
        self._grid_visible = True
        self._pose_lock = threading.Lock()
        self._pose = None
        self._gl = None
        self._context = None
        self._surface = None
        self._fbo = None
        self._image = None
        self._frame_count = 0
        self._fps_elapsed = 0.0
        self._fps_timer = QTimer(self)
        self._fps_timer.setInterval(700)
        self._fps_timer.timeout.connect(self._emit_fps)
        self._fps_timer.start()
        self._repaint_timer = QTimer(self)
        self._repaint_timer.setInterval(16)
        self._repaint_timer.timeout.connect(self.update)
        self._repaint_timer.start()

        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent)

    # -------------------------------------------------------------- 公共契约

    def set_pose(self, result) -> None:
        with self._pose_lock:
            self._pose = result

    def reset_view(self) -> None:
        self._camera.reset()
        self.update()

    def set_skin_visible(self, visible: bool) -> None:
        self._skin_visible = bool(visible)
        self.update()

    def set_grid_visible(self, visible: bool) -> None:
        self._grid_visible = bool(visible)
        self.update()

    def is_skin_visible(self) -> bool:
        return self._skin_visible

    def is_grid_visible(self) -> bool:
        return self._grid_visible

    def framebuffer_image(self):
        '''当前渲染结果（QImage 副本），供测试与截图证据使用。'''
        if self._image is None:
            self._render_gl(max(1, self.width()), max(1, self.height()))
        return None if self._image is None else QImage(self._image)

    # -------------------------------------------------------------- 事件

    def mousePressEvent(self, event) -> None:
        self._last_mouse = event.position()

    def mouseMoveEvent(self, event) -> None:
        pos = event.position()
        dx = pos.x() - self._last_mouse.x()
        dy = pos.y() - self._last_mouse.y()
        w = max(1.0, self.width())
        h = max(1.0, self.height())
        buttons = event.buttons()
        if buttons & Qt.MouseButton.LeftButton:
            self._camera.rotate(dx, dy, w, h)
        elif buttons & Qt.MouseButton.RightButton:
            self._camera.pan(dx, dy, w, h)
        self._last_mouse = pos
        self.update()

    def wheelEvent(self, event) -> None:
        self._camera.zoom(event.angleDelta().y())
        self.update()

    # -------------------------------------------------------------- GL 初始化（手动上下文 + 离屏表面）

    def _init_gl(self) -> None:
        fmt = QSurfaceFormat()
        fmt.setRenderableType(QSurfaceFormat.RenderableType.OpenGL)
        fmt.setSamples(0)
        fmt.setVersion(3, 3)
        fmt.setProfile(QSurfaceFormat.OpenGLContextProfile.CompatibilityProfile)
        fmt.setAlphaBufferSize(8)
        fmt.setDepthBufferSize(24)

        context = QOpenGLContext()
        context.setFormat(fmt)
        if not context.create():
            raise RuntimeError('OpenGL 上下文创建失败')
        surface = QOffscreenSurface()
        surface.setFormat(fmt)
        surface.create()
        if not context.makeCurrent(surface):
            raise RuntimeError('OpenGL 上下文激活失败')
        self._context = context
        self._surface = surface

        profile = QOpenGLVersionProfile()
        profile.setVersion(3, 3)
        profile.setProfile(QSurfaceFormat.OpenGLContextProfile.CompatibilityProfile)
        core = QOpenGLVersionFunctionsFactory.get(profile, context)
        base = context.functions()
        if core is None or base is None:
            raise RuntimeError('无法获取 OpenGL 函数（目标机驱动不支持）')
        core.initializeOpenGLFunctions()
        base.initializeOpenGLFunctions()
        gl = _GlHybrid(core, base)
        self._gl = gl

        gl.glEnable(GL_DEPTH_TEST)
        gl.glEnable(GL_BLEND)
        # RGB 正常混合，alpha 通道保持不透明（GL_ZERO/GL_ONE）：
        # 否则半透明皮肤会把 FBO 的 alpha 混合成 <1，toImage() 后的图像带半透明，
        # paintEvent 的 drawImage(SourceOver) 会与上一帧残留混合 → 动画时闪烁/拖影
        gl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE)
        gl.glEnable(GL_MULTISAMPLE)
        gl.glClearColor(0x07 / 255, 0x0b / 255, 0x12 / 255, 1.0)

        self._skin_program = _build_program(_SKIN_VERT, _LIT_FRAG)
        self._overlay_program = _build_program(_OVERLAY_VERT, _LIT_FRAG)
        self._grid_program = _build_program(_GRID_VERT, _GRID_FRAG)

        # VAO：Intel Windows 驱动强制要求绑定 VAO 后才接受 glVertexAttribPointer
        self._vao = QOpenGLVertexArrayObject()
        if not self._vao.create():
            raise RuntimeError('VAO 创建失败')

        # 网格几何（无索引展开）
        flat, count = _build_mesh_vertex_data(self._asset)
        self._mesh_vert_count = count
        self._mesh_vbo = _make_vbo(gl, flat)

        cyl_pos, cyl_norm, cyl_idx = unit_cylinder(BONE_RADIUS_TOP, BONE_RADIUS_BOTTOM, 1.0, BONE_SEGMENTS)
        sphere_pos, sphere_norm, sphere_idx = unit_sphere(1.0, SPHERE_SEGMENTS, SPHERE_RINGS)
        self._cyl_vbo, self._cyl_vert_count = self._make_geometry(cyl_pos, cyl_norm, cyl_idx)
        self._sphere_vbo, self._sphere_vert_count = self._make_geometry(sphere_pos, sphere_norm, sphere_idx)

        grid_points, center_points = _build_grid_vertices(10.0, 20)
        self._grid_vbo, self._grid_vert_count = self._make_positions_vbo(grid_points)
        self._grid_center_vbo, self._grid_center_vert_count = self._make_positions_vbo(center_points)
        grid_quat = quat_from_hamilton((math.sqrt(0.5), math.sqrt(0.5), 0.0, 0.0))  # 绕 X 90°
        self._grid_model = mat4_mul(translation_mat4(QVector3D(0, 0, -1.2)), quat_to_mat4(grid_quat))

    def _make_geometry(self, positions, normals, indices) -> tuple:
        '''带索引几何 → 无索引展开（pos3 normal3 交错）。'''
        gl = self._gl
        flat = []
        for i in indices:
            flat.extend(positions[i])
            flat.extend(normals[i])
        return _make_vbo(gl, flat), len(indices)

    def _make_positions_vbo(self, points) -> tuple:
        gl = self._gl
        flat = [c for p in points for c in p]
        return _make_vbo(gl, flat), len(points)

    # -------------------------------------------------------------- 绘制

    def _set_lit_uniforms(self, prog: QOpenGLShaderProgram) -> None:
        gl = self._gl

        def loc(name: str) -> int:
            return prog.uniformLocation(name)

        def vec3(name: str, value: tuple) -> None:
            prog.setUniformValue(loc(name), value[0], value[1], value[2])

        def scalar(name: str, value: float) -> None:
            # PySide6 6.10 的 setUniformValue(loc, float) 重载损坏（GL_INVALID_OPERATION），走 glUniform1f
            gl.glUniform1f(loc(name), value)

        vec3('uHemSky', LIGHT['hem_sky'])
        vec3('uHemGround', LIGHT['hem_ground'])
        scalar('uHemIntensity', LIGHT['hem_intensity'] * LIGHT_SCALE['hem'])
        vec3('uKeyDir', LIGHT['key_dir'])
        vec3('uKeyColor', LIGHT['key_color'])
        scalar('uKeyIntensity', LIGHT['key_intensity'] * LIGHT_SCALE['key'])
        vec3('uRimDir', LIGHT['rim_dir'])
        vec3('uRimColor', LIGHT['rim_color'])
        scalar('uRimIntensity', LIGHT['rim_intensity'] * LIGHT_SCALE['rim'])
        vec3('uPointPos', LIGHT['point_pos'])
        vec3('uPointColor', LIGHT['point_color'])
        scalar('uPointIntensity', LIGHT['point_intensity'] * LIGHT_SCALE['point'])
        vec3('uFogColor', LIGHT['fog_color'])
        scalar('uFogDensity', LIGHT['fog_density'])
        eye = self._camera.position()
        prog.setUniformValue(loc('uCamPos'), eye.x(), eye.y(), eye.z())

    def _render_gl(self, width: int, height: int) -> None:
        '''GL 渲染一帧到 FBO 并读回 QImage（离屏）。'''
        if self._context is None:
            try:
                self._init_gl()
            except Exception as error:
                print(f'[hand_view] GL 初始化失败：{error}')
                return
        gl = self._gl
        if gl is None or width <= 0 or height <= 0:
            return
        self._context.makeCurrent(self._surface)
        if self._fbo is None or self._fbo.width() != width or self._fbo.height() != height:
            self._fbo = QOpenGLFramebufferObject(width, height, QOpenGLFramebufferObject.Attachment.CombinedDepthStencil)
        self._fbo.bind()

        self._frame_count += 1
        gl.glViewport(0, 0, width, height)
        gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        self._vao.bind()

        aspect = width / max(1.0, height)
        m = mat4_mul(self._camera.projection_matrix(aspect), self._camera.view_matrix())
        view_proj = _row_major_to_qmatrix4(m)

        with self._pose_lock:
            pose = self._pose

        # ---- 网格 ----
        if self._grid_visible:
            prog = self._grid_program
            prog.bind()
            prog.setUniformValue(prog.uniformLocation('uViewProj'), view_proj)
            prog.setUniformValue(prog.uniformLocation('uModel'), _row_major_to_qmatrix4(self._grid_model))
            prog.setUniformValue(prog.uniformLocation('uFogColor'), LIGHT['fog_color'][0], LIGHT['fog_color'][1], LIGHT['fog_color'][2])
            gl.glUniform1f(prog.uniformLocation('uFogDensity'), LIGHT['fog_density'])
            gl.glUniform1f(prog.uniformLocation('uOpacity'), GRID_OPACITY)
            prog.setUniformValue(prog.uniformLocation('uColor'), GRID_COLOR[0], GRID_COLOR[1], GRID_COLOR[2])
            gl.glDepthMask(GL_FALSE)  # 参考平面不写深度：避免低角度视角下网格线穿透手模产生条纹闪烁
            gl.glBindBuffer(GL_ARRAY_BUFFER, self._grid_vbo.bufferId())
            _gl_vertex_attrib_pointer(prog.attributeLocation('aPos'), 3, GL_FLOAT, GL_FALSE, 12, 0)
            gl.glEnableVertexAttribArray(prog.attributeLocation('aPos'))
            gl.glDrawArrays(GL_LINES, 0, self._grid_vert_count)
            gl.glBindBuffer(GL_ARRAY_BUFFER, self._grid_center_vbo.bufferId())
            _gl_vertex_attrib_pointer(prog.attributeLocation('aPos'), 3, GL_FLOAT, GL_FALSE, 12, 0)
            gl.glDrawArrays(GL_LINES, 0, self._grid_center_vert_count)
            gl.glDepthMask(GL_TRUE)

        # ---- 蒙皮网格 ----
        if pose is not None and self._skin_visible:
            prog = self._skin_program
            prog.bind()
            prog.setUniformValue(prog.uniformLocation('uViewProj'), view_proj)
            self._set_lit_uniforms(prog)
            prog.setUniformValue(prog.uniformLocation('uBaseColor'), SKIN_COLOR[0], SKIN_COLOR[1], SKIN_COLOR[2])
            gl.glUniform1f(prog.uniformLocation('uRoughness'), SKIN_ROUGHNESS)
            gl.glUniform1f(prog.uniformLocation('uAlpha'), SKIN_ALPHA)
            # 逐元素上传 25 个蒙皮矩阵（setUniformValueArray 在 PySide6 下要求 numpy，规避之）
            base_loc = prog.uniformLocation('uSkin')
            for idx, mat in enumerate(pose.skin_matrices):
                prog.setUniformValue(base_loc + idx, _row_major_to_qmatrix4(mat))
            gl.glDepthMask(GL_FALSE)  # 半透明皮肤不写深度，保证骨骼覆盖层可见
            gl.glDisable(GL_CULL_FACE)  # 双面绘制：镜像手模绕序不一致，剔除会导致表面缺失与闪烁
            gl.glBindBuffer(GL_ARRAY_BUFFER, self._mesh_vbo.bufferId())
            stride = 14 * 4
            _gl_vertex_attrib_pointer(prog.attributeLocation('aPos'), 3, GL_FLOAT, GL_FALSE, stride, 0)
            gl.glEnableVertexAttribArray(prog.attributeLocation('aPos'))
            _gl_vertex_attrib_pointer(prog.attributeLocation('aNormal'), 3, GL_FLOAT, GL_FALSE, stride, 12)
            gl.glEnableVertexAttribArray(prog.attributeLocation('aNormal'))
            _gl_vertex_attrib_pointer(prog.attributeLocation('aJoints'), 4, GL_FLOAT, GL_FALSE, stride, 24)
            gl.glEnableVertexAttribArray(prog.attributeLocation('aJoints'))
            _gl_vertex_attrib_pointer(prog.attributeLocation('aWeights'), 4, GL_FLOAT, GL_FALSE, stride, 40)
            gl.glEnableVertexAttribArray(prog.attributeLocation('aWeights'))
            # 单遍绘制（绕序无关）：双面光照已消除正反面亮度差，VBO 顺序固定无跳变
            gl.glDrawArrays(GL_TRIANGLES, 0, self._mesh_vert_count)
            gl.glDepthMask(GL_TRUE)
            gl.glEnable(GL_CULL_FACE)  # 覆盖层（骨骼/关节球）绕序一致，恢复背面剔除

        # ---- 覆盖层 ----
        if pose is not None:
            prog = self._overlay_program
            prog.bind()
            prog.setUniformValue(prog.uniformLocation('uViewProj'), view_proj)
            self._set_lit_uniforms(prog)
            prog.setUniformValue(prog.uniformLocation('uBaseColor'), BONE_COLOR[0], BONE_COLOR[1], BONE_COLOR[2])
            gl.glUniform1f(prog.uniformLocation('uRoughness'), BONE_ROUGHNESS)
            gl.glUniform1f(prog.uniformLocation('uAlpha'), 1.0)
            gl.glBindBuffer(GL_ARRAY_BUFFER, self._cyl_vbo.bufferId())
            _gl_vertex_attrib_pointer(prog.attributeLocation('aPos'), 3, GL_FLOAT, GL_FALSE, 24, 0)
            gl.glEnableVertexAttribArray(prog.attributeLocation('aPos'))
            _gl_vertex_attrib_pointer(prog.attributeLocation('aNormal'), 3, GL_FLOAT, GL_FALSE, 24, 12)
            gl.glEnableVertexAttribArray(prog.attributeLocation('aNormal'))
            for start_name, end_name in pose.overlay_bones:
                mid, quat, length = bone_transform(pose.overlay_positions[start_name], pose.overlay_positions[end_name])
                q = quat_from_hamilton(quat)
                scale = (1.0, 0.0, 0.0, 0.0, 0.0, length, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)
                model = mat4_mul(translation_mat4(mid), mat4_mul(quat_to_mat4(q), scale))
                prog.setUniformValue(prog.uniformLocation('uModel'), _row_major_to_qmatrix4(model))
                gl.glDrawArrays(GL_TRIANGLES, 0, self._cyl_vert_count)
            prog.setUniformValue(prog.uniformLocation('uBaseColor'), JOINT_COLOR[0], JOINT_COLOR[1], JOINT_COLOR[2])
            gl.glUniform1f(prog.uniformLocation('uRoughness'), JOINT_ROUGHNESS)
            gl.glBindBuffer(GL_ARRAY_BUFFER, self._sphere_vbo.bufferId())
            _gl_vertex_attrib_pointer(prog.attributeLocation('aPos'), 3, GL_FLOAT, GL_FALSE, 24, 0)
            _gl_vertex_attrib_pointer(prog.attributeLocation('aNormal'), 3, GL_FLOAT, GL_FALSE, 24, 12)
            for name, pos in pose.overlay_positions.items():
                radius = JOINT_RADIUS_WRIST if name == 'wrist' else JOINT_RADIUS
                model = mat4_mul(translation_mat4(QVector3D(*pos)), scale_mat4(radius))
                prog.setUniformValue(prog.uniformLocation('uModel'), _row_major_to_qmatrix4(model))
                gl.glDrawArrays(GL_TRIANGLES, 0, self._sphere_vert_count)

        self._vao.release()
        gl.glBindBuffer(GL_ARRAY_BUFFER, 0)
        self._camera.step(1.0 / 60.0)
        self._fps_elapsed += 1.0 / 60.0

        self._image = self._fbo.toImage()
        self._fbo.release()
        self._context.doneCurrent()

    # -------------------------------------------------------------- 软呈现

    def paintEvent(self, event) -> None:
        self._render_gl(max(1, self.width()), max(1, self.height()))
        painter = QPainter(self)
        if self._image is not None and not self._image.isNull():
            painter.drawImage(self.rect(), self._image)
        else:
            painter.fillRect(self.rect(), Qt.GlobalColor.black)
        painter.end()

    def _emit_fps(self) -> None:
        if self._fps_elapsed > 0:
            self.fpsChanged.emit(self._frame_count / self._fps_elapsed)
        self._frame_count = 0
        self._fps_elapsed = 0.0
