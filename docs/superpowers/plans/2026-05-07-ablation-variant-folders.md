# Ablation Variant Folders Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create ChatAFL-V0/V1/V2 variant folders for validation-driven ablation experiments, matching the existing ChatAFL-CL1/CL2 pattern.

**Architecture:** ChatAFL (main) is the single source of truth. V0 is a source-level variant (removes validation code). V1/V2 are lightweight (env.sh only, share ChatAFL source). Build script uses rsync overlay pattern.

**Tech Stack:** Bash, rsync, Docker, Makefile

---

## Variant Definitions

| Variant | Purpose | Code Changes | Runtime Env Vars |
|---------|---------|-------------|-----------------|
| ChatAFL-V0 | Baseline: LLM enabled, no validation | Remove llm-validator, remove CRLF conversion, remove validation calls | (none) |
| ChatAFL-V1 | +Format validation | None (same as ChatAFL) | `AFL_LLM_VALIDATION=1` |
| ChatAFL-V2 | +Full validation (grammar+context) | None (same as ChatAFL) | `AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=1` |

---

### Task 1: Create ChatAFL-V0 Folder

**Files:**
- Create: `ChatAFL-V0/` (directory with modified source files)

**Context:** V0 is the baseline ablation variant. It keeps LLM chat/stall features but removes all validation code and CRLF normalization. This matches how CL1/CL2 work (source-level removal).

- [ ] **Step 1: Copy ChatAFL source to ChatAFL-V0**

```bash
cd /home/leaf/ChatAFL
rsync -a --exclude='*.o' --exclude='*.so' --exclude='afl-fuzz' --exclude='afl-gcc' \
  --exclude='afl-g++' --exclude='afl-clang' --exclude='afl-clang++' \
  --exclude='afl-clang-fast' --exclude='afl-clang-fast++' \
  --exclude='afl-as' --exclude='as' --exclude='afl-showmap' --exclude='afl-tmin' \
  --exclude='afl-gotcpu' --exclude='afl-analyze' --exclude='afl-replay' \
  --exclude='aflnet-replay' --exclude='aflnet-client' \
  --exclude='test-instr' --exclude='.test-instr*' \
  --exclude='out_dir' --exclude='*.stackdump' --exclude='core' --exclude='core.*' \
  --exclude='test_llm' --exclude='test_llm_validator' --exclude='test_llm_integration' \
  ChatAFL/ ChatAFL-V0/
```

- [ ] **Step 2: Remove llm-validator files from V0**

```bash
rm -f ChatAFL-V0/llm-validator.c ChatAFL-V0/llm-validator.h
```

- [ ] **Step 3: Modify afl-fuzz.c in V0 — remove validation includes and globals**

In `ChatAFL-V0/afl-fuzz.c`:
- Remove `#include "llm-validator.h"` (line 46)
- Remove the three global variables (lines 432-434):
  ```c
  u8 afl_llm_validation = 0;
  u8 afl_llm_validation_permissive = 0;
  u8 afl_llm_validation_strict = 0;
  ```
- Remove env var reads (lines 10734-10738):
  ```c
  if (getenv("AFL_LLM_VALIDATION")) afl_llm_validation = 1;
  if (getenv("AFL_LLM_VALIDATION_PERMISSIVE")) afl_llm_validation_permissive = 1;
  if (getenv("AFL_LLM_VALIDATION_STRICT")) afl_llm_validation_strict = 1;
  ```
- Remove `init_validation_log()` call (lines 10756-10758)
- Remove `close_validation_log()` call (lines 10999-11001)
- Remove validation gate in stall breaking (lines 7024-7044)
- Remove validation gate in seed enrichment (lines 2759-2782)
- Remove validation gate in grammar extraction (lines 551-569)

- [ ] **Step 4: Modify chat-llm.c in V0 — remove CRLF conversion**

In `ChatAFL-V0/chat-llm.c`, remove the CRLF conversion block in `clean_llm_response()` (lines 252-281):
```c
    /* Convert LF (\n) to CRLF (\r\n) for protocol messages */
    if (final_res) {
        ...
    }
```

- [ ] **Step 5: Modify Makefile in V0 — remove llm-validator dependencies**

In `ChatAFL-V0/Makefile`:
- Change `afl-fuzz` target from:
  ```
  afl-fuzz: afl-fuzz.c $(COMM_HDR) aflnet.o aflnet.h chat-llm.o chat-llm.h llm-validator.o llm-validator.h | test_x86
  	$(CC) $(CFLAGS) $@.c aflnet.o chat-llm.o llm-validator.o -o $@ $(LDFLAGS) -lcurl -ljson-c -lpcre2-8
  ```
  To:
  ```
  afl-fuzz: afl-fuzz.c $(COMM_HDR) aflnet.o aflnet.h chat-llm.o chat-llm.h | test_x86
  	$(CC) $(CFLAGS) $@.c aflnet.o chat-llm.o -o $@ $(LDFLAGS) -lcurl -ljson-c
  ```
- Remove `llm-validator.o` compilation rule
- Remove `test/test_llm_validator` and `test/test_llm_integration` targets
- Remove `test` and `test-integration` phony targets

- [ ] **Step 6: Verify V0 compiles**

```bash
cd /home/leaf/ChatAFL/ChatAFL-V0 && make clean && make afl-fuzz
```
Expected: Build succeeds without llm-validator

- [ ] **Step 7: Commit**

```bash
cd /home/leaf/ChatAFL
git add ChatAFL-V0/
git commit -m "feat: create ChatAFL-V0 ablation variant (no validation, no CRLF)"
```

---

### Task 2: Create ChatAFL-V1 and V2 Folders

**Files:**
- Create: `ChatAFL-V1/env.sh`
- Create: `ChatAFL-V2/env.sh`

**Context:** V1 and V2 share the same source as ChatAFL. They only differ in runtime environment variables. The env.sh files document which variables to set.

- [ ] **Step 1: Create ChatAFL-V1 directory and env.sh**

```bash
mkdir -p /home/leaf/ChatAFL/ChatAFL-V1
```

Create `ChatAFL-V1/env.sh`:
```bash
#!/bin/bash
# ChatAFL-V1: +Format Validation
# Set these environment variables before running afl-fuzz
export AFL_LLM_VALIDATION=1
```

- [ ] **Step 2: Create ChatAFL-V2 directory and env.sh**

```bash
mkdir -p /home/leaf/ChatAFL/ChatAFL-V2
```

Create `ChatAFL-V2/env.sh`:
```bash
#!/bin/bash
# ChatAFL-V2: +Full Validation (grammar + context)
# Set these environment variables before running afl-fuzz
export AFL_LLM_VALIDATION=1
export AFL_LLM_VALIDATION_STRICT=1
```

- [ ] **Step 3: Commit**

```bash
cd /home/leaf/ChatAFL
git add ChatAFL-V1/ ChatAFL-V2/
git commit -m "feat: create ChatAFL-V1/V2 ablation variant folders (env.sh only)"
```

---

### Task 3: Modify build_targets.sh for Variant Overlay

**Files:**
- Modify: `build_targets.sh`

**Context:** The current script rsyncs ChatAFL, CL1, CL2 to each benchmark dir. We need to add V0, V1, V2 with the overlay pattern: rsync ChatAFL first, then overlay variant files.

- [ ] **Step 1: Read current build_targets.sh**

Read the FUZZERS array and rsync loop.

- [ ] **Step 2: Update FUZZERS array**

Replace the FUZZERS array:
```bash
FUZZERS=(
  "aflnet:aflnet"
  "ChatAFL:chatafl"
  "ChatAFL-V0:chatafl-v0"
  "ChatAFL-V1:chatafl-v1"
  "ChatAFL-V2:chatafl-v2"
)
```

Note: Remove CL1/CL2 if the user confirms they're not needed for this experiment.

- [ ] **Step 3: Add overlay rsync for V1 and V2**

V1 and V2 need ChatAFL source + their env.sh. After the main rsync loop, add:

```bash
# For V1 and V2: overlay ChatAFL source first, then variant-specific files
for subject in "${TARGETS[@]}"; do
  subname=$(basename "$subject")
  for variant in "ChatAFL-V1:chatafl-v1" "ChatAFL-V2:chatafl-v2"; do
    SRC="${variant%%:*}"
    DST="${variant##*:}"
    echo "  [$subname] Overlaying ChatAFL -> $DST, then $SRC -> $DST"
    # First: copy ChatAFL base source
    rsync -a \
      --exclude='*.o' --exclude='*.so' \
      --exclude='test_llm' --exclude='aflnet-client' \
      --exclude='afl-fuzz' --exclude='afl-gcc' --exclude='afl-g++' \
      --exclude='afl-clang' --exclude='afl-clang++' \
      --exclude='afl-clang-fast' --exclude='afl-clang-fast++' \
      --exclude='afl-as' --exclude='as' \
      --exclude='afl-showmap' --exclude='afl-tmin' \
      --exclude='afl-gotcpu' --exclude='afl-analyze' \
      --exclude='afl-replay' --exclude='aflnet-replay' \
      --exclude='test-instr' --exclude='.test-instr*' \
      --exclude='out_dir' --exclude='*.stackdump' \
      --exclude='core' --exclude='core.*' \
      "$REPO_ROOT/ChatAFL/" "$subject/$DST/"
    # Second: overlay variant-specific files
    rsync -a "$REPO_ROOT/$SRC/" "$subject/$DST/"
  done
done
```

- [ ] **Step 4: Commit**

```bash
git add build_targets.sh
git commit -m "feat: add V0/V1/V2 to build_targets.sh with overlay rsync"
```

---

### Task 4: Modify profuzzbench_exec_common.sh to Pass Validation Env Vars

**Files:**
- Modify: `benchmark/scripts/execution/profuzzbench_exec_common.sh`

**Context:** The current docker run only passes LLM_URL, LLM_TOKEN, LLM_MODEL. We need to also pass AFL_LLM_VALIDATION and AFL_LLM_VALIDATION_STRICT.

- [ ] **Step 1: Read current docker run command**

Line 21 of profuzzbench_exec_common.sh.

- [ ] **Step 2: Add validation env vars to docker run**

Change line 21 from:
```bash
id=$(docker run --cpus=1 -e LLM_URL="${LLM_URL}" -e LLM_TOKEN="${LLM_TOKEN}" -e LLM_MODEL="${LLM_MODEL}" -d -it $DOCIMAGE /bin/bash -c "cd ${WORKDIR} && run ${FUZZER} ${OUTDIR} '${OPTIONS}' ${TIMEOUT} ${SKIPCOUNT}")
```
To:
```bash
id=$(docker run --cpus=1 -e LLM_URL="${LLM_URL}" -e LLM_TOKEN="${LLM_TOKEN}" -e LLM_MODEL="${LLM_MODEL}" -e AFL_LLM_VALIDATION="${AFL_LLM_VALIDATION}" -e AFL_LLM_VALIDATION_PERMISSIVE="${AFL_LLM_VALIDATION_PERMISSIVE}" -e AFL_LLM_VALIDATION_STRICT="${AFL_LLM_VALIDATION_STRICT}" -d -it $DOCIMAGE /bin/bash -c "cd ${WORKDIR} && run ${FUZZER} ${OUTDIR} '${OPTIONS}' ${TIMEOUT} ${SKIPCOUNT}")
```

- [ ] **Step 3: Commit**

```bash
git add benchmark/scripts/execution/profuzzbench_exec_common.sh
git commit -m "feat: pass validation env vars to docker container"
```

---

### Task 5: Update profuzzbench_exec_all.sh for V0/V1/V2 Targets

**Files:**
- Modify: `benchmark/scripts/execution/profuzzbench_exec_all.sh`

**Context:** Add chatafl-v0, chatafl-v1, chatafl-v2 to the target/fuzzer execution blocks for each protocol.

- [ ] **Step 1: Read current profuzzbench_exec_all.sh**

Understand the pattern: for each TARGET, for each FUZZER, call profuzzbench_exec_common.sh.

- [ ] **Step 2: Add V0/V1/V2 entries for live555 (RTSP)**

In the live555 block, add after the chatafl entry:
```bash
if [[ $FUZZER == "chatafl-v0" ]] || [[ $FUZZER == "all" ]]
then
    profuzzbench_exec_common.sh live555 $NUM_CONTAINERS results-live555 chatafl-v0 out-live555-chatafl_v0 "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" $TIMEOUT $SKIPCOUNT &
fi

if [[ $FUZZER == "chatafl-v1" ]] || [[ $FUZZER == "all" ]]
then
    profuzzbench_exec_common.sh live555 $NUM_CONTAINERS results-live555 chatafl-v1 out-live555-chatafl_v1 "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" $TIMEOUT $SKIPCOUNT &
fi

if [[ $FUZZER == "chatafl-v2" ]] || [[ $FUZZER == "all" ]]
then
    profuzzbench_exec_common.sh live555 $NUM_CONTAINERS results-live555 chatafl-v2 out-live555-chatafl_v2 "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" $TIMEOUT $SKIPCOUNT &
fi
```

- [ ] **Step 3: Add V0/V1/V2 entries for pure-ftpd (FTP)**

Same pattern as above but with FTP options:
```bash
if [[ $FUZZER == "chatafl-v0" ]] || [[ $FUZZER == "all" ]]
then
    profuzzbench_exec_common.sh pure-ftpd $NUM_CONTAINERS results-pure-ftpd chatafl-v0 out-pure-ftpd-chatafl_v0 "-m none -P FTP -D 10000 -q 3 -s 3 -E -K -t ${TEST_TIMEOUT}+" $TIMEOUT $SKIPCOUNT &
fi
```
(Same for v1, v2)

- [ ] **Step 4: Commit**

```bash
git add benchmark/scripts/execution/profuzzbench_exec_all.sh
git commit -m "feat: add chatafl-v0/v1/v2 to execution targets"
```

---

### Task 6: Update Dockerfiles for V0/V1/V2

**Files:**
- Modify: `benchmark/subjects/RTSP/Live555/Dockerfile`
- Modify: `benchmark/subjects/FTP/PureFTPD/Dockerfile`

**Context:** Each Dockerfile currently COPYs and builds aflnet, chatafl, chatafl-cl1, chatafl-cl2. We need to replace cl1/cl2 with v0/v1/v2.

- [ ] **Step 1: Update Live555 Dockerfile**

Replace the CL1/CL2 COPY+RUN blocks with V0/V1/V2:
```dockerfile
COPY --chown=ubuntu:ubuntu chatafl-v0 chatafl-v0
RUN cd chatafl-v0 && \
    make clean all $MAKE_OPT && \
    cd llvm_mode && make $MAKE_OPT

COPY --chown=ubuntu:ubuntu chatafl-v1 chatafl-v1
RUN cd chatafl-v1 && \
    make clean all $MAKE_OPT && \
    cd llvm_mode && make $MAKE_OPT

COPY --chown=ubuntu:ubuntu chatafl-v2 chatafl-v2
RUN cd chatafl-v2 && \
    make clean all $MAKE_OPT && \
    cd llvm_mode && make $MAKE_OPT
```

Note: V1 and V2 share ChatAFL source (with env.sh overlay), so they compile identically to chatafl. V0 compiles without llm-validator.

- [ ] **Step 2: Update PureFTPD Dockerfile**

Same pattern as Live555.

- [ ] **Step 3: Commit**

```bash
git add benchmark/subjects/RTSP/Live555/Dockerfile benchmark/subjects/FTP/PureFTPD/Dockerfile
git commit -m "feat: update Dockerfiles for V0/V1/V2 variants"
```

---

### Task 7: Final Build Verification

- [ ] **Step 1: Run build_targets.sh**

```bash
cd /home/leaf/ChatAFL && bash build_targets.sh
```
Expected: All variants rsynced, Docker images built successfully.

- [ ] **Step 2: Verify V0 binary has no validation**

```bash
# Check that V0's afl-fuzz doesn't reference validation symbols
nm ChatAFL-V0/afl-fuzz | grep -i "validate_llm\|llm_validation"
```
Expected: No output (validation symbols absent)

- [ ] **Step 3: Verify V1/V2 binaries have validation**

```bash
nm ChatAFL-V1/chatafl-v1/afl-fuzz 2>/dev/null || echo "V1 built from ChatAFL source in Docker"
```

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "chore: ablation variant folders V0/V1/V2 complete"
```

---

## Summary

**Total tasks:** 7
**Key deliverables:**
1. ChatAFL-V0/ — source-level variant (no validation, no CRLF)
2. ChatAFL-V1/ — env.sh only (AFL_LLM_VALIDATION=1)
3. ChatAFL-V2/ — env.sh only (AFL_LLM_VALIDATION=1 + AFL_LLM_VALIDATION_STRICT=1)
4. build_targets.sh — overlay rsync for V1/V2
5. profuzzbench_exec_common.sh — pass validation env vars to docker
6. profuzzbench_exec_all.sh — add v0/v1/v2 execution targets
7. Dockerfiles — build v0/v1/v2 variants

**After completion:**
- `./run.sh 1 2 live555 chatafl-v0` → baseline (no validation)
- `AFL_LLM_VALIDATION=1 ./run.sh 1 2 live555 chatafl-v1` → +format validation
- `AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=1 ./run.sh 1 2 live555 chatafl-v2` → +full validation
