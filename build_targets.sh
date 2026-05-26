#!/bin/bash
set -e

echo "============================================"
echo " ChatAFL Docker Image Builder"
echo " Targets: PureFTPD, Live555"
echo "============================================"

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"

# ---- Step 1: Copy fuzzer sources to target dirs ----
echo ""
echo "[Step 1] Copying fuzzer sources to benchmark dirs..."

TARGETS=(
  "$REPO_ROOT/benchmark/subjects/FTP/PureFTPD"
  "$REPO_ROOT/benchmark/subjects/RTSP/Live555"
)

FUZZERS=(
  "aflnet:aflnet"
  "ChatAFL:chatafl"
  "ChatAFL-V0:chatafl-v0"
)

# V1 and V2 share ChatAFL source with only env.sh overlay
OVERLAY_VARIANTS=(
  "ChatAFL-V1:chatafl-v1"
  "ChatAFL-V2:chatafl-v2"
)

for subject in "${TARGETS[@]}"; do
  subname=$(basename "$subject")
  for pair in "${FUZZERS[@]}"; do
    SRC="${pair%%:*}"
    DST="${pair##*:}"
    echo "  [$subname] Syncing $SRC -> $DST"
    rm -rf "$subject/$DST"
    rsync -a \
      --exclude='*.o' --exclude='*.so' \
      --exclude='test/' --exclude='test_llm' --exclude='aflnet-client' \
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
      "$REPO_ROOT/$SRC/" "$subject/$DST/"
  done
done

# Overlay variants: rsync ChatAFL base first, then variant-specific files
for subject in "${TARGETS[@]}"; do
  subname=$(basename "$subject")
  for pair in "${OVERLAY_VARIANTS[@]}"; do
    SRC="${pair%%:*}"
    DST="${pair##*:}"
    echo "  [$subname] Overlaying ChatAFL -> $DST, then $SRC -> $DST"
    rm -rf "$subject/$DST"
    # First: copy ChatAFL base source (same exclusions as above)
    rsync -a \
      --exclude='*.o' --exclude='*.so' \
      --exclude='test/' --exclude='test_llm' --exclude='aflnet-client' \
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
    # Second: overlay variant-specific files (env.sh)
    rsync -a "$REPO_ROOT/$SRC/" "$subject/$DST/"
  done
done

echo "[Step 1] Done."

# ---- Step 2: Build Docker images ----
echo ""
echo "[Step 2] Building Docker images..."

echo ""
echo ">>> Building PureFTPD..."
docker build -t pure-ftpd "$REPO_ROOT/benchmark/subjects/FTP/PureFTPD" 2>&1
echo ">>> PureFTPD build COMPLETE."

echo ""
echo ">>> Building Live555..."
docker build -t live555 "$REPO_ROOT/benchmark/subjects/RTSP/Live555" 2>&1
echo ">>> Live555 build COMPLETE."

echo ""
echo "============================================"
echo " All Docker images built successfully!"
echo "============================================"
echo ""
echo "Next: Run fuzzing with:"
echo "  export LLM_URL=... LLM_TOKEN=... LLM_MODEL=..."
echo "  ./run.sh 1 2 pure-ftpd chatafl"
echo "  ./run.sh 1 2 live555 chatafl"
