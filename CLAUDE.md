# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ChatAFL is an LLM-guided protocol fuzzer built on AFLNet. It uses LLMs in three ways:
1. **Grammar extraction**: Extract machine-readable protocol grammar for structure-aware mutation
2. **Seed enrichment**: Increase message diversity in initial seeds using historical vulnerability patterns
3. **Coverage plateau breaking**: Generate new messages via LLM when coverage stalls (plateau threshold Max=300)

> **Paper**: 杨立群，李镇，韦超仁，等. 大语言模型引导的协议模糊测试技术研究[J]. 信息网络安全，2025，25(12)：1847-1862.
> Note: This is a **Chinese fork** adapted for MiniMax API. The original ChatAFL (NDSS 2024) used OpenAI.

## Quick Start

```bash
# 1. Set MiniMax API key
export MINIMAX_API_KEY="your-api-key"

# 2. Build Docker images (one-time)
cd benchmark/subjects/FTP/PureFTPD
docker build . -t pure-ftpd --build-arg MINIMAX_API_KEY=$MINIMAX_API_KEY

# 3. Run fuzzing test
cd /path/to/ChatAFL
./run.sh 1 5 pure-ftpd chatafl

# 4. Analyze results
./analyze.sh pure-ftpd 5
```

## Key Commands

| Command | Purpose |
|---------|---------|
| `./run.sh <N> <min> <subject> <fuzzer>` | Run fuzzing: N containers, min minutes, subject, fuzzer |
| `./analyze.sh <subject> [min]` | Analyze results; default 1440 min if not specified |
| `./setup.sh` | Build all Docker images |
| `./clean.sh` | Clean up artifacts |

**Available fuzzers**: `chatafl`, `chatafl-cl1`, `chatafl-cl2`, `aflnet`
**Available subjects**: `pure-ftpd`, `live555`, `kamailio`, `proftpd`, `exim`, `lighttpd1`

## Architecture

```
ChatAFL/
├── ChatAFL/              # Full implementation (all 3 LLM strategies)
│   ├── afl-fuzz.c       # Main fuzzing logic + LLM integration points
│   ├── chat-llm.c/h     # LLM API calls (MiniMax adapted)
│   └── config.h         # Fuzzer parameters
├── ChatAFL-CL1/         # Ablation: structure-aware mutation only
├── ChatAFL-CL2/         # Ablation: structure-aware + seed enrichment
├── aflnet/              # Modified AFLNet base
└── benchmark/
    ├── subjects/        # Protocol server Docker images
    └── scripts/         # profuzzbench execution/analysis
```

## LLM Integration

**Adapted for MiniMax API** (not OpenAI):

| Component | Original (OpenAI) | Current (MiniMax) |
|-----------|-------------------|-------------------|
| Endpoint | api.openai.com | api.minimax.io |
| Model | gpt-3.5-turbo | MiniMax-M2.7 |
| Key env | OPENAI_API_KEY | MINIMAX_API_KEY |

Source: `ChatAFL/chat-llm.c` function `chat_with_llm()`

## Key Parameters

In `ChatAFL/config.h`:
- `UNINTERESTING_THRESHOLD` - plateau detection
- `CHATTING_THRESHOLD` - LLM intervention trigger count

In `ChatAFL/chat-llm.h`:
- `STALL_RETRIES` - LLM retry count for plateau breaking
- `GRAMMAR_RETRIES` - grammar generation retries
- `MAX_ENRICHMENT_MESSAGE_TYPES` - max enriched message types

**Plateau threshold**: Max=300 (from paper experiments)

## Important Notes

1. **⚠️ Run shell scripts in WSL2 Ubuntu, NOT in Windows Git Bash**: All `./run.sh`, `./analyze.sh`, `./setup.sh` etc. must be run inside WSL2 Ubuntu terminal. Running them in Windows Git Bash or Claude Code's bash tool causes path incompatibility errors (e.g., `cannot remove '/e/lunwen/...'`). Always use:
   ```bash
   wsl  # Enter WSL2 first
   cd /mnt/e/lunwen/ChatAFL
   ./run.sh ...
   ```

2. **Line endings**: `.gitattributes` enforces LF for all scripts. If issues arise, run `find . -name "*.sh" -exec dos2unix {} \;` in WSL2.

3. **Docker mode**: Requires Linux containers (not Windows containers). On Windows+WSL2, ensure Docker Desktop is set to Linux container mode.

4. **Results location**: `benchmark/results-<subject>-<fuzzer>/`

5. **Literature discrepancy**: README.md cites NDSS 2024 (original), but this fork's paper is 信息网络安全 2025 (Chinese journal). Both USER-GUIDE.md and SETUP-GUIDE.md reflect the correct Chinese paper.

## Code Locations for LLM Features

| Feature | Source File | Key Function |
|---------|-------------|--------------|
| Grammar extraction | `afl-fuzz.c` | `setup_llm_grammars` |
| Seed enrichment | `afl-fuzz.c` | `get_seeds_with_message_types` |
| Plateau breaking | `afl-fuzz.c` | `fuzz_one` (line ~6846) |
| LLM API calls | `chat-llm.c` | `chat_with_llm` |
