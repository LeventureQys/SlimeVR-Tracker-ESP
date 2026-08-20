#pragma once

// 25 个标准关节名与 19 条语义骨段（SubStage 5 / SubStage 4 共享契约）。
//
// 这是「解剖语义链」，与 GLB 的节点 parent（Armature 下 25 关节平铺）无关。
// 覆盖层骨段必须据此构建，不能从 Assimp/GLB 的节点 parent 画线。

#include <QString>
#include <QStringList>
#include <QVector>

namespace handstudio {

struct SemanticBoneSegment {
    QString start; // 语义关节名
    QString end;   // 语义关节名
};

// 25 个标准关节名：wrist 优先，随后 thumb / index / middle / ring / pinky。
// 与 Python REQUIRED_JOINT_NAMES 一致。
inline const QStringList &standardJointNames()
{
    static const QStringList names = {
        QStringLiteral("wrist"),
        QStringLiteral("thumb-metacarpal"),
        QStringLiteral("thumb-phalanx-proximal"),
        QStringLiteral("thumb-phalanx-distal"),
        QStringLiteral("thumb-tip"),
        QStringLiteral("index-finger-metacarpal"),
        QStringLiteral("index-finger-phalanx-proximal"),
        QStringLiteral("index-finger-phalanx-intermediate"),
        QStringLiteral("index-finger-phalanx-distal"),
        QStringLiteral("index-finger-tip"),
        QStringLiteral("middle-finger-metacarpal"),
        QStringLiteral("middle-finger-phalanx-proximal"),
        QStringLiteral("middle-finger-phalanx-intermediate"),
        QStringLiteral("middle-finger-phalanx-distal"),
        QStringLiteral("middle-finger-tip"),
        QStringLiteral("ring-finger-metacarpal"),
        QStringLiteral("ring-finger-phalanx-proximal"),
        QStringLiteral("ring-finger-phalanx-intermediate"),
        QStringLiteral("ring-finger-phalanx-distal"),
        QStringLiteral("ring-finger-tip"),
        QStringLiteral("pinky-finger-metacarpal"),
        QStringLiteral("pinky-finger-phalanx-proximal"),
        QStringLiteral("pinky-finger-phalanx-intermediate"),
        QStringLiteral("pinky-finger-phalanx-distal"),
        QStringLiteral("pinky-finger-tip"),
    };
    return names;
}

// 19 条语义骨段：拇指 3 段，其余四指各 4 段。
inline const QVector<SemanticBoneSegment> &semanticBoneSegments()
{
    static const QVector<SemanticBoneSegment> segments = {
        {QStringLiteral("thumb-metacarpal"), QStringLiteral("thumb-phalanx-proximal")},
        {QStringLiteral("thumb-phalanx-proximal"), QStringLiteral("thumb-phalanx-distal")},
        {QStringLiteral("thumb-phalanx-distal"), QStringLiteral("thumb-tip")},

        {QStringLiteral("index-finger-metacarpal"), QStringLiteral("index-finger-phalanx-proximal")},
        {QStringLiteral("index-finger-phalanx-proximal"), QStringLiteral("index-finger-phalanx-intermediate")},
        {QStringLiteral("index-finger-phalanx-intermediate"), QStringLiteral("index-finger-phalanx-distal")},
        {QStringLiteral("index-finger-phalanx-distal"), QStringLiteral("index-finger-tip")},

        {QStringLiteral("middle-finger-metacarpal"), QStringLiteral("middle-finger-phalanx-proximal")},
        {QStringLiteral("middle-finger-phalanx-proximal"), QStringLiteral("middle-finger-phalanx-intermediate")},
        {QStringLiteral("middle-finger-phalanx-intermediate"), QStringLiteral("middle-finger-phalanx-distal")},
        {QStringLiteral("middle-finger-phalanx-distal"), QStringLiteral("middle-finger-tip")},

        {QStringLiteral("ring-finger-metacarpal"), QStringLiteral("ring-finger-phalanx-proximal")},
        {QStringLiteral("ring-finger-phalanx-proximal"), QStringLiteral("ring-finger-phalanx-intermediate")},
        {QStringLiteral("ring-finger-phalanx-intermediate"), QStringLiteral("ring-finger-phalanx-distal")},
        {QStringLiteral("ring-finger-phalanx-distal"), QStringLiteral("ring-finger-tip")},

        {QStringLiteral("pinky-finger-metacarpal"), QStringLiteral("pinky-finger-phalanx-proximal")},
        {QStringLiteral("pinky-finger-phalanx-proximal"), QStringLiteral("pinky-finger-phalanx-intermediate")},
        {QStringLiteral("pinky-finger-phalanx-intermediate"), QStringLiteral("pinky-finger-phalanx-distal")},
        {QStringLiteral("pinky-finger-phalanx-distal"), QStringLiteral("pinky-finger-tip")},
    };
    return segments;
}

} // namespace handstudio
