'''本工作区上位机服务：有线串口接收主控已解算姿态。

静态页来自 ``web/``。串口只接收主控 31 字节×6 姿态帧，直接驱动三维手。
'''
from __future__ import annotations

import argparse
import gzip
import json
import queue
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import webbrowser
from dataclasses import dataclass
from functools import partial
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse


def _bundle_dir() -> Path:
    '''PyInstaller 冻结后资源在 _MEIPASS，源码运行时在项目根目录。'''
    if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
        return Path(sys._MEIPASS)
    return Path(__file__).resolve().parents[1]


HOST_DIR = _bundle_dir()
WEB_DIR = HOST_DIR / 'web'
TOOLS_DIR = Path(__file__).resolve().parent
if not getattr(sys, 'frozen', False) and str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from processed_pipeline import ProcessedPoseAssembler, modbus_crc

DEFAULT_BAUD = 921600
HAND_HZ = 200.0
HOST_CMD_RECALIBRATE_PREFIX = bytes([170, 85, 192, 1, 0, 0])
CALIBRATE_STALE_S = 0.4


def build_recalibrate_command() -> bytes:
    crc = modbus_crc(HOST_CMD_RECALIBRATE_PREFIX)
    return HOST_CMD_RECALIBRATE_PREFIX + bytes((crc & 255, crc >> 8 & 255))


@dataclass
class SerialStatus:
    phase: str
    message: str
    sample_count: int = 0
    output_frame_count: int = 0
    port: str | None = None
    baud: int | None = None


class SerialLiveSession:
    '''后台线程读取串口并推送主控姿态帧。'''

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._thread = None
        self._stop = threading.Event()
        self._serial = None
        self._pose_assembler = ProcessedPoseAssembler()
        self._frame_queue = queue.Queue(maxsize=8)
        self._latest_frame = None
        self._status = SerialStatus(phase='idle', message='串口未连接')
        self._host_calibrating = False
        self._calibrate_until = 0.0

    def _status_dict(self) -> dict:
        st = self.status
        with self._lock:
            calibrating = self._host_calibrating
        payload = {
            'phase': st.phase,
            'message': st.message,
            'sample_count': st.sample_count,
            'output_frame_count': st.output_frame_count,
            'port': st.port,
            'baud': st.baud,
            'hand_hz': HAND_HZ,
        }
        if calibrating:
            payload['phase'] = 'calibrating'
            payload['message'] = '重新标定中，请张开手静止约 3 s'
        return payload

    @property
    def status(self) -> SerialStatus:
        with self._lock:
            st = self._status
            return SerialStatus(
                phase=st.phase,
                message=st.message,
                sample_count=self._pose_assembler.node_count,
                output_frame_count=self._pose_assembler.output_frame_count,
                port=st.port,
                baud=st.baud,
            )

    def latest_frame(self) -> dict | None:
        with self._lock:
            return self._latest_frame

    def list_ports(self) -> list[dict[str, str]]:
        try:
            from serial.tools import list_ports
        except ImportError as error:
            raise RuntimeError('未安装 pyserial，请使用本目录 .venv 运行本服务') from error
        return [
            {
                'device': port.device,
                'description': port.description or '',
                'hwid': port.hwid or '',
            }
            for port in list_ports.comports()
        ]

    def start(self, port: str, baud: int = DEFAULT_BAUD) -> None:
        self.stop()
        import serial
        ser = serial.Serial(port=port, baudrate=baud, timeout=0.05)
        self._serial = ser
        self._stop.clear()
        self._pose_assembler = ProcessedPoseAssembler()
        self._pose_assembler.reset()
        self._latest_frame = None
        message = f'已打开 {port} @ {baud}，等待主控姿态帧…'
        with self._lock:
            self._host_calibrating = False
            self._calibrate_until = 0.0
            self._status = SerialStatus(phase='connecting', message=message, port=port, baud=baud)
        self._thread = threading.Thread(target=self._run, name='serial-live', daemon=True)
        self._thread.start()

    def request_calibrate(self) -> None:
        with self._lock:
            ser = self._serial
            if ser is None:
                raise RuntimeError('串口未连接')
            port = self._status.port
            baud = self._status.baud
        try:
            ser.write(build_recalibrate_command())
            ser.flush()
        except Exception as error:
            raise RuntimeError(f'发送标定命令失败：{error}') from error
        with self._lock:
            self._host_calibrating = True
            self._calibrate_until = time.perf_counter() + CALIBRATE_STALE_S
            self._status = SerialStatus(phase='calibrating', message='重新标定中，请张开手静止约 3 s', port=port, baud=baud)

    def stop(self) -> None:
        self._stop.set()
        thread = self._thread
        if thread and thread.is_alive():
            thread.join(timeout=1.5)
        self._thread = None
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None
        with self._lock:
            self._host_calibrating = False
            self._calibrate_until = 0.0
            self._status = SerialStatus(phase='idle', message='串口未连接')

    def _publish(self, frame: dict) -> None:
        with self._lock:
            self._latest_frame = frame
        try:
            self._frame_queue.put_nowait(frame)
        except queue.Full:
            try:
                self._frame_queue.get_nowait()
            except queue.Empty:
                pass
            try:
                self._frame_queue.put_nowait(frame)
            except queue.Full:
                pass

    def _set_status_from_worker(self, phase: str, message: str) -> None:
        with self._lock:
            self._status = SerialStatus(phase=phase, message=message, port=self._status.port, baud=self._status.baud)

    def _accept_live_frame(self) -> bool:
        with self._lock:
            if not self._host_calibrating:
                return True
            if time.perf_counter() < self._calibrate_until:
                return False
            self._host_calibrating = False
            return True

    def _run(self) -> None:
        assert self._serial is not None
        try:
            while not self._stop.is_set():
                waiting = self._serial.in_waiting
                chunk = self._serial.read(waiting or 1)
                if not chunk:
                    continue
                frames = self._pose_assembler.push(chunk)
                self._set_status_from_worker(self._pose_assembler.phase, self._pose_assembler.message)
                for frame in frames:
                    if not self._accept_live_frame():
                        continue
                    self._publish(frame)
        except Exception as error:
            self._set_status_from_worker('error', f'串口异常：{error}')
        finally:
            if self._serial is not None:
                try:
                    self._serial.close()
                except Exception:
                    pass
                self._serial = None


SESSION = SerialLiveSession()


class HostAppHandler(SimpleHTTPRequestHandler):
    '''web 静态资源 + 串口实时 API。'''
    server_version = 'DexterousHandHostApp/1.0'

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path
        if path == '/api/serial/ports':
            self._send_json({'ports': SESSION.list_ports()}, HTTPStatus.OK)
            return None
        if path == '/api/serial/status':
            self._send_json(SESSION._status_dict(), HTTPStatus.OK)
            return None
        if path == '/api/live/latest':
            frame = SESSION.latest_frame()
            self._send_json({'frame': frame, 'status': SESSION._status_dict()}, HTTPStatus.OK)
            return None
        if path == '/api/live/stream':
            self._stream_live()
            return None
        if path in {'/web/', '/web/index.html'}:
            self._send_index()
            return None
        super().do_GET()

    def end_headers(self) -> None:
        path = urlparse(self.path).path
        if path.startswith('/web/js/') or path.endswith(('.css', '.html', '.glb')):
            self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path
        if path == '/api/serial/open':
            self._serial_open()
            return None
        if path == '/api/serial/close':
            SESSION.stop()
            self._send_json({'ok': True, 'status': SESSION._status_dict()}, HTTPStatus.OK)
            return None
        if path == '/api/serial/calibrate':
            self._serial_calibrate()
            return None
        self.send_error(HTTPStatus.NOT_FOUND, 'API 路径不存在')

    def _serial_open(self) -> None:
        length = int(self.headers.get('Content-Length', '0'))
        body = self.rfile.read(length) if length > 0 else b'{}'
        try:
            payload = json.loads(body.decode('utf-8'))
            port = str(payload.get('port', '')).strip()
            baud = int(payload.get('baud', DEFAULT_BAUD))
            if not port:
                raise ValueError('缺少 port')
            SESSION.start(port, baud)
            self._send_json({'ok': True, 'status': SESSION._status_dict()}, HTTPStatus.OK)
        except Exception as error:
            self._send_json({'ok': False, 'error': str(error)}, HTTPStatus.BAD_REQUEST)

    def _serial_calibrate(self) -> None:
        try:
            SESSION.request_calibrate()
            self._send_json({'ok': True, 'status': SESSION._status_dict()}, HTTPStatus.OK)
        except Exception as error:
            self._send_json({'ok': False, 'error': str(error)}, HTTPStatus.BAD_REQUEST)

    def _stream_live(self) -> None:
        self.send_response(HTTPStatus.OK)
        self.send_header('Content-Type', 'text/event-stream; charset=utf-8')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Connection', 'keep-alive')
        self.end_headers()
        try:
            last_status = None
            while True:
                try:
                    frame = SESSION._frame_queue.get(timeout=0.5)
                    event = {'type': 'frame', 'frame': frame, 'status': SESSION._status_dict()}
                    self.wfile.write(f"data: {json.dumps(event, ensure_ascii=False)}\n\n".encode('utf-8'))
                    self.wfile.flush()
                except queue.Empty:
                    status = SESSION._status_dict()
                    if status != last_status:
                        last_status = dict(status)
                        event = {'type': 'status', 'status': status}
                        self.wfile.write(f"data: {json.dumps(event, ensure_ascii=False)}\n\n".encode('utf-8'))
                        self.wfile.flush()
                    if status.get('phase') in {'error', 'idle'} and SESSION._thread is None:
                        pass
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def _send_index(self) -> None:
        html = (WEB_DIR / 'index.html').read_text(encoding='utf-8')
        body = html.encode('utf-8')
        self.send_response(HTTPStatus.OK)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, payload: dict, status: HTTPStatus) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        use_gzip = 'gzip' in self.headers.get('Accept-Encoding', '').lower() and len(body) > 1024
        if use_gzip:
            body = gzip.compress(body, compresslevel=4)
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        if use_gzip:
            self.send_header('Content-Encoding', 'gzip')
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args) -> None:
        if '/api/live/' in self.path:
            return None
        super().log_message(format, *args)


def host_url(port: int) -> str:
    return f'http://127.0.0.1:{port}/web/'


def is_host_healthy(port: int, timeout: float = 1.0) -> bool:
    try:
        with urllib.request.urlopen(host_url(port), timeout=timeout) as response:
            return 200 <= int(response.status) < 300
    except (urllib.error.URLError, TimeoutError, ConnectionError, OSError):
        return False


def listener_pid(port: int) -> int | None:
    try:
        output = subprocess.check_output(['netstat', '-ano'], text=True, encoding='utf-8', errors='ignore')
    except (OSError, subprocess.CalledProcessError):
        return None
    needle = f'127.0.0.1:{port}'
    for line in output.splitlines():
        if needle in line and 'LISTENING' in line:
            try:
                return int(line.split()[-1])
            except ValueError:
                return None
    return None


def kill_listener(port: int) -> None:
    pid = listener_pid(port)
    if pid is None:
        return None
    subprocess.run(['taskkill', '/PID', str(pid), '/T', '/F'], check=False, capture_output=True)
    time.sleep(0.5)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8000)
    parser.add_argument('--no-open', action='store_true')
    args = parser.parse_args()
    if not WEB_DIR.is_dir():
        raise SystemExit(f'找不到网页目录：{WEB_DIR}')
    handler = partial(HostAppHandler, directory=str(HOST_DIR))
    url = host_url(args.port)
    if is_host_healthy(args.port):
        print(f'上位机已在运行：{url}')
        if not args.no_open:
            webbrowser.open(url)
        time.sleep(1.5)
        return 0
    if listener_pid(args.port) is not None:
        print(f'端口 {args.port} 被占用但页面无响应，正在重启服务…')
        kill_listener(args.port)
    try:
        server = ThreadingHTTPServer(('127.0.0.1', args.port), handler)
    except OSError as error:
        print(f'无法监听端口 {args.port}：{error}')
        return 1
    worker = threading.Thread(target=server.serve_forever, name='host-http', daemon=True)
    worker.start()
    ready = False
    for _ in range(50):
        if is_host_healthy(args.port, timeout=0.3):
            ready = True
            break
        time.sleep(0.05)
    if not ready:
        print('服务已启动，但页面尚未就绪，仍尝试打开浏览器。')
    print(f'上位机：{url}')
    print('请勿关闭本窗口。串口接收主控已解算姿态帧。')
    if not args.no_open:
        webbrowser.open(url)
    try:
        while worker.is_alive():
            worker.join(timeout=0.5)
    except KeyboardInterrupt:
        print('\n服务器已停止')
    finally:
        SESSION.stop()
        server.shutdown()
        server.server_close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
