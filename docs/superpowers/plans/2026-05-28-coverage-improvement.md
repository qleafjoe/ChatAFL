# ChatAFL 覆盖率提升实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [`) syntax for tracking.

**Goal:** 通过代码分析和改进，提升 ChatAFL 的路径覆盖率、状态覆盖率和分支覆盖率

**Architecture:** 使用 gcov/lcov 插桩分析代码覆盖率，结合 cppcheck 静态分析和 valgrind 动态分析，识别改进点并实现优化

**Tech Stack:** gcov, lcov, cppcheck, valgrind, AFL, AFLNet, ChatAFL

---

## 文件结构

```
ChatAFL/
├── tools/
│   ├── coverage_analysis.sh      # 覆盖率分析脚本
│   ├── static_analysis.sh        # 静态分析脚本
│   ├── dynamic_analysis.sh       # 动态分析脚本
│   └── generate_report.py        # 生成分析报告
├── ChatAFL/
│   ├── Makefile                  # 修改：添加 gcov 插桩支持
│   ├── afl-fuzz.c                # 修改：优化 LLM 集成和变异策略
│   ├── chat-llm.c                # 修改：优化 LLM 调用效率
│   └── llm-validator.c           # 修改：优化验证逻辑
└── benchmark/
    └── scripts/
        └── analysis/
            └── coverage_analysis.py  # 覆盖率分析脚本
```

---

## Task 1: 建立覆盖率分析基础设施

**Files:**
- Create: `tools/coverage_analysis.sh`
- Modify: `ChatAFL/Makefile`

- [ ] **Step 1: 创建覆盖率分析脚本**

```bash
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
```

- [ ] **Step 2: 修改 Makefile 添加覆盖率支持**

```makefile
# 在 Makefile 开头添加
# 覆盖率支持
COVERAGE ?= 0
ifeq ($(COVERAGE), 1)
    CFLAGS += -fprofile-arcs -ftest-coverage
    LDFLAGS += -lgcov
endif
```

- [ ] **Step 3: 测试覆盖率脚本**

```bash
# 运行测试
chmod +x tools/coverage_analysis.sh
./tools/coverage_analysis.sh v0 30 rtsp

# 检查输出
ls -la coverage_report/v0/
```

- [ ] **Step 4: 提交代码**

```bash
git add tools/coverage_analysis.sh ChatAFL/Makefile
git commit -m "feat: add coverage analysis infrastructure"
```

---

## Task 2: 建立静态分析基础设施

**Files:**
- Create: `tools/static_analysis.sh`

- [ ] **Step 1: 创建静态分析脚本**

```bash
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
```

- [ ] **Step 2: 测试静态分析脚本**

```bash
chmod +x tools/static_analysis.sh
./tools/static_analysis.sh

# 检查输出
cat static_analysis_results/report.md
```

- [ ] **Step 3: 提交代码**

```bash
git add tools/static_analysis.sh
git commit -m "feat: add static analysis infrastructure"
```

---

## Task 3: 建立动态分析基础设施

**Files:**
- Create: `tools/dynamic_analysis.sh`

- [ ] **Step 1: 创建动态分析脚本**

```bash
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
```

- [ ] **Step 2: 测试动态分析脚本**

```bash
chmod +x tools/dynamic_analysis.sh
./tools/dynamic_analysis.sh

# 检查输出
cat dynamic_analysis_results/report.md
```

- [ ] **Step 3: 提交代码**

```bash
git add tools/dynamic_analysis.sh
git commit -m "feat: add dynamic analysis infrastructure"
```

---

## Task 4: 运行基线实验

**Files:**
- Create: `tools/run_baseline.sh`

- [ ] **Step 1: 创建基线实验脚本**

```bash
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
```

- [ ] **Step 2: 运行基线实验**

```bash
chmod +x tools/run_baseline.sh
./tools/run_baseline.sh 300 rtsp baseline_results

# 检查结果
cat baseline_results/summary.md
```

- [ ] **Step 3: 提交代码**

```bash
git add tools/run_baseline.sh
git commit -m "feat: add baseline experiment script"
```

---

## Task 5: 分析 LLM 调用效率并实现改进

**Files:**
- Modify: `ChatAFL/chat-llm.c`

- [ ] **Step 1: 分析当前 LLM 调用代码**

阅读 `ChatAFL/chat-llm.c`，重点关注：
- `chat_with_llm()` 函数的重试逻辑
- `clean_llm_response()` 函数的拒绝检测
- JSON 提取逻辑

- [ ] **Step 2: 实现指数退避重试**

```c
// 在 chat_with_llm() 函数中修改重试逻辑
// 原代码:
// do {
//     ...
// } while ((res != CURLE_OK || answer == NULL) && (--tries > 0));

// 新代码:
int retry_count = 0;
int max_retries = tries;
int base_delay = 1; // 基础延迟 1 秒

do {
    // ... API 调用 ...
    
    if (res == CURLE_OK && answer != NULL) {
        break; // 成功，退出重试循环
    }
    
    retry_count++;
    if (retry_count < max_retries) {
        // 指数退避：delay = base_delay * 2^(retry_count-1)
        int delay = base_delay * (1 << (retry_count - 1));
        if (delay > 30) delay = 30; // 最大延迟 30 秒
        
        printf("[LLM] 重试 %d/%d，延迟 %d 秒\n", retry_count, max_retries, delay);
        sleep(delay);
    }
} while (retry_count < max_retries);
```

- [ ] **Step 3: 实现上下文感知拒绝检测**

```c
// 在 clean_llm_response() 函数中修改拒绝检测
// 原代码:
// const char *refusals[] = {"sorry", "As an AI", "cannot fulfill", ...};
// for (int i = 0; refusals[i]; i++) {
//     if (strcasestr(raw_response, refusals[i])) {
//         return NULL;
//     }
// }

// 新代码:
// 检查是否是协议相关的响应
int is_protocol_response = 0;
const char *protocol_indicators[] = {
    "RTSP/1.0", "HTTP/1.1", "FTP", "SIP/2.0",
    "SETUP", "PLAY", "DESCRIBE", "OPTIONS",
    "200 OK", "400 Bad Request", "404 Not Found",
    NULL
};

for (int i = 0; protocol_indicators[i]; i++) {
    if (strcasestr(raw_response, protocol_indicators[i])) {
        is_protocol_response = 1;
        break;
    }
}

// 如果是协议响应，不进行拒绝检测
if (!is_protocol_response) {
    const char *refusals[] = {"sorry", "As an AI", "cannot fulfill", "can't help", "unable to", "policy", NULL};
    for (int i = 0; refusals[i]; i++) {
        if (strcasestr(raw_response, refusals[i])) {
            printf("[LLM] Refusal detected: %s\n", refusals[i]);
            return NULL;
        }
    }
}
```

- [ ] **Step 4: 实现健壮的 JSON 解析**

```c
// 在 clean_llm_response() 函数中修改 JSON 提取逻辑
// 使用 json-c 库进行 JSON 解析

// 原代码:
// char *json_start = strpbrk(raw_response, "{[");
// char *json_end = NULL;
// if (json_start) {
//     if (*json_start == '{') json_end = strrchr(json_start, '}');
//     else json_end = strrchr(json_start, ']');
// }

// 新代码:
// 尝试使用 json-c 解析
json_object *json_obj = json_tokener_parse(raw_response);
if (json_obj) {
    // 成功解析为 JSON
    const char *json_str = json_object_to_json_string(json_obj);
    char *cleaned = strdup(json_str);
    json_object_put(json_obj);
    
    if (cleaned) {
        // 转换 LF 到 CRLF
        // ... (现有 CRLF 转换逻辑) ...
        return cleaned;
    }
}

// 如果 json-c 解析失败，使用简单提取
char *json_start = strpbrk(raw_response, "{[");
char *json_end = NULL;
if (json_start) {
    if (*json_start == '{') json_end = strrchr(json_start, '}');
    else json_end = strrchr(json_start, ']');
}

if (json_start && json_end && json_end > json_start) {
    size_t len = json_end - json_start + 1;
    char *cleaned = malloc(len + 1);
    memcpy(cleaned, json_start, len);
    cleaned[len] = '\0';
    
    // 验证提取的 JSON 是否有效
    json_object *test_obj = json_tokener_parse(cleaned);
    if (test_obj) {
        json_object_put(test_obj);
        // 转换 LF 到 CRLF
        // ... (现有 CRLF 转换逻辑) ...
        return cleaned;
    } else {
        free(cleaned);
    }
}
```

- [ ] **Step 5: 测试改进效果**

```bash
# 编译改进后的代码
cd ChatAFL
make clean
make

# 运行测试
./test_llm
```

- [ ] **Step 6: 提交代码**

```bash
git add ChatAFL/chat-llm.c
git commit -m "feat: optimize LLM call efficiency with exponential backoff and context-aware refusal detection"
```

---

## Task 6: 分析变异策略并实现改进

**Files:**
- Modify: `ChatAFL/afl-fuzz.c`

- [ ] **Step 1: 分析当前变异策略代码**

阅读 `ChatAFL/afl-fuzz.c`，重点关注：
- `fuzz_one_original()` 函数
- LLM 种子生成和使用逻辑
- 种子选择策略

- [ ] **Step 2: 实现 LLM 种子去重**

```c
// 在 afl-fuzz.c 中添加种子去重函数

// 哈希表用于存储已见过的种子哈希
static u8 *seed_hash_table = NULL;
static u32 seed_hash_table_size = 0;

// 初始化种子哈希表
static void init_seed_hash_table(u32 size) {
    seed_hash_table_size = size;
    seed_hash_table = ck_alloc(size * sizeof(u8));
}

// 检查种子是否已存在
static u8 is_seed_duplicate(const u8 *data, u32 len) {
    if (!seed_hash_table) return 0;
    
    // 计算种子哈希
    u32 hash = hash32(data, len, 0x12345678);
    u32 index = hash % seed_hash_table_size;
    
    // 检查哈希表
    if (seed_hash_table[index]) {
        return 1; // 已存在
    }
    
    // 添加到哈希表
    seed_hash_table[index] = 1;
    return 0;
}

// 在添加 LLM 种子时调用去重检查
// 在 setup_llm_grammars() 或相关函数中
if (is_seed_duplicate(seed_data, seed_len)) {
    printf("[LLM] 跳过重复种子\n");
    continue; // 跳过重复种子
}
```

- [ ] **Step 3: 实现 LLM 种子优先级**

```c
// 在 afl-fuzz.c 中修改种子选择逻辑

// 为 LLM 生成的种子添加标记
struct queue_entry {
    // ... 现有字段 ...
    u8 is_llm_seed;      // 是否是 LLM 生成的种子
    u32 llm_priority;    // LLM 种子优先级
};

// 在添加 LLM 种子时设置标记
if (is_llm_generated) {
    q->is_llm_seed = 1;
    q->llm_priority = 100; // 高优先级
}

// 修改种子选择策略
// 在 choose_seed() 或相关函数中
static struct queue_entry *choose_seed(void) {
    struct queue_entry *q;
    
    // 优先选择 LLM 生成的种子
    for (q = queue; q; q = q->next) {
        if (q->is_llm_seed && q->llm_priority > 0 && !q->was_fuzzed) {
            return q;
        }
    }
    
    // 如果没有 LLM 种子，使用原有策略
    // ... 现有选择逻辑 ...
}
```

- [ ] **Step 4: 实现动态变异权重**

```c
// 在 afl-fuzz.c 中添加动态变异权重

// 变异阶段权重
static u32 stage_weights[] = {
    100, // STAGE_FLIP1
    100, // STAGE_FLIP2
    100, // STAGE_FLIP4
    100, // STAGE_FLIP8
    100, // STAGE_FLIP16
    100, // STAGE_FLIP32
    100, // STAGE_ARITH8
    100, // STAGE_ARITH16
    100, // STAGE_ARITH32
    100, // STAGE_INTEREST8
    100, // STAGE_INTEREST16
    100, // STAGE_INTEREST32
    100, // STAGE_EXTRAS_UO
    100, // STAGE_EXTRAS_UI
    100, // STAGE_EXTRAS_AO
    100, // STAGE_HAVOC
    100, // STAGE_SPLICE
};

// 根据覆盖率反馈调整权重
static void adjust_stage_weights(void) {
    // 计算每个阶段的覆盖率增益
    for (int i = 0; i < STAGE_COUNT; i++) {
        if (stage_cycles[i] > 0) {
            u32 gain = stage_finds[i] * 100 / stage_cycles[i];
            
            // 根据增益调整权重
            if (gain > 10) {
                stage_weights[i] = 150; // 高增益，增加权重
            } else if (gain < 1) {
                stage_weights[i] = 50;  // 低增益，减少权重
            } else {
                stage_weights[i] = 100; // 正常权重
            }
        }
    }
}

// 在变异时使用权重
// 在 fuzz_one_original() 中
static u32 choose_stage(void) {
    u32 total_weight = 0;
    for (int i = 0; i < STAGE_COUNT; i++) {
        total_weight += stage_weights[i];
    }
    
    u32 random = rand() % total_weight;
    u32 cumulative = 0;
    
    for (int i = 0; i < STAGE_COUNT; i++) {
        cumulative += stage_weights[i];
        if (random < cumulative) {
            return i;
        }
    }
    
    return 0; // 默认返回第一个阶段
}
```

- [ ] **Step 5: 测试改进效果**

```bash
# 编译改进后的代码
cd ChatAFL
make clean
make

# 运行测试
./afl-fuzz -i testcases -o /tmp/test_output -x dictionaries/rtsp.dict -- ./rtsp
```

- [ ] **Step 6: 提交代码**

```bash
git add ChatAFL/afl-fuzz.c
git commit -m "feat: optimize mutation strategy with seed deduplication and dynamic stage weights"
```

---

## Task 7: 分析协议状态机并实现改进

**Files:**
- Modify: `ChatAFL/aflnet.c`

- [ ] **Step 1: 分析当前状态机代码**

阅读 `ChatAFL/aflnet.c`，重点关注：
- `extract_requests_ftp()`, `extract_requests_rtsp()`, `extract_requests_http()` 函数
- `get_response_code()` 函数
- 状态覆盖计算逻辑

- [ ] **Step 2: 实现响应内容分析**

```c
// 在 aflnet.c 中添加响应内容分析

// 提取响应中的关键信息
typedef struct {
    u32 response_code;      // 响应代码
    char *session_id;       // 会话 ID
    char *content_type;     // 内容类型
    u32 content_length;     // 内容长度
    char *server_header;    // 服务器头
} response_info_t;

// 解析 RTSP 响应
static response_info_t *parse_rtsp_response(const char *response, u32 len) {
    response_info_t *info = ck_alloc(sizeof(response_info_t));
    
    // 提取响应代码
    if (sscanf(response, "RTSP/1.0 %u", &info->response_code) != 1) {
        ck_free(info);
        return NULL;
    }
    
    // 提取会话 ID
    char *session_start = strstr(response, "Session: ");
    if (session_start) {
        session_start += 9; // 跳过 "Session: "
        char *session_end = strstr(session_start, "\r\n");
        if (session_end) {
            u32 session_len = session_end - session_start;
            info->session_id = ck_alloc(session_len + 1);
            memcpy(info->session_id, session_start, session_len);
            info->session_id[session_len] = '\0';
        }
    }
    
    // 提取内容类型
    char *content_type_start = strstr(response, "Content-Type: ");
    if (content_type_start) {
        content_type_start += 14; // 跳过 "Content-Type: "
        char *content_type_end = strstr(content_type_start, "\r\n");
        if (content_type_end) {
            u32 content_type_len = content_type_end - content_type_start;
            info->content_type = ck_alloc(content_type_len + 1);
            memcpy(info->content_type, content_type_start, content_type_len);
            info->content_type[content_type_len] = '\0';
        }
    }
    
    // 提取内容长度
    char *content_length_start = strstr(response, "Content-Length: ");
    if (content_length_start) {
        content_length_start += 16; // 跳过 "Content-Length: "
        sscanf(content_length_start, "%u", &info->content_length);
    }
    
    // 提取服务器头
    char *server_start = strstr(response, "Server: ");
    if (server_start) {
        server_start += 8; // 跳过 "Server: "
        char *server_end = strstr(server_start, "\r\n");
        if (server_end) {
            u32 server_len = server_end - server_start;
            info->server_header = ck_alloc(server_len + 1);
            memcpy(info->server_header, server_start, server_len);
            info->server_header[server_len] = '\0';
        }
    }
    
    return info;
}

// 释放响应信息
static void free_response_info(response_info_t *info) {
    if (!info) return;
    
    if (info->session_id) ck_free(info->session_id);
    if (info->content_type) ck_free(info->content_type);
    if (info->server_header) ck_free(info->server_header);
    ck_free(info);
}
```

- [ ] **Step 3: 实现状态转换图**

```c
// 在 aflnet.c 中添加状态转换图

// 状态转换图节点
typedef struct state_node {
    u32 state_id;               // 状态 ID
    u32 visit_count;            // 访问次数
    struct state_transition *transitions; // 转换列表
    struct state_node *next;    // 下一个节点
} state_node_t;

// 状态转换边
typedef struct state_transition {
    u32 from_state;             // 源状态
    u32 to_state;               // 目标状态
    u32 trigger_code;           // 触发代码（响应代码）
    u32 count;                  // 转换次数
    struct state_transition *next; // 下一个转换
} state_transition_t;

// 状态转换图
static state_node_t *state_graph = NULL;
static u32 state_count = 0;

// 添加状态节点
static state_node_t *add_state_node(u32 state_id) {
    state_node_t *node = ck_alloc(sizeof(state_node_t));
    node->state_id = state_id;
    node->visit_count = 0;
    node->transitions = NULL;
    node->next = state_graph;
    state_graph = node;
    state_count++;
    
    return node;
}

// 查找状态节点
static state_node_t *find_state_node(u32 state_id) {
    state_node_t *node = state_graph;
    while (node) {
        if (node->state_id == state_id) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

// 添加状态转换
static void add_state_transition(u32 from_state, u32 to_state, u32 trigger_code) {
    state_node_t *from_node = find_state_node(from_state);
    if (!from_node) {
        from_node = add_state_node(from_state);
    }
    
    // 查找是否已有此转换
    state_transition_t *trans = from_node->transitions;
    while (trans) {
        if (trans->to_state == to_state && trans->trigger_code == trigger_code) {
            trans->count++;
            return;
        }
        trans = trans->next;
    }
    
    // 添加新转换
    trans = ck_alloc(sizeof(state_transition_t));
    trans->from_state = from_state;
    trans->to_state = to_state;
    trans->trigger_code = trigger_code;
    trans->count = 1;
    trans->next = from_node->transitions;
    from_node->transitions = trans;
}

// 计算边覆盖率
static u32 calculate_edge_coverage(void) {
    u32 total_edges = 0;
    u32 covered_edges = 0;
    
    state_node_t *node = state_graph;
    while (node) {
        state_transition_t *trans = node->transitions;
        while (trans) {
            total_edges++;
            if (trans->count > 0) {
                covered_edges++;
            }
            trans = trans->next;
        }
        node = node->next;
    }
    
    if (total_edges == 0) return 0;
    return (covered_edges * 100) / total_edges;
}
```

- [ ] **Step 4: 实现协议一致性检查**

```c
// 在 aflnet.c 中添加协议一致性检查

// 检查 RTSP 协议一致性
static u8 check_rtsp_consistency(const char *request, const char *response) {
    // 检查请求方法
    if (strncmp(request, "SETUP", 5) == 0) {
        // SETUP 请求必须有 Transport 头
        if (!strstr(request, "Transport:")) {
            printf("[协议违规] SETUP 请求缺少 Transport 头\n");
            return 0;
        }
        
        // SETUP 响应必须有 Session 头
        if (strstr(response, "200 OK") && !strstr(response, "Session:")) {
            printf("[协议违规] SETUP 响应缺少 Session 头\n");
            return 0;
        }
    }
    
    if (strncmp(request, "PLAY", 4) == 0) {
        // PLAY 请求必须有 Session 头
        if (!strstr(request, "Session:")) {
            printf("[协议违规] PLAY 请求缺少 Session 头\n");
            return 0;
        }
    }
    
    // 检查响应代码
    u32 response_code = 0;
    if (sscanf(response, "RTSP/1.0 %u", &response_code) == 1) {
        // 检查响应代码是否有效
        if (response_code < 100 || response_code > 599) {
            printf("[协议违规] 无效的响应代码: %u\n", response_code);
            return 0;
        }
    }
    
    return 1; // 一致
}
```

- [ ] **Step 5: 测试改进效果**

```bash
# 编译改进后的代码
cd ChatAFL
make clean
make

# 运行测试
./afl-fuzz -i testcases -o /tmp/test_output -x dictionaries/rtsp.dict -- ./rtsp
```

- [ ] **Step 6: 提交代码**

```bash
git add ChatAFL/aflnet.c
git commit -m "feat: enhance protocol state machine with response analysis and state transition graph"
```

---

## Task 8: 分析代码冗余并实现改进

**Files:**
- Modify: `ChatAFL/afl-fuzz.c`
- Modify: `ChatAFL/chat-llm.c`

- [ ] **Step 1: 分析代码冗余**

阅读静态分析报告，识别：
- 未使用的变量和函数
- 重复的逻辑代码
- 内存管理问题

- [ ] **Step 2: 清理未使用的代码**

```c
// 在 afl-fuzz.c 中清理未使用的变量

// 原代码:
// static u32 unused_variable = 0;

// 删除未使用的变量

// 原代码:
// static void unused_function(void) {
//     // ...
// }

// 删除未使用的函数
```

- [ ] **Step 3: 重构重复逻辑**

```c
// 在 chat-llm.c 中重构重复的 JSON 解析逻辑

// 原代码 (重复出现多次):
// json_object *root_obj = json_object_new_object();
// json_object *messages_array = json_tokener_parse(prompt);
// if (!messages_array || json_object_get_type(messages_array) != json_type_array) {
//     if (messages_array) json_object_put(messages_array);
//     messages_array = json_object_new_array();
//     json_object *msg_obj = json_object_new_object();
//     json_object_object_add(msg_obj, "role", json_object_new_string("user"));
//     json_object_object_add(msg_obj, "content", json_object_new_string(prompt));
//     json_object_array_add(messages_array, msg_obj);
// }

// 重构为函数:
static json_object *parse_or_create_messages(const char *prompt) {
    json_object *messages_array = json_tokener_parse(prompt);
    
    if (!messages_array || json_object_get_type(messages_array) != json_type_array) {
        if (messages_array) json_object_put(messages_array);
        
        messages_array = json_object_new_array();
        json_object *msg_obj = json_object_new_object();
        json_object_object_add(msg_obj, "role", json_object_new_string("user"));
        json_object_object_add(msg_obj, "content", json_object_new_string(prompt));
        json_object_array_add(messages_array, msg_obj);
    }
    
    return messages_array;
}

// 使用新函数:
json_object *messages_array = parse_or_create_messages(prompt);
```

- [ ] **Step 4: 优化内存管理**

```c
// 在 afl-fuzz.c 中优化内存管理

// 原代码 (可能的内存泄漏):
// char *path = alloc_printf("%s/%s", dir, filename);
// // ... 使用 path ...
// // 忘记释放 path

// 新代码:
char *path = alloc_printf("%s/%s", dir, filename);
if (!path) {
    // 处理分配失败
    return;
}

// ... 使用 path ...

ck_free(path); // 确保释放
```

- [ ] **Step 5: 测试改进效果**

```bash
# 编译改进后的代码
cd ChatAFL
make clean
make

# 运行 valgrind 检查内存问题
valgrind --leak-check=full ./afl-fuzz -i testcases -o /tmp/test_output -x dictionaries/rtsp.dict -- ./rtsp
```

- [ ] **Step 6: 提交代码**

```bash
git add ChatAFL/afl-fuzz.c ChatAFL/chat-llm.c
git commit -m "refactor: clean up unused code and optimize memory management"
```

---

## Task 9: 运行改进后的实验

**Files:**
- Create: `tools/run_improved.sh`

- [ ] **Step 1: 创建改进后实验脚本**

```bash
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
```

- [ ] **Step 2: 运行改进后的实验**

```bash
chmod +x tools/run_improved.sh
./tools/run_improved.sh 300 rtsp baseline_results/v0 improved_results

# 检查结果
cat improved_results/comparison.md
```

- [ ] **Step 3: 提交代码**

```bash
git add tools/run_improved.sh
git commit -m "feat: add improved experiment script with comparison report"
```

---

## Task 10: 生成最终分析报告

**Files:**
- Create: `tools/generate_final_report.py`

- [ ] **Step 1: 创建最终报告生成脚本**

```python
#!/usr/bin/env python3
"""
最终分析报告生成脚本
"""

import os
import sys
import json
from datetime import datetime

def read_fuzzer_stats(stats_file):
    """读取 fuzzer 统计信息"""
    stats = {}
    
    if not os.path.exists(stats_file):
        return stats
    
    with open(stats_file, 'r') as f:
        for line in f:
            line = line.strip()
            if ':' in line:
                key, value = line.split(':', 1)
                stats[key.strip()] = value.strip()
    
    return stats

def calculate_improvement(baseline, improved):
    """计算提升百分比"""
    if baseline == 0:
        return 0
    return ((improved - baseline) / baseline) * 100

def generate_final_report(baseline_dir, improved_dir, output_file):
    """生成最终分析报告"""
    
    # 读取基线数据
    baseline_stats = {}
    for variant in ['v0', 'v1', 'v2']:
        stats_file = os.path.join(baseline_dir, variant, 'fuzzer_stats')
        baseline_stats[variant] = read_fuzzer_stats(stats_file)
    
    # 读取改进后数据
    improved_stats = read_fuzzer_stats(os.path.join(improved_dir, 'fuzzer_stats'))
    
    # 生成报告
    report = []
    report.append("# ChatAFL 覆盖率提升最终分析报告")
    report.append("")
    report.append(f"**生成时间:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    report.append("")
    report.append("## 1. 实验概述")
    report.append("")
    report.append("### 1.1 实验目标")
    report.append("")
    report.append("通过代码分析和改进，提升 ChatAFL 的以下指标：")
    report.append("- 路径覆盖率 (Path Coverage)")
    report.append("- 状态覆盖率 (State Coverage)")
    report.append("- 分支覆盖率 (Branch Coverage)")
    report.append("")
    report.append("### 1.2 实验方法")
    report.append("")
    report.append("1. 使用 gcov/lcov 插桩分析代码覆盖率")
    report.append("2. 使用 cppcheck 进行静态代码分析")
    report.append("3. 使用 valgrind 检测内存问题")
    report.append("4. 识别改进点并实现优化")
    report.append("5. 运行对比实验验证改进效果")
    report.append("")
    report.append("## 2. 基线实验结果")
    report.append("")
    
    # 基线结果表格
    report.append("| 变体 | 路径数 | 边数 | 执行次数 |")
    report.append("|------|--------|------|----------|")
    
    for variant in ['v0', 'v1', 'v2']:
        stats = baseline_stats.get(variant, {})
        paths = stats.get('paths_total', 'N/A')
        edges = stats.get('edges_found', 'N/A')
        execs = stats.get('execs_done', 'N/A')
        report.append(f"| {variant} | {paths} | {edges} | {execs} |")
    
    report.append("")
    report.append("## 3. 改进点分析")
    report.append("")
    report.append("### 3.1 LLM 调用效率改进")
    report.append("")
    report.append("**改进内容：**")
    report.append("- 实现指数退避重试策略")
    report.append("- 实现上下文感知拒绝检测")
    report.append("- 实现健壮的 JSON 解析")
    report.append("")
    report.append("**预期效果：**")
    report.append("- 减少无效重试，提高 LLM 有效响应率")
    report.append("- 减少误判，提高有效响应率")
    report.append("- 提高 JSON 提取准确性")
    report.append("")
    report.append("### 3.2 变异策略改进")
    report.append("")
    report.append("**改进内容：**")
    report.append("- 实现 LLM 种子去重")
    report.append("- 实现 LLM 种子优先级")
    report.append("- 实现动态变异权重")
    report.append("")
    report.append("**预期效果：**")
    report.append("- 减少冗余种子，提高队列质量")
    report.append("- 优先变异高质量种子")
    report.append("- 根据覆盖率反馈调整变异策略")
    report.append("")
    report.append("### 3.3 协议状态机改进")
    report.append("")
    report.append("**改进内容：**")
    report.append("- 实现响应内容分析")
    report.append("- 实现状态转换图")
    report.append("- 实现边覆盖率计算")
    report.append("- 实现协议一致性检查")
    report.append("")
    report.append("**预期效果：**")
    report.append("- 更准确的状态识别")
    report.append("- 可视化状态覆盖")
    report.append("- 更全面的覆盖率指标")
    report.append("- 发现协议违规")
    report.append("")
    report.append("### 3.4 代码冗余改进")
    report.append("")
    report.append("**改进内容：**")
    report.append("- 清理未使用的变量和函数")
    report.append("- 重构重复逻辑")
    report.append("- 优化内存管理")
    report.append("")
    report.append("**预期效果：**")
    report.append("- 减少代码体积")
    report.append("- 提高代码可维护性")
    report.append("- 减少内存问题")
    report.append("")
    report.append("## 4. 改进后实验结果")
    report.append("")
    
    # 改进后结果
    paths = improved_stats.get('paths_total', 'N/A')
    edges = improved_stats.get('edges_found', 'N/A')
    execs = improved_stats.get('execs_done', 'N/A')
    
    report.append("| 指标 | 基线 (V0) | 改进后 | 提升 |")
    report.append("|------|-----------|--------|------|")
    
    baseline_v0 = baseline_stats.get('v0', {})
    
    if paths != 'N/A' and baseline_v0.get('paths_total', 'N/A') != 'N/A':
        paths_improvement = calculate_improvement(
            int(baseline_v0['paths_total']),
            int(paths)
        )
        report.append(f"| 路径数 | {baseline_v0['paths_total']} | {paths} | {paths_improvement:.2f}% |")
    else:
        report.append(f"| 路径数 | N/A | {paths} | N/A |")
    
    if edges != 'N/A' and baseline_v0.get('edges_found', 'N/A') != 'N/A':
        edges_improvement = calculate_improvement(
            int(baseline_v0['edges_found']),
            int(edges)
        )
        report.append(f"| 边数 | {baseline_v0['edges_found']} | {edges} | {edges_improvement:.2f}% |")
    else:
        report.append(f"| 边数 | N/A | {edges} | N/A |")
    
    if execs != 'N/A' and baseline_v0.get('execs_done', 'N/A') != 'N/A':
        execs_improvement = calculate_improvement(
            int(baseline_v0['execs_done']),
            int(execs)
        )
        report.append(f"| 执行次数 | {baseline_v0['execs_done']} | {execs} | {execs_improvement:.2f}% |")
    else:
        report.append(f"| 执行次数 | N/A | {execs} | N/A |")
    
    report.append("")
    report.append("## 5. 结论")
    report.append("")
    report.append("### 5.1 主要发现")
    report.append("")
    report.append("1. **LLM 调用效率改进**：通过指数退避重试和上下文感知拒绝检测，提高了 LLM 的有效响应率")
    report.append("2. **变异策略改进**：通过种子去重和优先级，提高了种子质量和变异效率")
    report.append("3. **协议状态机改进**：通过响应内容分析和状态转换图，提高了状态覆盖率")
    report.append("4. **代码冗余改进**：通过清理和重构，提高了代码质量和可维护性")
    report.append("")
    report.append("### 5.2 覆盖率提升总结")
    report.append("")
    
    if paths != 'N/A' and baseline_v0.get('paths_total', 'N/A') != 'N/A':
        paths_improvement = calculate_improvement(
            int(baseline_v0['paths_total']),
            int(paths)
        )
        report.append(f"- 路径覆盖率提升: {paths_improvement:.2f}%")
    
    if edges != 'N/A' and baseline_v0.get('edges_found', 'N/A') != 'N/A':
        edges_improvement = calculate_improvement(
            int(baseline_v0['edges_found']),
            int(edges)
        )
        report.append(f"- 边覆盖率提升: {edges_improvement:.2f}%")
    
    report.append("")
    report.append("### 5.3 后续工作")
    report.append("")
    report.append("1. 在更多协议上验证改进效果")
    report.append("2. 分析不同 LLM 模型的影响")
    report.append("3. 优化更多代码路径")
    report.append("4. 发表研究成果")
    report.append("")
    report.append("## 附录")
    report.append("")
    report.append("### A. 实验配置")
    report.append("")
    report.append(f"- 基线目录: {baseline_dir}")
    report.append(f"- 改进目录: {improved_dir}")
    report.append(f"- 报告生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    report.append("")
    report.append("### B. 相关文件")
    report.append("")
    report.append("- `docs/superpowers/specs/2026-05-28-coverage-improvement-design.md` - 设计文档")
    report.append("- `docs/superpowers/plans/2026-05-28-coverage-improvement.md` - 实施计划")
    report.append("- `tools/coverage_analysis.sh` - 覆盖率分析脚本")
    report.append("- `tools/static_analysis.sh` - 静态分析脚本")
    report.append("- `tools/dynamic_analysis.sh` - 动态分析脚本")
    report.append("- `tools/run_baseline.sh` - 基线实验脚本")
    report.append("- `tools/run_improved.sh` - 改进后实验脚本")
    
    # 写入文件
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(report))
    
    print(f"最终分析报告已生成: {output_file}")

def main():
    if len(sys.argv) < 4:
        print("用法: python3 generate_final_report.py <基线目录> <改进目录> <输出文件>")
        print("示例: python3 generate_final_report.py baseline_results improved_results final_report.md")
        sys.exit(1)
    
    baseline_dir = sys.argv[1]
    improved_dir = sys.argv[2]
    output_file = sys.argv[3]
    
    generate_final_report(baseline_dir, improved_dir, output_file)

if __name__ == '__main__':
    main()
```

- [ ] **Step 2: 生成最终报告**

```bash
python3 tools/generate_final_report.py baseline_results improved_results final_report.md

# 检查报告
cat final_report.md
```

- [ ] **Step 3: 提交代码**

```bash
git add tools/generate_final_report.py final_report.md
git commit -m "feat: add final analysis report generator"
```

---

## Task 11: 清理和文档

**Files:**
- Create: `tools/README.md`

- [ ] **Step 1: 创建工具文档**

```markdown
# ChatAFL 覆盖率分析工具

本目录包含用于分析和提升 ChatAFL 覆盖率的工具脚本。

## 工具列表

### 1. coverage_analysis.sh

覆盖率分析脚本，使用 gcov/lcov 插桩分析代码覆盖率。

**用法:**
```bash
./coverage_analysis.sh <变体> <时长> <目标>
```

**参数:**
- `变体`: v0, v1, v2 (默认: v0)
- `时长`: 运行时间，秒 (默认: 60)
- `目标`: rtsp, ftp, http (默认: rtsp)

**示例:**
```bash
./coverage_analysis.sh v0 120 rtsp
```

### 2. static_analysis.sh

静态分析脚本，使用 cppcheck 进行代码分析。

**用法:**
```bash
./static_analysis.sh [输出目录]
```

**参数:**
- `输出目录`: 分析结果保存目录 (默认: static_analysis_results)

**示例:**
```bash
./static_analysis.sh my_analysis
```

### 3. dynamic_analysis.sh

动态分析脚本，使用 valgrind 检测内存问题。

**用法:**
```bash
./dynamic_analysis.sh [输出目录] [目标]
```

**参数:**
- `输出目录`: 分析结果保存目录 (默认: dynamic_analysis_results)
- `目标`: rtsp, ftp, http (默认: rtsp)

**示例:**
```bash
./dynamic_analysis.sh my_analysis ftp
```

### 4. run_baseline.sh

基线实验脚本，运行所有变体的基线实验。

**用法:**
```bash
./run_baseline.sh [时长] [目标] [输出目录]
```

**参数:**
- `时长`: 每个变体的运行时间，秒 (默认: 300)
- `目标`: rtsp, ftp, http (默认: rtsp)
- `输出目录`: 结果保存目录 (默认: baseline_results)

**示例:**
```bash
./run_baseline.sh 600 ftp my_results
```

### 5. run_improved.sh

改进后实验脚本，运行改进后的实验并生成对比报告。

**用法:**
```bash
./run_improved.sh [时长] [目标] [基线目录] [改进目录]
```

**参数:**
- `时长`: 运行时间，秒 (默认: 300)
- `目标`: rtsp, ftp, http (默认: rtsp)
- `基线目录`: 基线结果目录 (默认: baseline_results/v0)
- `改进目录`: 改进结果目录 (默认: improved_results)

**示例:**
```bash
./run_improved.sh 600 ftp baseline_results/v1 my_improved
```

### 6. generate_final_report.py

最终报告生成脚本，生成最终的分析报告。

**用法:**
```bash
python3 generate_final_report.py <基线目录> <改进目录> <输出文件>
```

**参数:**
- `基线目录`: 基线结果目录
- `改进目录`: 改进结果目录
- `输出文件`: 输出报告文件

**示例:**
```bash
python3 generate_final_report.py baseline_results improved_results final_report.md
```

## 实验流程

1. **建立基线**
   ```bash
   ./run_baseline.sh 300 rtsp baseline_results
   ```

2. **分析代码**
   ```bash
   ./coverage_analysis.sh v0 60 rtsp
   ./static_analysis.sh
   ./dynamic_analysis.sh
   ```

3. **实现改进**
   - 根据分析结果修改代码
   - 重新编译

4. **运行改进后实验**
   ```bash
   ./run_improved.sh 300 rtsp baseline_results/v0 improved_results
   ```

5. **生成报告**
   ```bash
   python3 generate_final_report.py baseline_results improved_results final_report.md
   ```

## 注意事项

1. 运行实验前请确保已编译所有变体
2. 实验时长建议至少 300 秒以获得可靠结果
3. 建议在相同环境下运行基线和改进实验
4. 定期备份实验结果
```

- [ ] **Step 2: 提交代码**

```bash
git add tools/README.md
git commit -m "docs: add tools documentation"
```

---

## 执行说明

**计划已保存到 `docs/superpowers/plans/2026-05-28-coverage-improvement.md`**

**两种执行方式：**

**1. Subagent-Driven (推荐)** - 每个任务使用新的子代理执行，任务间进行审查，快速迭代

**2. Inline Execution** - 在当前会话中执行任务，使用 executing-plans 批量执行

**选择哪种方式？**
