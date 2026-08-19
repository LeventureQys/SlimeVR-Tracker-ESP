# 旧版 web 与 HTTP 服务（已归档）

v1.0 PySide 桌面化验收通过后，旧版浏览器前端与其 HTTP 服务整体归档于此（2026-08-17）。

## 内容

| 路径 | 说明 |
| --- | --- |
| `web/` | Three.js 前端（index.html / style.css / js / assets / vendor） |
| `serve_app.py` | 原 HTTP 服务（ThreadingHTTPServer + SSE 推送 + SerialLiveSession） |

## 归档原因

- 需求（`Document/v1.0 C++化/需求说明.md`）：去掉 web 端，改用 Python + PySide 桌面界面。
- 替代者：`app/`（PySide6 桌面应用，QOpenGLWidget 自绘渲染）+ `tools/serial_live.py`（串口会话，自 serve_app.py 提取）+ `tools/processed_pipeline.py`（协议解析，零改动保留在 tools/）。
- 桌面应用自带 GLB 资产副本（`app/assets/generic-hand-left.glb`），不依赖本目录。

## 如需临时对照运行旧版

```powershell
.venv\Scripts\python.exe .\archived\旧版web与HTTP服务\serve_app.py --port 8000 --no-open
```

浏览器打开 `http://127.0.0.1:8000/web/`（注意：serve_app.py 通过 `Path(__file__).resolve().parents[1]` 定位 web 目录，归档路径下 `web/` 与其同级，可直接运行；前端 Three.js 依赖 unpkg CDN）。
