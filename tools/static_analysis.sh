#!/bin/bash
# tools/static_analysis.sh
# 静态分析脚本

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

    if ! command -v cppcheck &> /dev/null; then
        log_error "cppcheck 未安装，请安装 cppcheck"
        exit 1
    fi

    log_info "依赖检查通过"
}

# 运行 cppcheck 静态分析
run_cppcheck() {
    local output_dir=$1

    log_info "运行 cppcheck 静态分析..."

    mkdir -p $output_dir

    # 分析 ChatAFL 代码
    cppcheck --enable=all --inconclusive --std=c11 \
        --suppress=missingIncludeSystem \
        --suppress=unusedFunction \
        --suppress=unmatchedSuppression \
        --output-file=$output_dir/cppcheck_chat.txt \
        ChatAFL/*.c ChatAFL/*.h

    # 分析 V0 变体
    cppcheck --enable=all --inconclusive --std=c11 \
        --suppress=missingIncludeSystem \
        --suppress=unusedFunction \
        --suppress=unmatchedSuppression \
        --output-file=$output_dir/cppcheck_v0.txt \
        ChatAFL-V0/*.c ChatAFL-V0/*.h

    log_info "cppcheck 分析完成"
}

# 分析未使用的变量和函数
analyze_unused() {
    local output_dir=$1

    log_info "分析未使用的变量和函数..."

    # 使用 gcc 的 -Wunused 选项
    gcc -Wunused -fsyntax-only ChatAFL/*.c 2> $output_dir/unused_chat.txt || true
    gcc -Wunused -fsyntax-only ChatAFL-V0/*.c 2> $output_dir/unused_v0.txt || true

    log_info "未使用代码分析完成"
}

# 分析重复代码
analyze_duplicates() {
    local output_dir=$1

    log_info "分析重复代码..."

    # 比较 V0 和 V1 的差异
    diff -u ChatAFL-V0/afl-fuzz.c ChatAFL-V1/afl-fuzz.c > $output_dir/diff_v0_v1.txt || true

    # 比较 V1 和 V2 的差异
    diff -u ChatAFL-V1/afl-fuzz.c ChatAFL-V2/afl-fuzz.c > $output_dir/diff_v1_v2.txt || true

    log_info "重复代码分析完成"
}

# 生成分析报告
generate_report() {
    local output_dir=$1

    log_info "生成静态分析报告..."

    cat > $output_dir/report.md << EOF
# 静态分析报告

## 1. cppcheck 分析结果

### ChatAFL 主代码
\`\`\`
$(cat $output_dir/cppcheck_chat.txt)
\`\`\`

### V0 变体
\`\`\`
$(cat $output_dir/cppcheck_v0.txt)
\`\`\`

## 2. 未使用代码分析

### ChatAFL 主代码
\`\`\`
$(cat $output_dir/unused_chat.txt)
\`\`\`

### V0 变体
\`\`\`
$(cat $output_dir/unused_v0.txt)
\`\`\`

## 3. 代码差异分析

### V0 vs V1
\`\`\`diff
$(head -100 $output_dir/diff_v0_v1.txt)
\`\`\`

### V1 vs V2
\`\`\`diff
$(head -100 $output_dir/diff_v1_v2.txt)
\`\`\`
EOF

    log_info "静态分析报告已生成: $output_dir/report.md"
}

# 主函数
main() {
    local output_dir=${1:-"static_analysis_results"}

    log_info "开始静态分析..."
    log_info "输出目录: $output_dir"

    check_dependencies
    run_cppcheck $output_dir
    analyze_unused $output_dir
    analyze_duplicates $output_dir
    generate_report $output_dir

    log_info "静态分析完成"
}

# 使用说明
usage() {
    echo "用法: $0 [输出目录]"
    echo "  输出目录: 分析结果保存目录 (默认: static_analysis_results)"
    echo ""
    echo "示例:"
    echo "  $0"
    echo "  $0 my_analysis"
}

# 解析参数
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
    exit 0
fi

main "$@"
