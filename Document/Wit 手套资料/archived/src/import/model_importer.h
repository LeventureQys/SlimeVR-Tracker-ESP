#pragma once

#include "core/model_data.h"

namespace handrig {

class ModelImporter {
public:
    [[nodiscard]] ModelLoadResult load(const QString &filePath) const;
};

}
