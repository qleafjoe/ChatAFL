# CODEBUDDY.md This file provides guidance to CodeBuddy when working with code in this repository.

## Overview

ChatAFL is an LLM-guided protocol fuzzer built on top of AFLNet, published at NDSS 2024. It integrates OpenAI's GPT models to: (1) extract protocol grammars for structure-aware mutation, (2) enrich initial seed corpora with diverse message sequences, and (3) break out of coverage plateaus by prompting the LLM when fuzzing stalls. The artifact runs entirely inside Docker containers, orchestrated via a modified [ProfuzzBench](https://github.com/profuzzbench/profuzzbench) framework.

---

## Common Commands

### Install Host Dependencies
```bash
./deps.sh
```
Installs `docker`, `python3`, `pip3`, `matplotlib`, and `pandas` on the host machine.

### Build All Docker Images (~40 minutes)
```bash
KEY=<OPENAI_API_KEY> ./setup.sh
```
Injects the OpenAI API key into `chat-llm.h` for all three ChatAFL variants, copies each fuzzer source tree into every benchmark subject directory, then calls ProfuzzBench's `profuzzbench_build_all.sh` to build all Docker images.

### Build a Fuzzer Locally (outside Docker)
```bash
cd ChatAFL   # or ChatAFL-CL1, ChatAFL-CL2, aflnet
make clean all
```
Requires system packages: `libcurl4-openssl-dev`, `libjson-c-dev`, `libpcre2-dev`, `graphviz-dev`, `libcap-dev`.

### Run Fuzzing Experiments
```bash
./run.sh <num_containers> <fuzz_minutes> <subjects> <fuzzers>

# Example: 1 container, fuzz pure-ftpd with chatafl for 5 minutes
./run.sh 1 5 pure-ftpd chatafl

# Example: 5 containers, all subjects, all fuzzers for 240 minutes
./run.sh 5 240 all all
```
Results are written to `benchmark/results-<subject>/` as `.tar.gz` archives.

### Analyze Results and Generate Plots
```bash
./analyze.sh <subjects> <fuzz_minutes>

# Example: analyze first 4 hours for exim
./analyze.sh exim 240
```
Produces CSV files and PNG plots (branch and state coverage over time) in a timestamped `res_<subject>_<timestamp>/` directory at the repo root. Default time is 1440 minutes (1 day).

### Clean Up Docker Artifacts
```bash
./clean.sh
```

### Replay a Test Case
```bash
# Inside a ChatAFL build directory
./aflnet-replay <pcap_file> <protocol> <host> <port>
```

---

## Architecture

### Repository Layout

```
ChatAFL/          ← Full ChatAFL (all 3 LLM strategies)
ChatAFL-CL1/      ← Ablation: grammar-based mutation only
ChatAFL-CL2/      ← Ablation: grammar-based mutation + seed enrichment
aflnet/           ← Modified AFLNet baseline (adds state/transition output)
benchmark/
  subjects/       ← One subdirectory per protocol, each with fuzzer-specific run scripts and seeds
    FTP/{BFTPD,LightFTP,ProFTPD,PureFTPD}/
    SMTP/  RTSP/  SIP/  HTTP/  DAAP/
  scripts/
    execution/    ← profuzzbench_exec_all.sh, profuzzbench_build_all.sh
    analysis/     ← profuzzbench_generate_all.sh, CSV + PNG generation
analyze.sh        ← Wrapper that calls analysis scripts and collects output
run.sh            ← Wrapper that calls profuzzbench_exec_all.sh
setup.sh          ← Injects API key, copies fuzzers, builds Docker images
deps.sh           ← Installs host dependencies
```

`ChatAFL`, `ChatAFL-CL1`, and `ChatAFL-CL2` are structurally identical copies of the AFL/AFLNet codebase. The only difference is which LLM strategy hooks are compiled and active in `afl-fuzz.c`.

---

### Core Source Files (inside each ChatAFL variant)

| File | Role |
|---|---|
| `afl-fuzz.c` | Main fuzzer loop (~11 000 lines). Contains all ChatAFL-specific hooks at startup and inside `fuzz_one()`. |
| `aflnet.c` / `aflnet.h` | AFLNet protocol extensions: per-protocol request/response region extraction, state machine tracking, state-based seed selection strategies (`RANDOM_SELECTION`, `ROUND_ROBIN`, `FAVOR`). |
| `chat-llm.c` / `chat-llm.h` | All LLM interaction. Constructs prompts, calls the OpenAI REST API via libcurl, parses JSON responses with json-c, and applies PCRE2 regex matching for grammar templates. |
| `config.h` | Compile-time constants. ChatAFL-specific: `EPSILON_CHOICE` (grammar vs. havoc mutation probability), `UNINTERESTING_THRESHOLD` (stall detection), `CHATTING_THRESHOLD` (max LLM calls per run). |
| `chat-llm.h` | LLM retry limits (`STALL_RETRIES`, `GRAMMAR_RETRIES`, `ENRICHMENT_RETRIES`, etc.), prompt length limits, and the `OPENAI_TOKEN` constant injected by `setup.sh`. |

---

### Three LLM Strategy Integration Points in `afl-fuzz.c`

#### 1. Grammar Extraction — `setup_llm_grammars()`
Called once at fuzzer startup when a `-P <protocol>` flag is provided. Sends a prompt to `gpt-3.5-turbo-instruct` asking for message templates for the target protocol. The returned grammar is parsed into a list of PCRE2 compiled patterns stored in the global `protocol_patterns` (`klist_t(rang)`). During the main mutation loop in `fuzz_one()`, `EPSILON_CHOICE` (default 0.5) controls the probability of grammar-guided mutation (replacing mutable fields matched by the PCRE2 patterns) versus standard AFL havoc mutation.

#### 2. Seed Enrichment — `enrich_testcases()` / `get_seeds_with_messsage_types()`
Also called once at startup, immediately after grammar extraction. For each existing seed file in the input directory, the LLM identifies which protocol message types are absent from the sequence and generates additional messages to fill the gaps. The enriched sequences are written back to the input directory as new seed files named `enriched_<N>_<original_name>`. This populates the fuzzing queue with more diverse starting sequences before the main loop begins.

#### 3. State-Stall Breaking — inside `fuzz_one()`
During fuzzing, `uninteresting_times` is incremented whenever a mutated input does not discover new coverage. When `uninteresting_times >= UNINTERESTING_THRESHOLD` (default 512) and the total LLM call count `chat_times < CHATTING_THRESHOLD` (default 64), the fuzzer calls `gpt-3.5-turbo` with the current request-response history as context, asking for a message likely to advance to a new state. The LLM response is injected into the current message sequence and fuzzed. Prompts and responses are saved to `<out_dir>/stall-interactions/` for post-hoc inspection.

---

### AFLNet State Machine Layer (`aflnet.c`)

AFLNet (and therefore ChatAFL) treats each input file as a **sequence of protocol messages** delimited by message boundaries. For each supported protocol (FTP, SMTP, HTTP, RTSP, SIP, DICOM, DNS, TLS, SSH, …), `aflnet.c` provides:

- `extract_requests_<proto>()` — splits a raw byte buffer into individual request regions (`region_t[]`), each annotated with start/end byte offsets and a `modifiable` flag.
- `extract_response_codes_<proto>()` — parses the server response buffer to extract numeric state codes (e.g., FTP 3-digit codes).

The sequence of response codes forms the **state sequence** for a seed. AFLNet maintains a hash map (`hms`) of observed states (`state_info_t`), tracking coverage, path counts, and per-state seed lists. The `-P <proto>` flag at runtime selects the appropriate function pointers (`extract_requests`, `extract_response_codes`).

---

### Benchmark / ProfuzzBench Integration

Each `benchmark/subjects/<PROTO>/<TARGET>/` directory contains:
- `Dockerfile` — builds the fuzz target with coverage instrumentation (`afl-gcc` or `afl-clang`).
- `run.sh` — launched inside Docker; invokes `afl-fuzz` with the correct flags, then runs `gcovr` to collect branch coverage, and archives results.
- `in-<proto>/` — initial seed corpus (`.raw` files of captured protocol sessions).
- `*.dict` — AFL dictionary for token-level mutations.

`setup.sh` copies the four fuzzer source trees (`aflnet/`, `chatafl/`, `chatafl-cl1/`, `chatafl-cl2/`) into each subject directory before building images, so each Docker image is self-contained.

`run.sh` (root) delegates to `profuzzbench_exec_all.sh`, which creates one Docker container per fuzzer-subject-iteration combination, runs them in parallel up to the container limit, and saves `.tar.gz` result archives to `benchmark/results-<subject>/`.

`analyze.sh` (root) delegates to `profuzzbench_generate_all.sh`, which extracts archives, runs Python scripts to aggregate `cov_over_time.csv` files, and produces PNG plots of average branch/state coverage over time per fuzzer.

---

### Key Parameters to Tune

All in `config.h` and `chat-llm.h` — **require recompile and Docker image rebuild** after changing:

| Parameter | Default | Effect |
|---|---|---|
| `EPSILON_CHOICE` | 0.5 | Probability of grammar-guided vs. havoc mutation |
| `UNINTERESTING_THRESHOLD` | 512 | How many non-improving inputs before LLM stall call |
| `CHATTING_THRESHOLD` | 64 | Max LLM stall calls per fuzzing session |
| `STALL_RETRIES` | 2 | LLM API retries for stall prompts |
| `GRAMMAR_RETRIES` | 5 | LLM API retries for grammar extraction |
| `ENRICHMENT_RETRIES` | 5 | LLM API retries per seed enrichment attempt |
| `MAX_ENRICHMENT_MESSAGE_TYPES` | 2 | Max new message types added per seed |
| `MAX_ENRICHMENT_CORPUS_SIZE` | 10 | Max seeds examined during enrichment |
