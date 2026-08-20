@echo off
chcp 65001 >nul 2>nul
title 灵巧手上位机
cd /d "%~dp0"

if not exist ".venv\Scripts\python.exe" goto :bootstrap
goto :run

:bootstrap
echo [1/3] 未找到运行环境，正在自动创建 .venv ...
where py >nul 2>nul
if not errorlevel 1 (
    py -3 -m venv .venv
) else (
    python -m venv .venv
)
if not exist ".venv\Scripts\python.exe" goto :nopython
echo [2/3] 正在安装依赖 pyserial + PySide6 ...
".venv\Scripts\python.exe" -m pip install -r requirements.txt --disable-pip-version-check --timeout 30
if errorlevel 1 (
    echo [提示] 默认源安装失败，改用官方源 pypi.org 重试 ...
    ".venv\Scripts\python.exe" -m pip install -r requirements.txt --index-url https://pypi.org/simple --disable-pip-version-check --timeout 60
    if errorlevel 1 goto :noinstall
)

:run
echo [3/3] 启动灵巧手上位机（桌面窗口即界面；关闭本窗口即停止）
".venv\Scripts\python.exe" -m app.main
if errorlevel 1 (
    echo.
    echo [错误] 程序异常退出，请把上面窗口中的信息反馈给开发者。
    pause
)
exit /b 0

:nopython
echo.
echo [错误] 创建运行环境失败：未找到可用的 Python。
echo        请先安装 Python 3.9 及以上版本，安装时勾选 Add python.exe to PATH，
echo        或到 https://www.python.org/downloads/ 下载安装后重试。
pause
exit /b 1

:noinstall
echo.
echo [错误] 依赖安装失败，请检查网络后重试；也可以手动执行：
echo    .venv\Scripts\pip install -r requirements.txt
pause
exit /b 1
