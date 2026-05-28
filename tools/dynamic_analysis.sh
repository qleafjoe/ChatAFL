#!/bin/bash
# tools/dynamic_analysis.sh
# 动态分析脚本

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

    if ! command -v valgrind &> /dev/null; then
        log_error "valgrind 未安装，请安装 valgrind"
        exit 1
    fi

    log_info "依赖检查通过"
}

# 运行 valgrind 内存分析
run_valgrind() {
    local output_dir=$1
    local target=$2

    log_info "运行 valgrind 内存分析..."

    mkdir -p $output_dir

    # 运行 valgrind
    valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
        --log-file=$output_dir/valgrind_chat.txt \
        ./ChatAFL/afl-fuzz -i testcases -o /tmp/fuzz_output -x dictionaries/$target.dict -- ./$target &

    # 等待一段时间
    sleep 30

    # 停止 fuzzer
    pkill -f afl-fuzz || true

    log_info "valgrind 分析完成"
}

# 分析内存泄漏
analyze_memory_leaks() {
    local output_dir=$1

    log_info "分析内存泄漏..."

    # 提取内存泄漏信息
    grep -A 5 "LEAK SUMMARY" $output_dir/valgrind_chat.txt > $output_dir/memory_leaks.txt || true
    grep -A 10 "Invalid" $output_dir/valgrind_chat.txt > $output_dir/invalid_access.txt || true

    log_info "内存泄漏分析完成"
}

# 分析执行时间
analyze_execution_time() {
    local output_dir=$1
    local target=$2

    log_info "分析执行时间..."

    # 运行 time 命令
    { time ./ChatAFL/afl-fuzz -i testcases -o /tmp/fuzz_output -x dictionaries/$target.dict -- ./$target ; } 2> $output_dir/execution_time.txt &

    # 等待一段时间
    sleep 30

    # 停止 fuzzer
    pkill -f afl-fuzz || true

    log_info "执行时间分析完成"
}

# 生成分析报告
generate_report() {
    local output_dir=$1

    log_info "生成动态分析报告..."

    cat > $output_dir/report.md << EOF
# 动态分析报告

## 1. 内存分析

### 内存泄漏
\`\`\`
$(cat $output_dir/memory_leaks.txt 2>/dev/null || echo "无内存泄漏信息")
\`\`\`

### 无效访问
\`\`\`
$(cat $output_dir/invalid_access.txt 2>/dev/null || echo "无无效访问信息")
\`\`\`

## 2. 执行时间分析

\`\`\`
$(cat $output_dir/execution_time.txt 2>/dev/null || echo "无执行时间信息")
\`\`\`

## 3. valgrind 完整输出

\`\`\`
$(head -200 $output_dir/valgrind_chat.txt 2>/dev/null || echo "无 valgrind 输出")
\`\`\`
EOF

    log_info "动态分析报告已生成: $output_dir/report.md"
}

# 主函数
main() {
    local output_dir=${1:-"dynamic_analysis_results"}
    local target=${2:-"rtsp"}

    log_info "开始动态分析..."
    log_info "输出目录: $output_dir"
    log_info "目标: $target"

    check_dependencies
    run_valgrind $output_dir $target
    analyze_memory_leaks $output_dir
    analyze_execution_time $output_dir $target
    generate_report $output_dir

    log_info "动态分析完成"
}

# 使用说明
usage() {
    echo "用法: $0 [输出目录] [目标]"
    echo "  输出目录: 分析结果保存目录 (默认: dynamic_analysis_results)"
    echo "  目标: rtsp, ftp, http (默认: rtsp)"
    echo ""
    echo "示例:"
    echo "  $0"
    echo "  $0 my_analysis ftp"
}

# 解析参数
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
    exit 0
fi

main "$@"
