'''串口实时会话（从 tools/serve_app.py 62–227 行提取，去除 HTTP）。

后台线程读取串口（或模拟数据源）→ ProcessedPoseAssembler 解析 → 内部队列发布。
契约：设计文档 6.3 节。
'''
from __future__ import annotations

import queue
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path

if not getattr(sys, 'frozen', False):
    _tools_dir = str(Path(__file__).resolve().parent)
    if _tools_dir not in sys.path:
        sys.path.insert(0, _tools_dir)

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
    '''后台线程读取串口/模拟数据并推送主控姿态帧。'''

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._thread = None
        self._stop = threading.Event()
        self._serial = None
        self._demo_mode = False
        self._pose_assembler = ProcessedPoseAssembler()
        self._frame_queue = queue.Queue(maxsize=8)
        self._latest_frame = None
        self._status = SerialStatus(phase='idle', message='串口未连接')
        self._host_calibrating = False
        self._calibrate_until = 0.0
        self._raw_sink = None  # 原始字节流回调（录制时由上位机设置，串口线程内调用）

    def set_raw_sink(self, sink) -> None:
        '''设置原始字节流回调（录制时写入文件）；None 清除。回调在串口线程内调用，
        必须线程安全且快速（直接写文件即可）。'''
        with self._lock:
            self._raw_sink = sink

    # ------------------------------------------------------------------ 状态

    def status_dict(self) -> dict:
        st = self.status
        with self._lock:
            calibrating = self._host_calibrating
        payload = {
            'phase': st.phase,
            'message': st.message,
            'sample_count': st.sample_count,
            'output_frame_count': st.output_frame_count,
            'bad_crc': self._pose_assembler.bad_crc,
            'skipped': self._pose_assembler.skipped,
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

    @property
    def running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def latest_frame(self) -> dict | None:
        with self._lock:
            return self._latest_frame

    def drain_frames(self) -> list:
        '''非阻塞清空内部队列，返回帧列表（FIFO）。'''
        frames = []
        while True:
            try:
                frames.append(self._frame_queue.get_nowait())
            except queue.Empty:
                break
        return frames

    # ------------------------------------------------------------------ 连接

    def list_ports(self) -> list:
        try:
            from serial.tools import list_ports
        except ImportError as error:
            raise RuntimeError('未安装 pyserial，请使用本目录 .venv 运行本程序') from error
        return [
            {'device': port.device, 'description': port.description or '', 'hwid': port.hwid or ''}
            for port in list_ports.comports()
        ]

    def start(self, port: str, baud: int = DEFAULT_BAUD) -> None:
        self.stop()
        import serial
        ser = serial.Serial(port=port, baudrate=baud, timeout=0.05)
        self._serial = ser
        self._demo_mode = False
        self._prepare(port=port, baud=baud, message=f'已打开 {port} @ {baud}，等待主控姿态帧…')
        self._thread = threading.Thread(target=self._run, name='serial-live', daemon=True)
        self._thread.start()

    def start_demo(self) -> None:
        '''模拟数据源线程（真实实现在 app.demo_source，走同一 AA55 协议路径）。'''
        self.stop()
        try:
            from app.demo_source import demo_frame_bytes
        except ImportError as error:
            raise RuntimeError('模拟数据源不可用') from error
        self._demo_mode = True
        self._prepare(port=None, baud=None, message='模拟数据源已启动，等待姿态帧…')
        self._thread = threading.Thread(target=self._run_demo, args=(demo_frame_bytes,), name='serial-demo', daemon=True)
        self._thread.start()

    def _prepare(self, port, baud, message: str) -> None:
        self._stop.clear()
        self._pose_assembler = ProcessedPoseAssembler()
        self._pose_assembler.reset()
        self._latest_frame = None
        with self._lock:
            self._host_calibrating = False
            self._calibrate_until = 0.0
            self._status = SerialStatus(phase='connecting', message=message, port=port, baud=baud)

    def request_calibrate(self) -> None:
        with self._lock:
            ser = self._serial
            demo = self._demo_mode
            port = self._status.port
            baud = self._status.baud
        if demo:
            raise RuntimeError('模拟数据源不支持标定')
        if ser is None:
            raise RuntimeError('串口未连接')
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
        self._demo_mode = False
        with self._lock:
            self._host_calibrating = False
            self._calibrate_until = 0.0
            self._status = SerialStatus(phase='idle', message='串口未连接')

    # ------------------------------------------------------------------ 内部

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

    def _process_chunk(self, chunk: bytes) -> None:
        with self._lock:
            sink = self._raw_sink
        if sink is not None:
            try:
                sink(chunk)
            except Exception:
                pass  # 录制侧异常不应拖垮串口线程
        frames = self._pose_assembler.push(chunk)
        self._set_status_from_worker(self._pose_assembler.phase, self._pose_assembler.message)
        for frame in frames:
            if not self._accept_live_frame():
                continue
            self._publish(frame)

    def _run(self) -> None:
        assert self._serial is not None
        try:
            while not self._stop.is_set():
                waiting = self._serial.in_waiting
                chunk = self._serial.read(waiting or 1)
                if not chunk:
                    continue
                self._process_chunk(chunk)
        except Exception as error:
            self._set_status_from_worker('error', f'串口异常：{error}')
        finally:
            if self._serial is not None:
                try:
                    self._serial.close()
                except Exception:
                    pass
                self._serial = None

    def _run_demo(self, frame_bytes_func) -> None:
        start = time.perf_counter()
        sequence = 0
        try:
            while not self._stop.is_set():
                now = time.perf_counter() - start
                try:
                    chunk = frame_bytes_func(now, sequence)
                except Exception as error:
                    raise RuntimeError(f'模拟数据生成失败：{error}') from error
                sequence += 1
                self._process_chunk(chunk)
                time.sleep(1.0 / HAND_HZ)
        except Exception as error:
            self._set_status_from_worker('error', f'模拟数据源异常：{error}')
