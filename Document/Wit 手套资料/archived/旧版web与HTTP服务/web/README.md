# web

浏览器页面：接收主控姿态帧，显示三维手、角度和曲线。

## 启动

双击上级目录 `启动上位机.bat`，或：

```powershell
.\.venv\Scripts\python.exe .\tools\serve_app.py --port 8000
```

打开 `http://127.0.0.1:8000/web/`。连接后可点「重新标定」，张开手静止约 3 秒。
