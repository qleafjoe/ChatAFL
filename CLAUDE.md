# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ChatAFL is an LLM-guided protocol fuzzer built on top of AFLNet. It integrates a large language model (MiniMax API, OpenAI-compatible) into the fuzzing loop for three purposes: grammar extraction, seed enrichment, and stall-breaking. A validation framework (`llm-validator.c/h`) validates LLM-generated messages against protocol format rules with optional grammar/context constraints and feedback retry.

## Build & Run Workflow

The primary workflow is entirely Docker-based. `build_targets.sh` copies fuzzer source into benchmark subject directories, then Docker images are built (Dockerfiles internally run `make clean all` and `cd llvm_mode && make`).

```bash
# Step 1: Set LLM credentials (required before building Docker images)
export LLM_URL="https://api.minimaxi.com/v1/text/chatcompletion_v2"
export LLM_TOKEN="your-token"
export LLM_MODEL="MiniMax-M2.7"

# Step 2: Build Docker images (copies sources, compiles inside Docker)
./build_targets.sh

# Step 3: Run experiments
./run_tr_ablation.sh smoke    # 10 min, 1 container, live555 only
./run_tr_ablation.sh short    # 120 min, 3 containers
./run_tr_ablation.sh full     # 720 min, 10 containers
# Or custom: ./run.sh NUM_CONTAINERS TIMEOUT_MINUTES TARGETS FUZZERS EXPERIMENT_ID

# Step 4: Analyze results
./analyze.sh live555 1440

# Clean up Docker
./clean.sh
```

### Local Development (direct make)

For local compilation outside Docker (e.g., iterating on C code):

```bash
cd ChatAFL && make clean all    # build afl-fuzz
cd ChatAFL/llvm_mode && make    # build LLVM instrumentation
cd ChatAFL && make test         # run unit tests
cd ChatAFL && make test-integration  # run integration tests
```

System dependencies for local build: `clang`, `graphviz-dev`, `libcap-dev`, `libpcre2-dev`, `libcurl4-openssl-dev`, `libjson-c-dev`

## Architecture

### Core Source (ChatAFL/)

- **`afl-fuzz.c`** — Main fuzzer loop: seed selection, mutation, execution, coverage tracking.
- **`aflnet.c/h`** — Network protocol state machine. Request/response parsers for 11 protocols (benchmark uses RTSP/Live555 and FTP/PureFTPD). State selection algorithms: RANDOM, ROUND_ROBIN, FAVOR.
- **`chat-llm.c/h`** — LLM integration via libcurl HTTP POST. Key functions: `chat_with_llm()`, `construct_prompt_for_templates()`, `construct_prompt_stall()`, `enrich_sequence()`, `llm_feedback_retry_stall()`.
- **`llm-validator.c/h`** — Validation framework. `validate_llm_message()` / `validate_llm_sequence()` with format/grammar/context modes. `classify_llm_execution_gain()` for post-execution attribution. Controlled by env vars: `AFL_LLM_VALIDATION`, `AFL_LLM_VALIDATION_STRICT`, `AFL_LLM_POST_GAIN`, `AFL_LLM_FEEDBACK`.

### Ablation Variants

Variants are in separate top-level directories. Two control mechanisms:

- **V0/V1/V2**: Controlled by `env.sh` environment variables toggling features at runtime. V1/V2 are thin overlays — `build_targets.sh` rsyncs ChatAFL base source first, then overlays the variant's `afl-fuzz.c` and `env.sh`.
- **TR1-TR4**: Behavioral differences hardcoded in their own source files (different `afl-fuzz.c`, `chat-llm.c`, `aflnet.c`).

| Variant | Directory | Validation | Strict | Post-Gain | Feedback |
|---------|-----------|------------|--------|-----------|----------|
| aflnet | `aflnet/` | No | N/A | N/A | N/A |
| ChatAFL | `ChatAFL/` | Yes | Yes | Yes | Yes |
| TR1-TR4 | `ChatAFL-TR{1,2,3,4}/` | (hardcoded in source) | | | |
| V0 | `ChatAFL-V0/` | Disabled | Disabled | Disabled | Disabled |
| V1 | `ChatAFL-V1/` | Format only | No | No | Yes |
| V2 | `ChatAFL-V2/` | Full | Yes | No | Yes |

### Benchmark Infrastructure

- **`benchmark/subjects/RTSP/Live555/`** — Live555 (RTSP) target with Dockerfile
- **`benchmark/subjects/FTP/PureFTPD/`** — PureFTPD (FTP) target with Dockerfile
- **`benchmark/scripts/`** — ProFuzzBench orchestration scripts

### Key Runtime Environment Variables

- `LLM_URL`, `LLM_TOKEN`, `LLM_MODEL` — LLM API credentials (passed into Docker containers)
- `AFL_LLM_VALIDATION` — 0=disabled, 1=enabled
- `AFL_LLM_VALIDATION_STRICT` — 0=format-only, 1=full (grammar+context)
- `AFL_LLM_POST_GAIN` — enable post-execution gain attribution
- `AFL_LLM_FEEDBACK` — enable feedback retry on validation failure
- `AFL_LLM_FEEDBACK_MAX_RETRIES` — max retry count (default 3)
- `AFL_LLM_SKIP_STARTUP` — skip LLM grammar extraction at startup

## Code Conventions

- C code uses AFL-style conventions: `u8`/`u16`/`u32`/`u64` types from `types.h`, `ck_alloc()`/`ck_free()` memory management from `alloc-inl.h`
- LLM prompt construction uses fixed-size buffers (`MAX_PROMPT_LENGTH 8192`)
- Protocol-specific code is guarded by `strcmp` on protocol name strings (e.g., `"RTSP"`, `"FTP"`)
- Validation results use `llm_validation_result_t` enum; error descriptions via `get_validation_error_detail()`
