#!/bin/bash
# sync_to_benchmark.sh
# 将主目录的源代码修改同步到 benchmark Docker 构建上下文目录

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SRC_DIR="$PROJECT_ROOT/ChatAFL"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 需要同步的文件（这些文件在所有变体中共享）
SHARED_FILES=(
    "aflnet.c"
    "aflnet.h"
    "Makefile"
)

# 需要同步到主版本和 V1/V2 的文件（V0 有不同版本）
MAIN_FILES=(
    "chat-llm.c"
    "afl-fuzz.c"
    "llm-validator.c"
    "llm-validator.h"
)

sync_shared_files() {
    local target_dir=$1
    for f in "${SHARED_FILES[@]}"; do
        if [ -f "$SRC_DIR/$f" ]; then
            cp "$SRC_DIR/$f" "$target_dir/$f"
            log_info "  Synced $f"
        fi
    done
}

sync_main_files() {
    local target_dir=$1
    for f in "${MAIN_FILES[@]}"; do
        if [ -f "$SRC_DIR/$f" ]; then
            cp "$SRC_DIR/$f" "$target_dir/$f"
            log_info "  Synced $f"
        fi
    done
}

# 同步到所有 benchmark 目录
for subject_dir in "$PROJECT_ROOT"/benchmark/subjects/*/*/; do
    subject_name=$(basename "$(dirname "$subject_dir")")/$(basename "$subject_dir")
    log_info "Processing subject: $subject_name"

    for variant in chatafl chatafl-v0 chatafl-v1 chatafl-v2; do
        variant_dir="${subject_dir}${variant}"
        if [ ! -d "$variant_dir" ]; then
            log_warn "  Skipping $variant (not found)"
            continue
        fi

        log_info "  Syncing to $variant..."

        # 共享文件：同步到所有变体
        sync_shared_files "$variant_dir"

        # 主版本文件：只同步到 chatafl, v1, v2（v0 有不同实现）
        if [ "$variant" != "chatafl-v0" ]; then
            sync_main_files "$variant_dir"
        else
            log_warn "  Skipping chat-llm.c and afl-fuzz.c for v0 (different implementation)"
        fi
    done
done

log_info "Sync complete!"
log_info ""
log_info "Next steps:"
log_info "  1. Rebuild Docker images:"
log_info "     cd benchmark/subjects/RTSP/Live555 && docker build -t chatafl-live555 ."
log_info "     cd benchmark/subjects/FTP/PureFTPD && docker build -t chatafl-pureftpd ."
log_info "  2. Run experiments with the updated images"
