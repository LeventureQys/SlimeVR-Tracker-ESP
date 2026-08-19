from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "figures" / "Fig001_单IMU交互与姿态流程.png"


def add_box(axis, x, y, width, height, text, color):
    box = FancyBboxPatch(
        (x, y), width, height,
        boxstyle="round,pad=0.018,rounding_size=0.018",
        linewidth=1.5, edgecolor="#334155", facecolor=color,
    )
    axis.add_patch(box)
    axis.text(x + width / 2, y + height / 2, text, ha="center", va="center", fontsize=10)


def add_arrow(axis, start, end, text=""):
    arrow = FancyArrowPatch(start, end, arrowstyle="-|>", mutation_scale=14,
                            linewidth=1.5, color="#475569")
    axis.add_patch(arrow)
    if text:
        axis.text((start[0] + end[0]) / 2, (start[1] + end[1]) / 2 + 0.018,
                  text, ha="center", va="bottom", fontsize=8, color="#334155")


def main():
    plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "Arial Unicode MS"]
    plt.rcParams["axes.unicode_minus"] = False
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)

    figure, axis = plt.subplots(figsize=(15, 8.5), dpi=160)
    axis.set_xlim(0, 1)
    axis.set_ylim(0, 1)
    axis.axis("off")
    axis.set_title("单个指尖 IMU：连接、临时校准与受约束整链驱动", fontsize=18, pad=18)

    top_boxes = [
        (0.03, "扫描 WT 设备"),
        (0.20, "选择并连接"),
        (0.37, "选择绑定手指"),
        (0.54, "手掌舒展\n指尖自然伸直"),
        (0.71, "临时校准\n记录 qZero"),
        (0.86, "启用驱动"),
    ]
    widths = [0.13, 0.13, 0.13, 0.13, 0.11, 0.11]
    for (x, text), width in zip(top_boxes, widths):
        add_box(axis, x, 0.78, width, 0.11, text, "#dbeafe")
    for index in range(len(top_boxes) - 1):
        add_arrow(axis, (top_boxes[index][0] + widths[index], 0.835),
                  (top_boxes[index + 1][0], 0.835))

    processing = [
        (0.04, 0.48, 0.15, "BLE 通知\n20 字节帧"),
        (0.23, 0.48, 0.15, "解析欧拉角\n生成 qCurrent"),
        (0.42, 0.48, 0.17, "相对姿态\nqDelta = inverse(qZero) * qCurrent"),
        (0.63, 0.48, 0.15, "安装坐标修正\nqTip = C * qDelta * inverse(C)"),
        (0.82, 0.48, 0.15, "屈伸/张合\n分量提取"),
    ]
    for x, y, width, text in processing:
        add_box(axis, x, y, width, 0.12, text, "#dcfce7")
    for index in range(len(processing) - 1):
        add_arrow(axis, (processing[index][0] + processing[index][2], 0.54),
                  (processing[index + 1][0], 0.54))
    add_arrow(axis, (0.915, 0.78), (0.115, 0.60), "每个有效姿态帧")

    add_box(axis, 0.16, 0.19, 0.22, 0.13, "所选手指路由\n其余 5 个逻辑槽位保持空闲", "#fef3c7")
    add_box(axis, 0.45, 0.19, 0.22, 0.13, "整链耦合 + 关节限位\n扭转锁定、骨长不变", "#fef3c7")
    add_box(axis, 0.74, 0.19, 0.20, 0.13, "更新 GPU 蒙皮\n显示状态与实时数据", "#fef3c7")
    add_arrow(axis, (0.895, 0.48), (0.27, 0.32))
    add_arrow(axis, (0.38, 0.255), (0.45, 0.255))
    add_arrow(axis, (0.67, 0.255), (0.74, 0.255))

    axis.text(0.5, 0.07,
              "安全门禁：未连接 / 未选择手指 / 未校准 / 姿态无效时不驱动；重新绑定、断开或重置均清除校准。",
              ha="center", va="center", fontsize=11, color="#991b1b",
              bbox={"boxstyle": "round,pad=0.5", "facecolor": "#fee2e2", "edgecolor": "#ef4444"})

    figure.tight_layout()
    figure.savefig(OUTPUT, bbox_inches="tight")
    plt.close(figure)


if __name__ == "__main__":
    main()
