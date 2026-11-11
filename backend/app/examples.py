"""Loads example programs from the examples/ directory (metadata is read from `// @key value` header comments)."""
from __future__ import annotations

import re

from . import config

_META = re.compile(r"^//\s*@(\w+)\s+(.*)$")


def load_examples() -> list[dict]:
    out = []
    for path in sorted(config.EXAMPLES_DIR.glob("*.nova")):
        text = path.read_text(encoding="utf-8")
        meta: dict[str, str] = {}
        for line in text.splitlines():
            m = _META.match(line)
            if m:
                meta[m.group(1)] = m.group(2).strip()
            elif line.strip() and not line.startswith("//"):
                break
        out.append(
            {
                "id": path.stem,
                "title": meta.get("title", path.stem),
                "description": meta.get("desc", ""),
                "category": meta.get("category", "misc"),
                "source": text,
            }
        )
    return out
