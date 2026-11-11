"""Debugger sessions.

The C++ VM executes the program once with snapshot recording (`novac compile --trace`). Every snapshot is the
real VM state (instruction pointer, operand stacks, locals of every call frame) *before* an instruction runs.
Stepping, stepping backward, breakpoints and 'continue' just move a cursor over those authentic snapshots, so
the browser never simulates execution.
"""
from __future__ import annotations

import threading
import time
import uuid
from dataclasses import dataclass, field

from . import config


@dataclass
class DebugSession:
    id: str
    created: float
    last_used: float
    bytecode: dict
    trace: list[dict]
    output: list[str]
    error: dict | None
    truncated: bool
    target: str
    total_steps: int
    cursor: int = 0
    breakpoints: set[int] = field(default_factory=set)         # source lines
    breakpoint_addrs: set[int] = field(default_factory=set)    # bytecode addresses

    # ---- helpers -----------------------------------------------------------------------------
    def _instr(self, ip: int) -> dict:
        return self.bytecode["instructions"][ip]

    def _line(self, idx: int) -> int:
        return self._instr(self.trace[idx]["ip"]).get("line", 0)

    def _depth(self, idx: int) -> int:
        return len(self.trace[idx]["frames"])

    def _hits_breakpoint(self, idx: int) -> bool:
        snap = self.trace[idx]
        if snap["ip"] in self.breakpoint_addrs:
            return True
        line = self._line(idx)
        if line and line in self.breakpoints:
            if idx == 0:
                return True
            prev = idx - 1
            # stop when we arrive at a breakpoint line (not on every instruction of that line)
            return self._line(prev) != line or self._depth(prev) != self._depth(idx)
        return False

    # ---- navigation --------------------------------------------------------------------------
    def step(self, direction: str, mode: str, count: int) -> None:
        last = len(self.trace) - 1
        for _ in range(count):
            if direction == "back":
                if self.cursor == 0:
                    break
                self.cursor -= 1
                if mode == "line":
                    while self.cursor > 0 and self._line(self.cursor - 1) == self._line(self.cursor) and self._depth(self.cursor - 1) == self._depth(self.cursor):
                        self.cursor -= 1
                continue
            if self.cursor >= last:
                break
            start_line, start_depth = self._line(self.cursor), self._depth(self.cursor)
            self.cursor += 1
            if mode in ("line", "over"):
                # stay on the same source line of the same frame; 'over' also runs called functions to completion
                while self.cursor < last:
                    deeper = self._depth(self.cursor) > start_depth
                    if mode == "over" and deeper:
                        self.cursor += 1
                    elif self._line(self.cursor) == start_line and self._depth(self.cursor) == start_depth:
                        self.cursor += 1
                    else:
                        break

    def continue_(self) -> str:
        last = len(self.trace) - 1
        while self.cursor < last:
            self.cursor += 1
            if self._hits_breakpoint(self.cursor):
                return "breakpoint"
        return "finished"

    def reset(self) -> None:
        self.cursor = 0

    # ---- presentation ------------------------------------------------------------------------
    def view(self, reason: str = "") -> dict:
        snap = self.trace[self.cursor]
        funcs = self.bytecode["functions"]
        frames_out = []
        for depth, fr in enumerate(snap["frames"]):
            f = funcs[fr["fn"]]
            locals_out = []
            for i, name in enumerate(f["slotNames"]):
                locals_out.append(
                    {"slot": i, "name": name, "type": f["slotTypes"][i], "value": fr["locals"][i], "temp": f["slotIsTemp"][i], "param": i < f["params"]}
                )
            frames_out.append(
                {"depth": depth, "function": f["name"], "callLine": fr["callLine"], "locals": locals_out, "stack": fr["stack"]}
            )
        instr = self._instr(snap["ip"])
        # history = last 40 executed instructions (the snapshot at k describes the state *before* instruction k runs)
        lo = max(0, self.cursor - 40)
        history = [
            {"step": self.trace[i]["step"], "addr": self.trace[i]["ip"], "text": self._instr(self.trace[i]["ip"])["text"], "line": self._line(i), "event": self.trace[i + 1]["event"]}
            for i in range(lo, self.cursor)
        ]
        at_end = self.cursor == len(self.trace) - 1
        done = at_end and snap["terminal"]
        return {
            "session_id": self.id,
            "cursor": self.cursor,
            "total_steps": self.total_steps,
            "trace_length": len(self.trace),
            "trace_truncated": self.truncated,
            "step": snap["step"],
            "ip": snap["ip"],
            "instruction": {"addr": snap["ip"], "text": instr["text"], "comment": instr["comment"], "line": instr.get("line", 0), "col": instr.get("col", 0),
                            "description": instr["description"], "function": instr["function"]},
            "event": snap["event"],
            "frames": frames_out,
            "output": self.output[: snap["out"]],
            "finished": done,
            "error": self.error if done else None,
            "history": history,
            "reason": reason,
            "breakpoints": sorted(self.breakpoints),
            "breakpoint_addrs": sorted(self.breakpoint_addrs),
            "target": self.target,
        }


class SessionStore:
    def __init__(self) -> None:
        self._sessions: dict[str, DebugSession] = {}
        self._lock = threading.Lock()

    def _evict(self) -> None:
        now = time.time()
        for sid in [s for s, v in self._sessions.items() if now - v.last_used > config.SESSION_TTL_S]:
            del self._sessions[sid]
        while len(self._sessions) >= config.MAX_SESSIONS:
            oldest = min(self._sessions.values(), key=lambda v: v.last_used)
            del self._sessions[oldest.id]

    def create(self, **kwargs) -> DebugSession:
        with self._lock:
            self._evict()
            now = time.time()
            s = DebugSession(id=uuid.uuid4().hex, created=now, last_used=now, **kwargs)
            self._sessions[s.id] = s
            return s

    def get(self, sid: str) -> DebugSession | None:
        with self._lock:
            s = self._sessions.get(sid)
            if s:
                s.last_used = time.time()
            return s

    def __len__(self) -> int:
        return len(self._sessions)


store = SessionStore()
