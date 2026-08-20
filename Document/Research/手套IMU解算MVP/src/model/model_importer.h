#pragma once

#include "model/model_data.h"

namespace handstudio {

// GLB/glTF2 模型导入器（自包含解析器，无 Python、无 Assimp 运行时依赖）。
//
// 说明（SubStage 5 Assimp 决策门）：
//   任务书首选 Assimp 5.4.3，但本会话沙箱阻断 TLS 出网且本地 Assimp 缓存为空，
//   无法获取 Assimp 源码。依据任务书第 6 节「允许替换解析器，但不得改变
//   RiggedModel 和渲染接口」，此处实现自包含 glTF2/GLB 解析器作为替换路径，
//   行为与 Python 参考 loader.py/hand_pose.py 完全一致。
class ModelImporter {
public:
    [[nodiscard]] ModelLoadResult load(const QString &filePath) const;
};

} // namespace handstudio
