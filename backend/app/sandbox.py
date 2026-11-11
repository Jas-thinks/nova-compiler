"""Safe subprocess execution used for every external tool (novac, gcc/clang).

What this DOES provide: argv-only invocation (never a shell), wall-clock timeout with process-group kill,
hard cap on captured output (the child is killed when exceeded), RLIMIT_CPU / RLIMIT_AS / RLIMIT_NPROC-free
core-dump suppression, a scrubbed environment and a private working directory.

What it does NOT provide: filesystem, network or syscall isolation (no namespaces / seccomp / containers).
Do not expose this service to untrusted users without running it inside a container or VM.
"""
from __future__ import annotations

import os
import signal
import subprocess
import threading
from dataclasses import dataclass

try:  # resource limits are POSIX-only
    import resource
except ImportError:  # pragma: no cover
    resource = None  # type: ignore[assignment]

from . import config


@dataclass
class RunResult:
    returncode: int
    stdout: bytes
    stderr: bytes
    timed_out: bool = False
    output_truncated: bool = False


def limits_available() -> bool:
    return resource is not None


def _preexec(cpu_s: int, mem: int):
    def apply() -> None:
        if resource is None:
            return
        for lim, val in ((resource.RLIMIT_CPU, cpu_s), (resource.RLIMIT_AS, mem)):
            try:
                resource.setrlimit(lim, (val, val))
            except (ValueError, OSError):
                pass  # e.g. limit above the hard limit; the wall-clock timeout still applies
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    return apply


def run_limited(argv: list[str], stdin: bytes = b"", timeout: float | None = None, max_output: int | None = None,
                cwd: str | None = None, mem_bytes: int | None = None) -> RunResult:
    timeout = timeout or config.COMPILE_TIMEOUT_S
    max_output = max_output or config.MAX_OUTPUT_BYTES
    proc = subprocess.Popen(
        argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd,
        preexec_fn=_preexec(int(timeout) + 5, mem_bytes or config.MEMORY_LIMIT_BYTES), start_new_session=True,
        env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
    )
    state = {"timed_out": False, "truncated": False}

    def kill() -> None:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass

    timer = threading.Timer(timeout, lambda: (state.__setitem__("timed_out", True), kill()))
    timer.start()
    bufs: dict[str, bytearray] = {"out": bytearray(), "err": bytearray()}

    def pump(stream, key: str) -> None:
        while True:
            chunk = stream.read(65536)
            if not chunk:
                return
            if len(bufs["out"]) + len(bufs["err"]) + len(chunk) > max_output:
                state["truncated"] = True
                kill()
                return
            bufs[key].extend(chunk)

    threads = [threading.Thread(target=pump, args=(proc.stdout, "out")), threading.Thread(target=pump, args=(proc.stderr, "err"))]
    for t in threads:
        t.start()
    try:
        proc.stdin.write(stdin)
        proc.stdin.close()
    except (BrokenPipeError, OSError):
        pass
    for t in threads:
        t.join()
    proc.wait()
    timer.cancel()
    return RunResult(proc.returncode, bytes(bufs["out"]), bytes(bufs["err"]), state["timed_out"], state["truncated"])
