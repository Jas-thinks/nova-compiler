"""Runtime configuration (paths and safety limits). All values can be overridden by environment variables."""
from __future__ import annotations

import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]  # .../nova


def _find_novac() -> Path:
    env = os.environ.get("NOVAC_BIN")
    if env:
        return Path(env)
    for candidate in (ROOT / "compiler" / "build" / "novac", ROOT.parent / "build_tmp" / "novac"):
        if candidate.exists():
            return candidate
    return ROOT / "compiler" / "build" / "novac"


NOVAC_BIN: Path = _find_novac()
NOVA_TESTS_BIN: Path = NOVAC_BIN.parent / "nova_tests"
EXAMPLES_DIR: Path = Path(os.environ.get("NOVA_EXAMPLES", ROOT / "examples"))

MAX_SOURCE_BYTES = int(os.environ.get("NOVA_MAX_SOURCE_BYTES", 64 * 1024))
COMPILE_TIMEOUT_S = float(os.environ.get("NOVA_COMPILE_TIMEOUT", 10))
TESTS_TIMEOUT_S = float(os.environ.get("NOVA_TESTS_TIMEOUT", 120))
MAX_VM_STEPS = int(os.environ.get("NOVA_MAX_VM_STEPS", 5_000_000))
DEFAULT_VM_STEPS = 2_000_000
MAX_TRACE_STEPS = int(os.environ.get("NOVA_MAX_TRACE_STEPS", 20_000))
MAX_FUZZ_COUNT = int(os.environ.get("NOVA_MAX_FUZZ", 2000))
MEMORY_LIMIT_BYTES = int(os.environ.get("NOVA_MEM_LIMIT", 1024 * 1024 * 1024))
MAX_OUTPUT_BYTES = 64 * 1024 * 1024

SESSION_TTL_S = 30 * 60
MAX_SESSIONS = 25

CORS_ORIGINS = [o for o in os.environ.get("NOVA_CORS", "http://localhost:5173,http://127.0.0.1:5173").split(",") if o]
