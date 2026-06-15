# TR2 多模型对比实验计划

## 目标
针对 TR2 变体，测试不同 LLM 模型对协议模糊测试效果的影响：
1. **MiniMax-M2.7**（基线模型）
2. **小米 mimo-v2.5-pro**（新模型）

## 模型配置

### MiniMax-M2.7（默认）
```bash
export LLM_URL="https://api.minimaxi.com/v1/text/chatcompletion_v2"
export LLM_TOKEN="sk-cp-EK3rwNPjpttunXcODVKSpsvJh4dySqRdtbgbjcmLxdlSHRyoIuWJzFPXFUr8I8rponL4y-xwRMMcO3eodW7dwfO2hqL3G6cCQBtIufVHuRX11_JV1YK5YFs"
export LLM_MODEL="MiniMax-M2.7"
```

### 小米 mimo-v2.5-pro
```bash
export LLM_URL="https://api.xiaomimimo.com/v1/chat/completions"
export LLM_TOKEN="tp-ck4ub0esxhwcbrtpw7ib8xtzehaylyzb8vuaar8oeka7945u"
export LLM_MODEL="mimo-v2.5-pro"
```

## 实验设计

### 实验矩阵
| 实验 ID | 变体 | 模型 | 目标 | 运行时间 |
|---------|------|------|------|----------|
| tr2-minimax-rtsp | TR2 | MiniMax-M2.7 | Live555 (RTSP) | 10h |
| tr2-xiaomi-rtsp | TR2 | mimo-v2.5-pro | Live555 (RTSP) | 10h |
| tr2-minimax-ftp | TR2 | MiniMax-M2.7 | PureFTPD (FTP) | 10h |
| tr2-xiaomi-ftp | TR2 | mimo-v2.5-pro | PureFTPD (FTP) | 10h |

### 运行规模
- **Smoke 测试**: 1 container, 10 min（验证配置正确）
- **Short 测试**: 3 containers, 120 min（初步效果评估）
- **Full 测试**: 10 containers, 720 min（论文数据收集）

## 执行步骤

### 1. 创建模型配置文件

创建 `benchmark/models/minimax.env`:
```bash
#!/bin/bash
export LLM_URL="https://api.minimaxi.com/v1/text/chatcompletion_v2"
export LLM_TOKEN="sk-cp-EK3rwNPjpttunXcODVKSpsvJh4dySqRdtbgbjcmLxdlSHRyoIuWJzFPXFUr8I8rponL4y-xwRMMcO3eodW7dwfO2hqL3G6cCQBtIufVHuRX11_JV1YK5YFs"
export LLM_MODEL="MiniMax-M2.7"
```

创建 `benchmark/models/xiaomi.env`:
```bash
#!/bin/bash
export LLM_URL="https://api.xiaomimimo.com/v1/chat/completions"
export LLM_TOKEN="tp-ck4ub0esxhwcbrtpw7ib8xtzehaylyzb8vuaar8oeka7945u"
export LLM_MODEL="mimo-v2.5-pro"
```

### 2. 创建运行脚本

创建 `run_model_comparison.sh`:
```bash
#!/bin/bash
set -euo pipefail

MODEL="${1:-minimax}"
MODE="${2:-smoke}"
FUZZERS="chatafl-tr2"

# 加载模型配置
case "$MODEL" in
  minimax)
    source benchmark/models/minimax.env
    EXPERIMENT_PREFIX="tr2-minimax"
    ;;
  xiaomi)
    source benchmark/models/xiaomi.env
    EXPERIMENT_PREFIX="tr2-xiaomi"
    ;;
  *)
    echo "Usage: $0 {minimax|xiaomi} {smoke|short|full}" >&2
    exit 1
    ;;
esac

# 设置运行参数
case "$MODE" in
  smoke)
    NUM_CONTAINERS=1
    TIMEOUT=10
    TARGETS="live555"
    ;;
  short)
    NUM_CONTAINERS=3
    TIMEOUT=120
    TARGETS="live555,pure-ftpd"
    ;;
  full)
    NUM_CONTAINERS=10
    TIMEOUT=720
    TARGETS="live555,pure-ftpd"
    ;;
  *)
    echo "Usage: $0 {minimax|xiaomi} {smoke|short|full}" >&2
    exit 1
    ;;
esac

EXPERIMENT_ID="${EXPERIMENT_PREFIX}-${MODE}-$(date +%Y%m%d-%H%M%S)"

echo "=========================================="
echo "Model Comparison Experiment"
echo "=========================================="
echo "Model: ${LLM_MODEL}"
echo "URL: ${LLM_URL}"
echo "Mode: ${MODE}"
echo "Targets: ${TARGETS}"
echo "Containers: ${NUM_CONTAINERS}"
echo "Timeout: ${TIMEOUT} min"
echo "Experiment ID: ${EXPERIMENT_ID}"
echo "=========================================="

# 运行实验
LLM_URL="${LLM_URL}" \
LLM_TOKEN="${LLM_TOKEN}" \
LLM_MODEL="${LLM_MODEL}" \
EXPERIMENT_ID="${EXPERIMENT_ID}" \
  ./run_tr_ablation.sh "${MODE}"
```

### 3. 验证配置

在正式运行前，先进行 smoke 测试验证配置：

```bash
# 验证 MiniMax 配置
./run_model_comparison.sh minimax smoke

# 验证小米配置
./run_model_comparison.sh xiaomi smoke
```

检查日志确认：
- LLM_URL 正确传递
- API 调用成功
- 模型响应正常

### 4. 正式实验

#### 方案 A：串行运行（推荐，资源占用少）
```bash
# 先运行 MiniMax
./run_model_comparison.sh minimax full

# 等待完成后运行小米
./run_model_comparison.sh xiaomi full
```

#### 方案 B：并行运行（需要更多资源）
```bash
# 同时运行两个模型
./run_model_comparison.sh minimax full &
./run_model_comparison.sh xiaomi full &
wait
```

## 数据收集与分析

### 输出目录结构
```
Result/
├── tr2-minimax-full-20260608-xxxx/
│   ├── out-live555-chatafl-tr2/
│   └── out-pure-ftpd-chatafl-tr2/
└── tr2-xiaomi-full-20260608-xxxx/
    ├── out-live555-chatafl-tr2/
    └── out-pure-ftpd-chatafl-tr2/
```

### 关键指标
1. **代码覆盖率**：edge coverage, state coverage
2. **发现的漏洞**：crash 数量, unique faults
3. **协议状态探索**：state transitions
4. **LLM 调用效率**：successful calls / total calls

### 分析脚本
```bash
# 比较两个模型的覆盖率
python3 benchmark/scripts/analysis/compare_coverage.py \
  Result/tr2-minimax-full-*/out-live555-chatafl-tr2/ \
  Result/tr2-xiaomi-full-*/out-live555-chatafl-tr2/

# 生成对比图表
python3 benchmark/scripts/analysis/plot_model_comparison.py \
  --minimax Result/tr2-minimax-full-*/ \
  --xiaomi Result/tr2-xiaomi-full-*/
```

## 预期结果

### 假设
1. 小米 mimo-v2.5-pro 可能生成更高质量的协议消息
2. 不同模型在不同协议上可能有不同表现
3. 模型响应速度可能影响整体 fuzzing 效率

### 风险与应对
| 风险 | 影响 | 应对措施 |
|------|------|----------|
| API 限流 | 实验中断 | 添加重试机制，降低并发 |
| 模型响应慢 | 效率下降 | 调整超时参数 |
| 模型输出格式不同 | 解析失败 | 检查 clean_llm_response 兼容性 |

## 时间安排

| 阶段 | 任务 | 预计时间 |
|------|------|----------|
| Day 1 | 配置验证 + Smoke 测试 | 2 小时 |
| Day 1-2 | Short 测试（初步评估） | 4 小时 |
| Day 2-3 | Full 测试（MiniMax） | 12 小时 |
| Day 3-4 | Full 测试（小米） | 12 小时 |
| Day 4-5 | 数据分析 + 论文图表 | 8 小时 |

## 附录：快速命令参考

```bash
# Smoke 测试
./run_model_comparison.sh minimax smoke
./run_model_comparison.sh xiaomi smoke

# Short 测试
./run_model_comparison.sh minimax short
./run_model_comparison.sh xiaomi short

# Full 测试
./run_model_comparison.sh minimax full
./run_model_comparison.sh xiaomi full

# 查看运行状态
docker ps --filter "name=chatafl"
tail -f run_logs/*.log
```
