#!/bin/bash

# Live555 第4轮实验 - aflnet + tr1-tr5 xiaomi
# 每个实验1个容器，800分钟，6个实验并行执行

set -e

cd /home/leaf/ChatAFL/benchmark

# 实验参数
export NUM_CONTAINERS=1
export TIMEOUT=48000        # 800分钟
export SKIPCOUNT=1
export TEST_TIMEOUT=20000
export PFBENCH=/home/leaf/ChatAFL/benchmark
export PATH=$PFBENCH/scripts/execution:$PATH

# xiaomi 模型配置
export LLM_URL="https://token-plan-cn.xiaomimimo.com/v1/chat/completions"
export LLM_TOKEN="tp-c0dk3e4p9i6sevv1jnlfzcqrfp7xerjggdo5bl0342j9vcxv"
export LLM_MODEL="mimo-v2.5-pro"

echo "========================================="
echo "Live555 第4轮实验 (RTSP)"
echo "========================================="
echo "超时: $TIMEOUT 秒 (800分钟)"
echo "容器数: $NUM_CONTAINERS"
echo "实验数: 6 (aflnet + tr1-tr5 xiaomi)"
echo "========================================="
echo ""

echo "[1/6] 启动 Live555 AFLNet 基线..."
mkdir -p results-live555-aflnet_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-aflnet_4 \
    aflnet \
    out-live555-aflnet \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "[2/6] 启动 Live555 tr1 + xiaomi..."
mkdir -p results-live555-tr1_xiaomi_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-tr1_xiaomi_4 \
    chatafl-tr1 \
    out-live555-chatafl_tr1 \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "[3/6] 启动 Live555 tr2 + xiaomi..."
mkdir -p results-live555-tr2_xiaomi_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-tr2_xiaomi_4 \
    chatafl-tr2 \
    out-live555-chatafl_tr2 \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "[4/6] 启动 Live555 tr3 + xiaomi..."
mkdir -p results-live555-tr3_xiaomi_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-tr3_xiaomi_4 \
    chatafl-tr3 \
    out-live555-chatafl_tr3 \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "[5/6] 启动 Live555 tr4 + xiaomi..."
mkdir -p results-live555-tr4_xiaomi_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-tr4_xiaomi_4 \
    chatafl-tr4 \
    out-live555-chatafl_tr4 \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "[6/6] 启动 Live555 tr5 + xiaomi..."
mkdir -p results-live555-tr5_xiaomi_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-tr5_xiaomi_4 \
    chatafl-tr5 \
    out-live555-chatafl_tr5 \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "========================================="
echo "所有6个实验已启动（并行运行）"
echo "等待所有实验完成..."
echo "========================================="

wait

echo "========================================="
echo "Live555 第4轮实验全部完成！"
echo "========================================="
echo ""
echo "结果目录:"
echo "  - results-live555-aflnet_4"
echo "  - results-live555-tr1_xiaomi_4"
echo "  - results-live555-tr2_xiaomi_4"
echo "  - results-live555-tr3_xiaomi_4"
echo "  - results-live555-tr4_xiaomi_4"
echo "  - results-live555-tr5_xiaomi_4"
