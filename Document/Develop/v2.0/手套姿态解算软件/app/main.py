'''桌面应用入口：QLockFile 单实例、深色主题、CLI 证据参数（--demo/--screenshot/--quit-after）。'''
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PySide6.QtCore import QDir, QLockFile, QPoint, QTimer
from PySide6.QtGui import QGuiApplication, QSurfaceFormat
from PySide6.QtWidgets import QApplication, QMessageBox

ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = ROOT / 'tools'
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from app.gltf.loader import GltfLoader  # noqa: E402
from app.main_window import MainWindow  # noqa: E402
from app.styles import GLOBAL_QSS  # noqa: E402


def _setup_surface_format() -> None:
    '''在 QApplication 创建之前设置默认表面格式（Qt6 下 QOpenGLWidget 黑窗的已知修复点）。'''
    fmt = QSurfaceFormat()
    fmt.setRenderableType(QSurfaceFormat.RenderableType.OpenGL)
    # Intel UHD 770 + PySide6 6.10 下 MSAA FBO 创建失败，samples=0
    fmt.setSamples(0)
    fmt.setVersion(3, 3)
    fmt.setProfile(QSurfaceFormat.OpenGLContextProfile.CompatibilityProfile)
    fmt.setAlphaBufferSize(8)   # DWM 合成纹理需要 alpha 通道（黑窗常见根因）
    fmt.setStencilBufferSize(8)
    fmt.setDepthBufferSize(24)
    fmt.setSwapInterval(0)
    QSurfaceFormat.setDefaultFormat(fmt)


def _asset_path() -> Path:
    if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
        return Path(sys._MEIPASS) / 'app' / 'assets' / 'generic-hand-left.glb'
    return ROOT / 'app' / 'assets' / 'generic-hand-left.glb'


def main() -> int:
    parser = argparse.ArgumentParser(description='灵巧手上位机（PySide6 桌面版）')
    parser.add_argument('--demo', action='store_true', help='启动后自动连接模拟数据源')
    parser.add_argument('--screenshot', metavar='PATH', help='连接后延时截图（验收证据）')
    parser.add_argument('--quit-after', metavar='MS', type=int, default=0, help='延时自动退出（配合截图）')
    args = parser.parse_args()

    _setup_surface_format()  # 必须在 QApplication 创建之前
    app = QApplication(sys.argv)
    app.setStyleSheet(GLOBAL_QSS)

    lock = QLockFile(QDir.tempPath() + '/灵巧手上位机.lock')
    # 崩溃残留的锁文件 30 秒后视为过期自动清除（0 = 永不过期，会导致崩溃后无法重启）
    lock.setStaleLockTime(30000)
    if not lock.tryLock(100):
        QMessageBox.information(None, '灵巧手上位机', '上位机已在运行。')
        return 0

    try:
        asset = GltfLoader(_asset_path()).load()
    except Exception as error:
        QMessageBox.critical(None, '灵巧手上位机', f'真实手模型加载失败：{error}')
        return 1

    try:
        window = MainWindow(asset, auto_demo=args.demo)
    except Exception as error:
        QMessageBox.critical(None, '灵巧手上位机', f'窗口初始化失败：{error}')
        return 1
    window.show()

    if args.screenshot:
        def capture() -> None:
            # 手部画面经 QPainter 软呈现（普通控件绘制路径），window.grab 即可完整捕获
            pix = window.grab()
            target = Path(args.screenshot)
            target.parent.mkdir(parents=True, exist_ok=True)
            pix.save(str(target))
            print(f'[screenshot] 已保存 {target} ({pix.width()}x{pix.height()})')
        QTimer.singleShot(4000, capture)

    if args.quit_after and args.quit_after > 0:
        QTimer.singleShot(args.quit_after, app.quit)

    exit_code = app.exec()
    lock.unlock()
    return exit_code


if __name__ == '__main__':
    raise SystemExit(main())
