#!/bin/bash
# Setup script: copy fuzzer sources to ONLY PureFTPD and Live555 benchmark dirs
# Uses rsync to skip build artifacts for speed

set -e

EXCLUDES="--exclude='*.o' --exclude='*.so' --exclude='test_llm' --exclude='aflnet-client' --exclude='afl-fuzz' --exclude='afl-gcc' --exclude='afl-g++' --exclude='afl-clang' --exclude='afl-clang++' --exclude='afl-clang-fast' --exclude='afl-clang-fast++' --exclude='afl-as' --exclude='as' --exclude='afl-showmap' --exclude='afl-tmin' --exclude='afl-gotcpu' --exclude='afl-analyze' --exclude='afl-replay' --exclude='aflnet-replay' --exclude='test-instr' --exclude='.test-instr*' --exclude='out_dir' --exclude='*.stackdump' --exclude='core' --exclude='core.*'"

TARGETS=(
  "./benchmark/subjects/FTP/PureFTPD"
  "./benchmark/subjects/RTSP/Live555"
)

FUZZERS=(
  "aflnet:aflnet"
  "ChatAFL:chatafl"
  "ChatAFL-CL1:chatafl-cl1"
  "ChatAFL-CL2:chatafl-cl2"
)

for subject in "${TARGETS[@]}"; do
  echo "=== Processing $subject ==="
  for pair in "${FUZZERS[@]}"; do
    SRC="${pair%%:*}"
    DST="${pair##*:}"
    echo "  Syncing $SRC -> $subject/$DST ..."
    rm -rf "$subject/$DST"
    eval rsync -a $EXCLUDES "$SRC/" "$subject/$DST/"
  done
  echo "  Done."
done

echo ""
echo "=== All fuzzer sources copied. Ready to build Docker images. ==="
