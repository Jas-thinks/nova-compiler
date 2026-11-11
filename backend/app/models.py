"""Pydantic request models. Validation here is the first line of defence against malformed requests."""
from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field, field_validator

from . import config

PASS_IDS = {"inlining", "constant_folding", "constant_propagation", "algebraic", "cse", "unreachable", "dce"}


class _SourceRequest(BaseModel):
    source: str = Field(..., description="NOVA program text")
    passes: list[str] | None = Field(
        default=None,
        description="Optimization pass ids to enable. null = all passes, [] = none.",
    )

    @field_validator("source")
    @classmethod
    def _source_size(cls, v: str) -> str:
        if len(v.encode("utf-8")) > config.MAX_SOURCE_BYTES:
            raise ValueError(f"source exceeds {config.MAX_SOURCE_BYTES} bytes")
        if "\x00" in v:
            raise ValueError("source contains NUL bytes")
        return v

    @field_validator("passes")
    @classmethod
    def _known_passes(cls, v: list[str] | None) -> list[str] | None:
        if v is None:
            return v
        unknown = sorted(set(v) - PASS_IDS)
        if unknown:
            raise ValueError(f"unknown optimization pass(es): {', '.join(unknown)}")
        return sorted(set(v))


class CompileRequest(_SourceRequest):
    run: bool = False
    max_steps: int = Field(default=config.DEFAULT_VM_STEPS, ge=1, le=config.MAX_VM_STEPS)


class ExecuteRequest(_SourceRequest):
    max_steps: int = Field(default=config.DEFAULT_VM_STEPS, ge=1, le=config.MAX_VM_STEPS)


class OptimizeRequest(_SourceRequest):
    pass


class DebugStartRequest(_SourceRequest):
    target: Literal["optimized", "baseline"] = "optimized"
    breakpoints: list[int] = Field(default_factory=list, max_length=500, description="Source line numbers")
    max_steps: int = Field(default=200_000, ge=1, le=config.MAX_VM_STEPS)


class DebugStepRequest(BaseModel):
    session_id: str = Field(..., min_length=8, max_length=64)
    direction: Literal["forward", "back"] = "forward"
    mode: Literal["instruction", "line", "over"] = "instruction"
    count: int = Field(default=1, ge=1, le=1000)


class DebugContinueRequest(BaseModel):
    session_id: str = Field(..., min_length=8, max_length=64)
    breakpoints: list[int] | None = Field(default=None, max_length=500)
    breakpoint_addrs: list[int] | None = Field(default=None, max_length=500)


class DebugSeekRequest(BaseModel):
    session_id: str = Field(..., min_length=8, max_length=64)
    cursor: int = Field(..., ge=0)


class DebugResetRequest(BaseModel):
    session_id: str = Field(..., min_length=8, max_length=64)


class DebugBreakpointsRequest(BaseModel):
    session_id: str = Field(..., min_length=8, max_length=64)
    breakpoints: list[int] = Field(default_factory=list, max_length=500)
    breakpoint_addrs: list[int] = Field(default_factory=list, max_length=500)


class TestRunRequest(BaseModel):
    fuzz_count: int = Field(default=200, ge=0, le=config.MAX_FUZZ_COUNT)
    seed: int = Field(default=1, ge=0, le=2**31)
    passes: list[str] | None = None
    unit_tests: bool = True

    @field_validator("passes")
    @classmethod
    def _known_passes(cls, v):
        if v is None:
            return v
        unknown = sorted(set(v) - PASS_IDS)
        if unknown:
            raise ValueError(f"unknown optimization pass(es): {', '.join(unknown)}")
        return sorted(set(v))


class ExplainRequest(BaseModel):
    source: str = Field(..., max_length=config.MAX_SOURCE_BYTES)
    diagnostic: dict


class ExplorerCRequest(BaseModel):
    source: str = Field(..., max_length=16 * 1024)
    opt_level: int = Field(default=1, ge=0, le=3)


class AsmRequest(BaseModel):
    source: str = Field(..., max_length=config.MAX_SOURCE_BYTES)
    passes: list[str] | None = None
    regalloc: bool = True

    @field_validator("passes")
    @classmethod
    def _known_passes(cls, v):
        if v is None:
            return v
        unknown = sorted(set(v) - PASS_IDS)
        if unknown:
            raise ValueError(f"unknown optimization pass(es): {', '.join(unknown)}")
        return sorted(set(v))


class SqlRequest(BaseModel):
    script: str = Field(..., max_length=64 * 1024)


class SourceRequest(BaseModel):
    source: str = Field(..., max_length=config.MAX_SOURCE_BYTES)
