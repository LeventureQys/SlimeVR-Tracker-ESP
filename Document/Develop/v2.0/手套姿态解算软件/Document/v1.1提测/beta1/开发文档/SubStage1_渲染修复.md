# 任务书：SubStage 1 — 3D 渲染修复

- **所属 Stage**：阶段三·开发
- **依赖前置**：无（独立可执行）
- **并行状态**：可与 SubStage 2、3 并行
- **所属版本**：v1.1 beta1

---

## 1. 目标

修复 3D 手模型的两个渲染问题：
1. 消除骨骼 overlay 与 mesh 表面的 z-fight 产生的高频彩色闪烁点。
2. 增加 specular 高光，使模型表面有光泽反射感。

## 2. 修改文件

**唯一修改文件**：`archived/src/render/hand_render_widget.cpp`

## 3. 当前代码状态

### 3.1 彩色点问题

`drawSkeleton()` 方法（682-741 行）使用 `GL_PROGRAM_POINT_SIZE` + `GL_POINTS` 绘制骨骼点。这些点与 mesh 共享深度缓冲（`GL_DEPTH_TEST` 始终开启），导致 overlay 点与 mesh 表面 z-fight 产生彩色闪烁。

相关代码行：
- 714 行：`glDisable(GL_CULL_FACE);`（已有，但未禁用深度测试）
- 734 行：`overlayProgram_->setUniformValue("uPointSize", static_cast<float>(8.0 * devicePixelRatioF()));`

### 3.2 光照问题

mesh fragment shader（416-428 行）：
```glsl
vec3 normal = normalize(vNormal);
vec3 lightDirection = normalize(vec3(0.35, 0.7, 0.55));
float diffuse = max(dot(normal, lightDirection), 0.0);
float lighting = 0.28 + 0.72 * diffuse;
fragmentColor = vec4(uBaseColor.rgb * lighting, uBaseColor.a);
```
- 只有 ambient(0.28) + diffuse(0.72)，**无 specular 高光**。
- vertex shader（389-414 行）只传递 `vNormal`，不传递世界坐标。

## 4. 具体修改内容

### 4.1 修复 z-fight（drawSkeleton 方法，约 712-740 行）

**在 overlay 绘制前禁用深度测试，绘制后恢复：**

在 `drawSkeleton()` 中，找到约 714 行的 `glDisable(GL_CULL_FACE);`，在其**前面**添加：
```cpp
glDisable(GL_DEPTH_TEST);
```

在约 739 行的 `glEnable(GL_CULL_FACE);` 的**后面**添加：
```cpp
glEnable(GL_DEPTH_TEST);
```

同时将 734 行的点大小从 `8.0` 改为 `6.0`：
```cpp
overlayProgram_->setUniformValue("uPointSize", static_cast<float>(6.0 * devicePixelRatioF()));
```

### 4.2 增加 specular 高光

**修改 vertex shader**（约 389-414 行的 `meshVertexShader` 字符串）：

在 `out vec3 vNormal;` 后面添加：
```glsl
out vec3 vWorldPosition;
```

在 `gl_Position = uViewProjection * worldPosition;` 前面添加：
```glsl
vWorldPosition = worldPosition.xyz;
```

**修改 fragment shader**（约 416-428 行的 `meshFragmentShader` 字符串）：

将整个 fragment shader 替换为：
```glsl
#version 330 core
in vec3 vNormal;
in vec3 vWorldPosition;
uniform vec4 uBaseColor;
out vec4 fragmentColor;
void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDirection = normalize(vec3(0.35, 0.7, 0.55));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    vec3 viewDir = normalize(-vWorldPosition);
    vec3 halfDir = normalize(lightDirection + viewDir);
    float specular = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.3;
    float lighting = 0.25 + 0.65 * diffuse + specular;
    fragmentColor = vec4(uBaseColor.rgb * lighting, uBaseColor.a);
}
```

**注意**：`viewDir = normalize(-vWorldPosition)` 假设相机在世界原点。由于 `updateMatrices()` 中 eye 计算为 `camera_.target + direction * camera_.distance`，这不是世界原点。更准确的做法是传递相机位置作为 uniform。但作为近似（手模型在视口中心附近），这个近似可接受。如果效果不理想，可改为传递 `uCameraPosition` uniform。

## 5. 验收标准

| # | 检查项 | 通过标准 |
| --- | --- | --- |
| 1 | 编译 | 构建成功，零 warning |
| 2 | 彩色点 | 启动后手模型表面无高频彩色闪烁点 |
| 3 | 骨骼点 | 骨骼覆盖层点和线正常显示，不被 mesh 遮挡 |
| 4 | 光泽 | 手模型表面可见 specular 高光反射（手指弯曲处有亮点） |

## 6. 禁止事项

- 不得修改 `drawMeshes()` 中的矩阵计算逻辑。
- 不得修改 camera/projection 矩阵。
- 不得修改 `uploadModel()` 中的顶点数据。
- 不得改变 overlay 绘制的线/点颜色逻辑。
