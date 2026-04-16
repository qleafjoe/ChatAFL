# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

ChatAFL is a protocol fuzzer guided by large language models (LLMs), built on top of AFLNet. It integrates three LLM-powered components: structure-aware mutation (grammar extraction), initial seed enrichment, and state stall handling (breaking coverage plateaus).

## Project Structure

```
ChatAFL-Artifact/
├── aflnet/          # Modified AFLNet that outputs states and state transitions
├── ChatAFL/         # Full ChatAFL with all three strategies
├── ChatAFL-CL1/     # Ablation: structure-aware mutations only
├── ChatAFL-CL2/     # Ablation: structure-aware + seed enrichment only
├── benchmark/       # Modified ProfuzzBench for evaluation
├── aflnet/tutorials/# Protocol-specific fuzzing tutorials (Live555, FTP, etc.)
└── run.sh, analyze.sh, setup.sh, clean.sh, deps.sh  # Artifact scripts
```

Each fuzzer directory (ChatAFL, ChatAFL-CL1, ChatAFL-CL2, aflnet) is an independent AFL build with its own Makefile.

## Building

```bash
cd ChatAFL-CL1   # or ChatAFL, ChatAFL-CL2, aflnet
make clean all
```

**Prerequisites:** clang, graphviz-dev, libcap-dev

**Note:** `afl-fuzz` links against `-lcurl -ljson-c -lpcre2-8`. Set `AFL_NO_X86=1` to skip x86 compilation checks on non-x86 systems.

## Key Source Files

| File | Purpose |
|------|---------|
| `afl-fuzz.c` | Main fuzzer entry point and mutation logic |
| `aflnet.c/h` | Network protocol support (state machine learning, message parsing) |
| `chat-llm.c/h` | LLM integration (grammar extraction, seed enrichment, stall handling) |
| `config.h` | AFL configuration; ChatAFL adds `EPSILON_CHOICE`, `UNINTERESTING_THRESHOLD`, `CHATTING_THRESHOLD` |

## ChatAFL-Specific Parameters

**In `config.h`:**
- `EPSILON_CHOICE` - Threshold for selecting grammar-based mutation over random mutation
- `UNINTERESTING_THRESHOLD` (512) - Cycles without coverage gain before triggering stall handling
- `CHATTING_THRESHOLD` (0) - Max LLM stall interventions before accepting plateau

**In `chat-llm.h`:**
- `STALL_RETRIES` (2) - Max retries for state stall messages
- `GRAMMAR_RETRIES` (5) - Max retries for grammar extraction
- `MESSAGE_TYPE_RETRIES` (5) - Max retries for message type extraction
- `ENRICHMENT_RETRIES` (5) - Max retries per seed enrichment
- `MAX_ENRICHMENT_MESSAGE_TYPES` (2) - Max new message types to add per enrichment
- `MAX_ENRICHMENT_CORPUS_SIZE` (10) - Max seeds to examine for enrichment

## Architecture Notes

- **Grammar extraction**: `setup_llm_grammars()` in `afl-fuzz.c` uses LLM to extract protocol grammar for structure-aware mutation
- **Seed enrichment**: `get_seeds_with_messsage_types()` enriches initial corpus using LLM
- **State stall handling**: `fuzz_one()` triggers LLM consultation when `uninteresting_times >= UNINTERESTING_THRESHOLD && chat_times < CHATTING_THRESHOLD`
- **AFLNet protocol support**: Uses response codes to infer state machine; supports RTSP, FTP, DTLS12, DNS, DICOM, SMTP, SSH, TLS, DAAP-HTTP, SIP

## Running Experiments

```bash
# Install dependencies
./deps.sh

# Build docker images (requires OPENAI_API_KEY)
KEY=<key> ./setup.sh

# Run fuzzers
./run.sh <containers> <minutes> <subjects> <fuzzers>
# Example: ./run.sh 1 5 pure-ftpd chatafl

# Analyze results
./analyze.sh <subjects> <fuzzed_time>
```
