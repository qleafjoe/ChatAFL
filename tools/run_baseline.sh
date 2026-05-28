#!/bin/bash
# tools/run_baseline.sh
# 基线实验脚本

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

# 运行单个变体实验
run_variant() {
    local variant=$1
    local duration=$2
    local target=$3
    local output_dir=$4

    log_info "运行变体 $variant (时长: ${duration}s, 目标: $target)..."

    # 创建输出目录
    mkdir -p $output_dir/$variant

    # 根据变体选择目录
    local variant_dir=""
    case $variant in
        v0)
            variant_dir="ChatAFL-V0"
            ;;
        v1)
            variant_dir="ChatAFL-V1"
            ;;
        v2)
            variant_dir="ChatAFL-V2"
            ;;
        *)
            log_error "未知变体: $variant"
            exit 1
            ;;
    esac

    # 运行 fuzzer
    timeout $duration ./$variant_dir/afl-fuzz \
        -i testcases \
        -o $output_dir/$variant \
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
            log_info "变体 $variant 已运行 ${elapsed}s"

            # 读取 fuzzer 统计信息
            if [ -f "$output_dir/$variant/fuzzer_stats" ]; then
                paths=$(grep "paths_total" $output_dir/$variant/fuzzer_stats | cut -d: -f2 | tr -d ' ')
                edges=$(grep "edges_found" $output_dir/$variant/fuzzer_stats | cut -d: -f2 | tr -d ' ')
                log_info "  路径数: $paths, 边数: $edges"
            fi
        fi

        sleep 10
    done

    log_info "变体 $variant 实验完成"
}

# 收集实验结果
collect_results() {
    local output_dir=$1

    log_info "收集实验结果..."

    # 创建结果汇总文件
    cat > $output_dir/summary.md << EOF
# 基线实验结果汇总

## 实验配置
- 时间: $(date)
- 输出目录: $output_dir

## 变体结果

EOF

    # 遍历所有变体结果
    for variant_dir in $output_dir/v*; do
        if [ -d "$variant_dir" ]; then
            variant=$(basename $variant_dir)

            cat >> $output_dir/summary.md << EOF
### $variant

EOF

            # 读取 fuzzer 统计信息
            if [ -f "$variant_dir/fuzzer_stats" ]; then
                paths=$(grep "paths_total" $variant_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')
                edges=$(grep "edges_found" $variant_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')
                execs=$(grep "execs_done" $variant_dir/fuzzer_stats | cut -d: -f2 | tr -d ' ')

                cat >> $output_dir/summary.md << EOF
- 路径数: $paths
- 边数: $edges
- 执行次数: $execs

EOF
            else
                cat >> $output_dir/summary.md << EOF
- 无统计数据

EOF
            fi
        fi
    done

    log_info "实验结果汇总已生成: $output_dir/summary.md"
}

# 主函数
main() {
    local duration=${1:-300}
    local target=${2:-"rtsp"}
    local output_dir=${3:-"baseline_results"}

    log_info "开始基线实验..."
    log_info "时长: ${duration}s"
    log_info "目标: $target"
    log_info "输出目录: $output_dir"

    # 运行所有变体实验
    run_variant "v0" $duration $target $output_dir
    run_variant "v1" $duration $target $output_dir
    run_variant "v2" $duration $target $output_dir

    # 收集实验结果
    collect_results $output_dir

    log_info "基线实验完成"
}

# 使用说明
usage() {
    echo "用法: $0 [时长] [目标] [输出目录]"
    echo "  时长: 每个变体的运行时间，秒 (默认: 300)"
    echo "  目标: rtsp, ftp, http (默认: rtsp)"
    echo "  输出目录: 结果保存目录 (默认: baseline_results)"
    echo ""
    echo "示例:"
    echo "  $0"
    echo "  $0 600 ftp my_results"
}

# 解析参数
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage
    exit 0
fi

main "$@"
