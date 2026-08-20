'''帧窗口测试：上限丢头、index 语义、latest。'''
from __future__ import annotations

from app.frame_window import FrameWindow


def test_window_cap():
    w = FrameWindow(10)
    for i in range(15):
        w.push({'sequence': i})
    assert len(w) == 10
    assert [f['sequence'] for f in w.frames()] == list(range(5, 15))


def test_index_semantics():
    w = FrameWindow(400)
    assert w.index_of_latest() == -1
    assert w.latest() is None
    w.push({'sequence': 1})
    w.push({'sequence': 2})
    assert w.index_of_latest() == 1
    assert w.latest()['sequence'] == 2


def test_clear():
    w = FrameWindow(5)
    w.push({'sequence': 1})
    w.clear()
    assert len(w) == 0
    assert w.latest() is None
