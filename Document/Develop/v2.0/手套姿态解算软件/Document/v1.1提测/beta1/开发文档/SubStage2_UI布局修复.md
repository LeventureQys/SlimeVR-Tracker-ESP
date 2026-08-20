# 任务书：SubStage 2 — UI 布局修复

- **所属 Stage**：阶段三·开发
- **依赖前置**：无（独立可执行）
- **并行状态**：可与 SubStage 1、3 并行
- **所属版本**：v1.1 beta1

---

## 1. 目标

修复右侧设置面板显示不全、整体布局"拧巴"的问题。

## 2. 修改文件

**唯一修改文件**：`archived/src/ui/main_window.cpp`

## 3. 当前代码状态

`buildInterface()` 方法（81-231 行）：
- 83-84 行：`renderWidget_` 作为 central widget。
- 86-90 行：左侧 `treeDock`（骨骼树），`addDockWidget(Qt::LeftDockWidgetArea, treeDock)`。
- 92-156 行：右侧 `propertiesDock`，内含 `QVBoxLayout` 垂直堆叠 modeCombo + manualPanel + simulationPanel + imuPanel + stretch。
- 55 行：`resize(1420, 900)`。

**问题根因**：
- 右侧 dock 无最小宽度限制，窗口缩小时控件被截断。
- 左侧 dock 无最小宽度限制，骨骼树可能过窄。
- 无 QScrollArea 包裹，小窗口下无法滚动查看所有控件。

## 4. 具体修改内容

### 4.1 调整窗口尺寸

找到第 55 行：
```cpp
resize(1420, 900);
```
替换为：
```cpp
resize(1600, 900);
```

### 4.2 左侧 dock 最小宽度

找到约 86-90 行：
```cpp
auto *treeDock = new QDockWidget(QStringLiteral("骨骼树"), this);
boneTree_ = new QTreeWidget(treeDock);
boneTree_->setHeaderLabels({QStringLiteral("骨骼"), QStringLiteral("状态")});
treeDock->setWidget(boneTree_);
addDockWidget(Qt::LeftDockWidgetArea, treeDock);
```
在 `addDockWidget` 前添加：
```cpp
treeDock->setMinimumWidth(200);
```

### 4.3 右侧 dock 最小宽度 + QScrollArea

找到约 92-156 行的 `propertiesDock` 构建逻辑。需要：

1. 在 `addDockWidget(Qt::RightDockWidgetArea, propertiesDock);`（第 156 行）前添加：
```cpp
propertiesDock->setMinimumWidth(340);
```

2. 将 `propertiesDock->setWidget(properties);`（第 155 行）改为使用 QScrollArea 包裹：
```cpp
auto *scrollArea = new QScrollArea(propertiesDock);
scrollArea->setWidget(properties);
scrollArea->setWidgetResizable(true);
scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
scrollArea->setFrameShape(QFrame::NoFrame);
propertiesDock->setWidget(scrollArea);
```

需要在文件顶部添加 `#include <QScrollArea>` 和 `#include <QFrame>`。

### 4.4 状态栏布局保护

找到约 162-167 行：
```cpp
modelStatus_ = new QLabel(this);
inputStatus_ = new QLabel(QStringLiteral("输入：就绪"), this);
fpsStatus_ = new QLabel(QStringLiteral("FPS：--"), this);
statusBar()->addWidget(modelStatus_, 1);
statusBar()->addPermanentWidget(inputStatus_, 2);
statusBar()->addPermanentWidget(fpsStatus_);
```
在 `statusBar()->addWidget(modelStatus_, 1);` 前添加：
```cpp
modelStatus_->setMinimumWidth(120);
inputStatus_->setMinimumWidth(200);
fpsStatus_->setMinimumWidth(80);
```

## 5. 验收标准

| # | 检查项 | 通过标准 |
| --- | --- | --- |
| 1 | 编译 | 构建成功，零 warning |
| 2 | 右侧面板 | 所有控件完整可见，不截断 |
| 3 | 左侧骨骼树 | 完整显示，不被挤压 |
| 4 | 滚动 | 缩小窗口后右侧面板可滚动查看 |
| 5 | 状态栏 | 三个标签文字完整，不截断 |

## 6. 禁止事项

- 不得改变面板内的控件逻辑（信号连接、值范围等）。
- 不得改变 dock 的区域分配（左/右）。
- 不得删除任何现有控件。
