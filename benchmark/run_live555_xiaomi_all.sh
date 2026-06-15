#!/bin/bash

# Live555 Xiaomi 模型批量测试脚本
# 用法: ./run_live555_xiaomi_all.sh [config]
# config: tr1, tr2, tr3, tr4, tr5, all (默认 all)

set -e

# 设置环境
cd /home/leaf/ChatAFL/benchmark

# 模型配置
export LLM_URL="https://token-plan-cn.xiaomimimo.com/v1/chat/completions"
export LLM_TOKEN="tp-c0dk3e4p9i6sevv1jnlfzcqrfp7xerjggdo5bl0342j9vcxv"
export LLM_MODEL="mimo-v2.5-pro"

# 实验参数
export NUM_CONTAINERS="${NUM_CONTAINERS:-1}"
export TIMEOUT="${TIMEOUT:-48000}"  # 800分钟 = 48000秒
export SKIPCOUNT="${SKIPCOUNT:-1}"
export TEST_TIMEOUT="${TEST_TIMEOUT:-20000}"
export PFBENCH=/home/leaf/ChatAFL/benchmark
export PATH=$PFBENCH/scripts/execution:$PATH

# 获取要运行的配置
CONFIG="${1:-all}"

echo "========================================="
echo "Live555 Xiaomi 模型测试"
echo "========================================="
echo ""
echo "模型: $LLM_MODEL"
echo "容器数: $NUM_CONTAINERS"
echo "超时: $TIMEOUT 秒"
echo "配置: $CONFIG"
echo ""

# 运行单个实验的函数
run_experiment() {
  local config=$1
  local result_dir=$2
  local fuzzer=$3
  
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
}

# 根据配置并行运行实验
if [[ "$CONFIG" == "all" ]] || [[ "$CONFIG" == "tr1" ]]; then
  run_experiment "tr1" "results-live555-tr1_live555_xiaomi_3" "chatafl-tr1" &
fi

if [[ "$CONFIG" == "all" ]] || [[ "$CONFIG" == "tr2" ]]; then
  run_experiment "tr2" "results-live555-tr2_live555_xiaomi_3" "chatafl-tr2" &
fi

if [[ "$CONFIG" == "all" ]] || [[ "$CONFIG" == "tr3" ]]; then
  run_experiment "tr3" "results-live555-tr3_live555_xiaomi_3" "chatafl-tr3" &
fi

if [[ "$CONFIG" == "all" ]] || [[ "$CONFIG" == "tr4" ]]; then
  run_experiment "tr4" "results-live555-tr4_live555_xiaomi_3" "chatafl-tr4" &
fi

if [[ "$CONFIG" == "all" ]] || [[ "$CONFIG" == "tr5" ]]; then
  run_experiment "tr5" "results-live555-tr5-xiaomi-rtsp_2" "chatafl-tr5" &
fi

echo "========================================="
echo "所有实验已启动（并行运行）"
echo "等待所有实验完成..."
echo "========================================="

# 等待所有后台任务完成
wait

echo "========================================="
echo "所有实验完成！"
echo "========================================="
