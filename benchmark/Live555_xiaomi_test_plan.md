# Live555 Xiaomi 模型测试计划

## 1. 测试目标

将 Live555 的 tr1、tr2、tr3、tr4、tr5 配置使用 xiaomi 模型再跑一遍，验证 xiaomi 模型在不同消融配置下的表现。

## 2. 命名规范

根据之前的命名规范：
- `_1` 后缀：第一次实验（目录名无后缀）
- `_2` 后缀：第二次实验（目录名有后缀）
- `_3` 后缀：第三次实验（本次新增）

### 新实验命名

| 配置 | 目录名称 | 输出目录名称 |
|------|----------|--------------|
| tr1 | `results-live555-tr1_live555_xiaomi_3` | `out-live555-chatafl_tr1` |
| tr2 | `results-live555-tr2_live555_xiaomi_3` | `out-live555-chatafl_tr2` |
| tr3 | `results-live555-tr3_live555_xiaomi_3` | `out-live555-chatafl_tr3` |
| tr4 | `results-live555-tr4_live555_xiaomi_3` | `out-live555-chatafl_tr4` |
| tr5 | `results-live555-tr5-xiaomi-rtsp_2` | `out-live555-chatafl_tr5` |

## 3. 实验配置

### 3.1 模型配置

使用 xiaomi 模型：
```bash
export LLM_URL="https://token-plan-cn.xiaomimimo.com/v1/chat/completions"
export LLM_TOKEN="tp-c0dk3e4p9i6sevv1jnlfzcqrfp7xerjggdo5bl0342j9vcxv"
export LLM_MODEL="mimo-v2.5-pro"
```

### 3.2 通用参数

```bash
NUM_CONTAINERS=10    # 每个实验运行10个容器
TIMEOUT=48000        # 800分钟超时
SKIPCOUNT=1          # 每个测试用例后计算覆盖率
TEST_TIMEOUT=20000   # 测试超时 20000ms
```

### 3.3 Live555 专用参数

```bash
"-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none"
```

## 4. 执行命令

### 4.1 设置环境变量

```bash
cd /home/leaf/ChatAFL/benchmark

# 设置 xiaomi 模型配置
export LLM_URL="https://token-plan-cn.xiaomimimo.com/v1/chat/completions"
export LLM_TOKEN="tp-c0dk3e4p9i6sevv1jnlfzcqrfp7xerjggdo5bl0342j9vcxv"
export LLM_MODEL="mimo-v2.5-pro"

# 设置实验参数
export NUM_CONTAINERS=10
export TIMEOUT=86400
export SKIPCOUNT=1
export TEST_TIMEOUT=20000

# 设置工作目录
export PFBENCH=/home/leaf/ChatAFL/benchmark
```

### 4.2 运行 tr1 + xiaomi

```bash
# 创建结果目录
mkdir -p results-live555-tr1_live555_xiaomi_3

# 运行实验
cd $PFBENCH
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
  results-live555-tr1_live555_xiaomi_3 \
  chatafl-tr1 \
  out-live555-chatafl_tr1 \
  "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
  $TIMEOUT $SKIPCOUNT
```

### 4.3 运行 tr2 + xiaomi

```bash
mkdir -p results-live555-tr2_live555_xiaomi_3

cd $PFBENCH
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
  results-live555-tr2_live555_xiaomi_3 \
  chatafl-tr2 \
  out-live555-chatafl_tr2 \
  "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
  $TIMEOUT $SKIPCOUNT
```

### 4.4 运行 tr3 + xiaomi

```bash
mkdir -p results-live555-tr3_live555_xiaomi_3

cd $PFBENCH
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
  results-live555-tr3_live555_xiaomi_3 \
  chatafl-tr3 \
  out-live555-chatafl_tr3 \
  "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
  $TIMEOUT $SKIPCOUNT
```

### 4.5 运行 tr4 + xiaomi

```bash
mkdir -p results-live555-tr4_live555_xiaomi_3

cd $PFBENCH
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
  results-live555-tr4_live555_xiaomi_3 \
  chatafl-tr4 \
  out-live555-chatafl_tr4 \
  "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
  $TIMEOUT $SKIPCOUNT
```

### 4.6 运行 tr5 + xiaomi（第二轮）

```bash
mkdir -p results-live555-tr5-xiaomi-rtsp_2

cd $PFBENCH
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
  results-live555-tr5-xiaomi-rtsp_2 \
  chatafl-tr5 \
  out-live555-chatafl_tr5 \
  "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
  $TIMEOUT $SKIPCOUNT
```

## 5. 批量执行脚本

创建批量执行脚本 `run_live555_xiaomi_all.sh`：

```bash
#!/bin/bash

# Live555 Xiaomi 模型批量测试脚本

set -e

# 设置环境
cd /home/leaf/ChatAFL/benchmark

# 模型配置
export LLM_URL="https://token-plan-cn.xiaomimimo.com/v1/chat/completions"
export LLM_TOKEN="tp-c0dk3e4p9i6sevv1jnlfzcqrfp7xerjggdo5bl0342j9vcxv"
export LLM_MODEL="mimo-v2.5-pro"

# 实验参数
export NUM_CONTAINERS=10
export TIMEOUT=86400
export SKIPCOUNT=1
export TEST_TIMEOUT=20000
export PFBENCH=/home/leaf/ChatAFL/benchmark

# 实验列表
declare -A EXPERIMENTS
EXPERIMENTS=(
  ["tr1"]="results-live555-tr1_live555_xiaomi_3"
  ["tr2"]="results-live555-tr2_live555_xiaomi_3"
  ["tr3"]="results-live555-tr3_live555_xiaomi_3"
  ["tr4"]="results-live555-tr4_live555_xiaomi_3"
  ["tr5"]="results-live555-tr5-xiaomi-rtsp_2"
)

# Fuzzer 映射
declare -A FUZZERS
FUZZERS=(
  ["tr1"]="chatafl-tr1"
  ["tr2"]="chatafl-tr2"
  ["tr3"]="chatafl-tr3"
  ["tr4"]="chatafl-tr4"
  ["tr5"]="chatafl-tr5"
)

echo "========================================="
echo "Live555 Xiaomi 模型批量测试"
echo "========================================="
echo ""
echo "模型: $LLM_MODEL"
echo "容器数: $NUM_CONTAINERS"
echo "超时: $TIMEOUT 秒"
echo ""

# 运行所有实验
for config in tr1 tr2 tr3 tr4 tr5; do
  result_dir="${EXPERIMENTS[$config]}"
  fuzzer="${FUZZERS[$config]}"
  
  echo "----------------------------------------"
  echo "运行 $config + xiaomi"
  echo "结果目录: $result_dir"
  echo "Fuzzer: $fuzzer"
  echo "----------------------------------------"
  
  # 创建结果目录
  mkdir -p "$result_dir"
  
  # 运行实验
  cd $PFBENCH
  profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    "$result_dir" \
    "$fuzzer" \
    "out-live555-chatafl_${config}" \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT
  
  echo "$config 完成"
  echo ""
done

echo "========================================="
echo "所有实验完成！"
echo "========================================="
```

## 6. 预期结果目录结构

```
benchmark/
├── results-live555-tr1_live555_xiaomi_3/
│   ├── out-live555-chatafl_tr1_1.tar.gz
│   ├── out-live555-chatafl_tr1_2.tar.gz
│   ├── ...
│   └── out-live555-chatafl_tr1_10.tar.gz
├── results-live555-tr2_live555_xiaomi_3/
│   └── ...
├── results-live555-tr3_live555_xiaomi_3/
│   └── ...
├── results-live555-tr4_live555_xiaomi_3/
│   └── ...
└── results-live555-tr5-xiaomi-rtsp_2/
    └── ...
```

## 7. 数据收集

实验完成后，需要：

1. **解压结果文件**
   ```bash
   for dir in results-live555-tr*_xiaomi_3 results-live555-tr5-xiaomi-rtsp_2; do
     cd "$dir"
     for tar in *.tar.gz; do
       tar -xzf "$tar"
     done
     cd ..
   done
   ```

2. **提取覆盖率数据**
   - 从每个实验的 `cov_over_time.csv` 提取最终的 `b_abs` 和 `l_abs`
   - 从 `ipsm.dot` 文件解析 `nodes` 和 `edges`

3. **更新报告**
   - 将新数据添加到 `experiment_summary_clean.csv`
   - 更新 `experiment_summary_report.md`

## 8. 预计时间

每个实验约需 800 分钟（48000 秒 ≈ 13.3 小时），5 个实验串行执行约需 2.8 天。

如果并行执行（需要足够的计算资源），可缩短至 13.3 小时。

## 9. 注意事项

1. **资源需求**：每个实验需要 10 个 Docker 容器，确保有足够的 CPU 和内存资源
2. **磁盘空间**：每个实验结果约 500MB-1GB，确保有足够的磁盘空间
3. **网络连接**：xiaomi 模型需要稳定的网络连接
4. **监控**：建议使用 `docker stats` 监控容器资源使用情况
