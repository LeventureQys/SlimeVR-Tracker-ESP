#pragma once

#include <QString>
#include <QVector3D>

#include <array>

namespace handstudio {

// Signed permutation matrix mapping raw sensor axes onto application axes.
// out[row] = sum_col matrix[row][col] * in[col]. A valid mapping is a signed
// permutation: every row and every column contains exactly one entry of
// magnitude 1, all other entries are 0.
struct AxisRemap {
    std::array<std::array<int, 3>, 3> matrix{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

    static AxisRemap identity()
    {
        AxisRemap result;
        return result;
    }

    bool isValid(QString *reason = nullptr) const
    {
        for (int row = 0; row < 3; ++row) {
            int nonZero = 0;
            for (int col = 0; col < 3; ++col) {
                const int value = matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
                if (value != 0) {
                    if (value != 1 && value != -1) {
                        if (reason) {
                            *reason = QStringLiteral("轴映射元素必须为 -1、0 或 1");
                        }
                        return false;
                    }
                    ++nonZero;
                }
            }
            if (nonZero != 1) {
                if (reason) {
                    *reason = QStringLiteral("轴映射每行必须且只能有一个非零元素");
                }
                return false;
            }
        }
        for (int col = 0; col < 3; ++col) {
            int nonZero = 0;
            for (int row = 0; row < 3; ++row) {
                if (matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] != 0) {
                    ++nonZero;
                }
            }
            if (nonZero != 1) {
                if (reason) {
                    *reason = QStringLiteral("轴映射每列必须且只能有一个非零元素");
                }
                return false;
            }
        }
        if (reason) {
            reason->clear();
        }
        return true;
    }

    QVector3D apply(const QVector3D &input) const
    {
        const double in[3] = {double(input.x()), double(input.y()), double(input.z())};
        double out[3] = {0.0, 0.0, 0.0};
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                out[row] += double(matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)]) * in[col];
            }
        }
        return QVector3D(float(out[0]), float(out[1]), float(out[2]));
    }
};

}
