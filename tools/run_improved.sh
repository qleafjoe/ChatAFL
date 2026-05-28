#!/bin/bash
# tools/run_improved.sh
# 改进后实验脚本

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

# 运行改进后的实验
run_improved_experiment() {
    local duration=$1
    local target=$2
    local output_dir=$3

    log_info "运行改进后的实验..."
    log_info "时长: ${duration}s"
    log_info "目标: $target"
    log_info "输出目录: $output_dir"

    # 创建输出目录
    mkdir -p $output_dir

    # 运行改进后的 fuzzer
    timeout $duration ./ChatAFL/afl-fuzz \
        -i testcases \
        -o $output_dir \
        -x dictionaries/$target.dict \
        -- ./$target &

    # 等待 fuzzer 启动
    sleep 5

    # 记录开始时间
    start_time=$(date +%s)

    # 监控进度
    while ps -p $! > /dev/null 2>&1; do
        current_time=$(date +%s)
        elapsed=$((current_time - start_time))

        # 每 60 秒输出一次状态
        if [ $((elapsed % 60)) -eq 0 ]; then
            log_info "已运行 ${elapsed}s"

            # 读取 fuzzer 统计信息
            if [ -f "$output_dir/fuzzer_stats" ]; then
                paths=$(grep "paths_total" $output_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')
                edges=$(grep "edges_found" $output_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')
                log_info "  路径数: $paths, 边数: $edges"
            fi
        fi

        sleep 10
    done

    log_info "实验完成"
}

# 生成对比报告
generate_comparison_report() {
    local baseline_dir=$1
    local improved_dir=$2
    local output_file=$3

    log_info "生成对比报告..."

    cat > $output_file << EOF
# 实验对比报告

## 实验配置
- 时间: $(date)
- 基线目录: $baseline_dir
- 改进目录: $improved_dir

## 结果对比

### 路径覆盖
- 基线: $(grep "paths_total" $baseline_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')
- 改进: $(grep "paths_total" $improved_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')
- 提升: $(($(grep "paths_total" $improved_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ') - $(grep "paths_total" $baseline_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')))

### 边覆盖
- 基线: $(grep "edges_found" $baseline_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')
- 改进: $(grep "edges_found" $improved_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')
- 提升: $(($(grep "edges_found" $improved_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ') - $(grep "edges_found" $baseline_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')))

### 执行次数
- 基线: $(grep "execs_done" $baseline_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')
- 改进: $(grep "execs_done" $improved_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')
- 提升: $(($(grep "execs_done" $improved_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ') - $(grep "execs_done" $baseline_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')))

## 改进分析

### 路径覆盖率提升
$(echo "scale=2; ($(grep "paths_total" $improved_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ') - $(grep "paths_total" $baseline_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')) * 100 / $(grep "paths_total" $baseline_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')" | bc)%

### 边覆盖率提升
$(echo "scale=2; ($(grep "edges_found" $improved_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ') - $(grep "edges_found" $baseline_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')) * 100 / $(grep "edges_found" $baseline_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')" | bc)%
EOF

    log_info "对比报告已生成: $output_file"
}

# 主函数
main() {
    local duration=${1:-300}
    local target=${2:-"rtsp"}
    local baseline_dir=${3:-"baseline_results/v0"}
    local improved_dir=${4:-"improved_results"}

    log_info "开始改进后实验..."

    # 运行改进后的实验
    run_improved_experiment $duration $target $improved_dir

    # 生成对比报告
    generate_comparison_report $baseline_dir $improved_dir "$improved_dir/comparison.md"

    log_info "改进后实验完成"
}

# 使用说明
usage() {
    echo "用法: $0 [时长] [目标] [基线目录] [改进目录]"
    echo "  时长: 运行时间，秒 (默认: 300)"
    echo "  目标: rtsp, ftp, http (默认: rtsp)"
    echo "  基线目录: 基线结果目录 (默认: baseline_results/v0)"
    echo "  改进目录: 改进结果目录 (默认: improved_results)"
    echo ""
    echo "示例:"
    echo "  $0"
    echo "  $0 600 ftp baseline_results/v1 my_improved"
}

# 解析参数
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
    exit 0
fi

main "$@"
