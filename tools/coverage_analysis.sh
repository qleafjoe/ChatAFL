#!/bin/bash
# tools/coverage_analysis.sh
# 覆盖率分析脚本

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
check_dependencies() {
    log_info "检查依赖..."

    if ! command -v gcov &> /dev/null; then
        log_error "gcov 未安装，请安装 gcc"
        exit 1
    fi

    if ! command -v lcov &> /dev/null; then
        log_error "lcov 未安装，请安装 lcov"
        exit 1
    fi

    log_info "依赖检查通过"
}

# 清理之前的覆盖率数据
cleanup_coverage() {
    log_info "清理之前的覆盖率数据..."
    rm -f *.gcda *.gcno *.gcov
    rm -rf coverage_report
}

# 编译带覆盖率插桩的代码
compile_with_coverage() {
    local variant=$1
    log_info "编译变体 $variant (带覆盖率插桩)..."

    cd ChatAFL

    # 备份原始 Makefile
    cp Makefile Makefile.bak

    # 添加覆盖率编译选项
    sed -i 's/CFLAGS =/CFLAGS = -fprofile-arcs -ftest-coverage/g' Makefile
    sed -i 's/LDFLAGS =/LDFLAGS = -lgcov/g' Makefile

    # 清理并重新编译
    make clean
    make

    # 恢复原始 Makefile
    mv Makefile.bak Makefile

    cd ..

    log_info "编译完成"
}

# 收集覆盖率数据
collect_coverage() {
    local variant=$1
    local duration=$2
    local target=$3

    log_info "收集覆盖率数据 (变体: $variant, 时长: ${duration}s, 目标: $target)..."

    # 创建输出目录
    mkdir -p coverage_data/$variant

    # 运行 fuzzer
    timeout $duration ./ChatAFL/afl-fuzz -i testcases -o coverage_data/$variant -x dictionaries/$target.dict -- ./$target &

    # 等待 fuzzer 启动
    sleep 5

    # 收集覆盖率数据
    while ps -p $! > /dev/null 2>&1; do
        gcov -b ChatAFL/*.c > /dev/null 2>&1
        sleep 10
    done

    log_info "覆盖率数据收集完成"
}

# 生成覆盖率报告
generate_report() {
    local variant=$1

    log_info "生成覆盖率报告 (变体: $variant)..."

    # 使用 lcov 收集数据
    lcov --capture --directory ChatAFL --output-file coverage_data/$variant/coverage.info

    # 生成 HTML 报告
    genhtml coverage_data/$variant/coverage.info --output-directory coverage_report/$variant

    log_info "覆盖率报告已生成: coverage_report/$variant/index.html"
}

# 主函数
main() {
    local variant=${1:-"v0"}
    local duration=${2:-60}
    local target=${3:-"rtsp"}

    log_info "开始覆盖率分析..."
    log_info "变体: $variant"
    log_info "时长: ${duration}s"
    log_info "目标: $target"

    check_dependencies
    cleanup_coverage
    compile_with_coverage $variant
    collect_coverage $variant $duration $target
    generate_report $variant

    log_info "覆盖率分析完成"
}

# 使用说明
usage() {
    echo "用法: $0 <变体> <时长> <目标>"
    echo "  变体: v0, v1, v2 (默认: v0)"
    echo "  时长: 运行时间，秒 (默认: 60)"
    echo "  目标: rtsp, ftp, http (默认: rtsp)"
    echo ""
    echo "示例:"
    echo "  $0 v0 120 rtsp"
    echo "  $0 v1 60 ftp"
}

# 解析参数
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
    exit 0
fi

main "$@"
