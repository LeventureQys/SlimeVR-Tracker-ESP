'''帧窗口纯数据类：400 帧环形窗口（viewer.js 273–278 行语义）。'''
from __future__ import annotations


class FrameWindow:
    '''push 上限 max_len；frames() 返回当前窗口；latest() 最新帧；index_of_latest() 最新索引。'''

    def __init__(self, max_len: int = 400) -> None:
        self._frames = []
        self._max_len = max_len

    def push(self, frame: dict) -> None:
        self._frames.append(frame)
        if len(self._frames) > self._max_len:
            del self._frames[:len(self._frames) - self._max_len]

    def frames(self) -> list:
        return list(self._frames)

    def latest(self):
        return self._frames[-1] if self._frames else None

    def index_of_latest(self) -> int:
        return len(self._frames) - 1

    def clear(self) -> None:
        self._frames = []

    def __len__(self) -> int:
        return len(self._frames)
