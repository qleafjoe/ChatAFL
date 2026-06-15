#!/bin/bash

# 第4轮实验 - 多协议消融对比
# Phase 1: Live555 (RTSP) - aflnet + tr1-tr5 xiaomi (6个)
# Phase 2: Pure-FTPd (FTP) - aflnet + tr1-tr5 xiaomi (6个)
# Phase 3: Lighttpd1 (HTTP) - aflnet + tr1-tr5 xiaomi (6个)
# 每个实验1轮，800分钟，每阶段内并行执行

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
echo "第4轮实验 - 多协议消融对比"
echo "========================================="
echo "超时: $TIMEOUT 秒 (800分钟)"
echo "容器数: $NUM_CONTAINERS (每个实验1个容器)"
echo "阶段1: Live555 (6个实验)"
echo "阶段2: Pure-FTPd (6个实验)"
echo "阶段3: Lighttpd1 (6个实验)"
echo "总计: 18个实验 (6×3)"
echo "========================================="

# ==========================================
# Phase 1: Live555 (RTSP)
# ==========================================
echo ""
echo "========================================="
echo "Phase 1: Live555 (RTSP)"
echo "========================================="
echo ""

echo "[1/18] 启动 Live555 AFLNet 基线..."
mkdir -p results-live555-aflnet_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-aflnet_4 \
    aflnet \
    out-live555-aflnet \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "[2/18] 启动 Live555 tr1 + xiaomi..."
mkdir -p results-live555-tr1_xiaomi_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-tr1_xiaomi_4 \
    chatafl-tr1 \
    out-live555-chatafl_tr1 \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "[3/18] 启动 Live555 tr2 + xiaomi..."
mkdir -p results-live555-tr2_xiaomi_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-tr2_xiaomi_4 \
    chatafl-tr2 \
    out-live555-chatafl_tr2 \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "[4/18] 启动 Live555 tr3 + xiaomi..."
mkdir -p results-live555-tr3_xiaomi_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-tr3_xiaomi_4 \
    chatafl-tr3 \
    out-live555-chatafl_tr3 \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "[5/18] 启动 Live555 tr4 + xiaomi..."
mkdir -p results-live555-tr4_xiaomi_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-tr4_xiaomi_4 \
    chatafl-tr4 \
    out-live555-chatafl_tr4 \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "[6/18] 启动 Live555 tr5 + xiaomi..."
mkdir -p results-live555-tr5_xiaomi_4
profuzzbench_exec_common.sh live555 $NUM_CONTAINERS \
    results-live555-tr5_xiaomi_4 \
    chatafl-tr5 \
    out-live555-chatafl_tr5 \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" \
    $TIMEOUT $SKIPCOUNT &

echo "========================================="
echo "Phase 1: Live555 6个实验已启动（并行运行）"
echo "等待所有实验完成..."
echo "========================================="

wait

echo "========================================="
echo "Phase 1: Live555 所有实验完成！"
echo "========================================="

# ==========================================
# Phase 2: Pure-FTPd (FTP)
# ==========================================
echo ""
echo "========================================="
echo "Phase 2: Pure-FTPd (FTP)"
echo "========================================="
echo ""

echo "[7/18] 启动 Pure-FTPd AFLNet 基线..."
mkdir -p results-pure-ftpd-aflnet_4
profuzzbench_exec_common.sh pure-ftpd $NUM_CONTAINERS \
    results-pure-ftpd-aflnet_4 \
    aflnet \
    out-pure-ftpd-aflnet \
    "-m none -P FTP -D 10000 -q 3 -s 3 -E -K -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "[8/18] 启动 Pure-FTPd tr1 + xiaomi..."
mkdir -p results-pure-ftpd-tr1_xiaomi_4
profuzzbench_exec_common.sh pure-ftpd $NUM_CONTAINERS \
    results-pure-ftpd-tr1_xiaomi_4 \
    chatafl-tr1 \
    out-pure-ftpd-chatafl_tr1 \
    "-m none -P FTP -D 10000 -q 3 -s 3 -E -K -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "[9/18] 启动 Pure-FTPd tr2 + xiaomi..."
mkdir -p results-pure-ftpd-tr2_xiaomi_4
profuzzbench_exec_common.sh pure-ftpd $NUM_CONTAINERS \
    results-pure-ftpd-tr2_xiaomi_4 \
    chatafl-tr2 \
    out-pure-ftpd-chatafl_tr2 \
    "-m none -P FTP -D 10000 -q 3 -s 3 -E -K -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "[10/18] 启动 Pure-FTPd tr3 + xiaomi..."
mkdir -p results-pure-ftpd-tr3_xiaomi_4
profuzzbench_exec_common.sh pure-ftpd $NUM_CONTAINERS \
    results-pure-ftpd-tr3_xiaomi_4 \
    chatafl-tr3 \
    out-pure-ftpd-chatafl_tr3 \
    "-m none -P FTP -D 10000 -q 3 -s 3 -E -K -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "[11/18] 启动 Pure-FTPd tr4 + xiaomi..."
mkdir -p results-pure-ftpd-tr4_xiaomi_4
profuzzbench_exec_common.sh pure-ftpd $NUM_CONTAINERS \
    results-pure-ftpd-tr4_xiaomi_4 \
    chatafl-tr4 \
    out-pure-ftpd-chatafl_tr4 \
    "-m none -P FTP -D 10000 -q 3 -s 3 -E -K -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "[12/18] 启动 Pure-FTPd tr5 + xiaomi..."
mkdir -p results-pure-ftpd-tr5_xiaomi_4
profuzzbench_exec_common.sh pure-ftpd $NUM_CONTAINERS \
    results-pure-ftpd-tr5_xiaomi_4 \
    chatafl-tr5 \
    out-pure-ftpd-chatafl_tr5 \
    "-m none -P FTP -D 10000 -q 3 -s 3 -E -K -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "========================================="
echo "Phase 2: Pure-FTPd 6个实验已启动（并行运行）"
echo "等待所有实验完成..."
echo "========================================="

wait

echo "========================================="
echo "Phase 2: Pure-FTPd 所有实验完成！"
echo "========================================="

# ==========================================
# Phase 3: Lighttpd1 (HTTP)
# ==========================================
echo ""
echo "========================================="
echo "Phase 3: Lighttpd1 (HTTP)"
echo "========================================="
echo ""

echo "[13/18] 启动 Lighttpd1 AFLNet 基线..."
mkdir -p results-lighttpd1-aflnet_4
profuzzbench_exec_common.sh lighttpd1 $NUM_CONTAINERS \
    results-lighttpd1-aflnet_4 \
    aflnet \
    out-lighttpd1-aflnet \
    "-P HTTP -D 200000 -m none -q 3 -s 3 -E -K -R -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "[14/18] 启动 Lighttpd1 tr1 + xiaomi..."
mkdir -p results-lighttpd1-tr1_xiaomi_4
profuzzbench_exec_common.sh lighttpd1 $NUM_CONTAINERS \
    results-lighttpd1-tr1_xiaomi_4 \
    chatafl-tr1 \
    out-lighttpd1-chatafl_tr1 \
    "-P HTTP -D 200000 -m none -q 3 -s 3 -E -K -R -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "[15/18] 启动 Lighttpd1 tr2 + xiaomi..."
mkdir -p results-lighttpd1-tr2_xiaomi_4
profuzzbench_exec_common.sh lighttpd1 $NUM_CONTAINERS \
    results-lighttpd1-tr2_xiaomi_4 \
    chatafl-tr2 \
    out-lighttpd1-chatafl_tr2 \
    "-P HTTP -D 200000 -m none -q 3 -s 3 -E -K -R -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "[16/18] 启动 Lighttpd1 tr3 + xiaomi..."
mkdir -p results-lighttpd1-tr3_xiaomi_4
profuzzbench_exec_common.sh lighttpd1 $NUM_CONTAINERS \
    results-lighttpd1-tr3_xiaomi_4 \
    chatafl-tr3 \
    out-lighttpd1-chatafl_tr3 \
    "-P HTTP -D 200000 -m none -q 3 -s 3 -E -K -R -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "[17/18] 启动 Lighttpd1 tr4 + xiaomi..."
mkdir -p results-lighttpd1-tr4_xiaomi_4
profuzzbench_exec_common.sh lighttpd1 $NUM_CONTAINERS \
    results-lighttpd1-tr4_xiaomi_4 \
    chatafl-tr4 \
    out-lighttpd1-chatafl_tr4 \
    "-P HTTP -D 200000 -m none -q 3 -s 3 -E -K -R -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "[18/18] 启动 Lighttpd1 tr5 + xiaomi..."
mkdir -p results-lighttpd1-tr5_xiaomi_4
profuzzbench_exec_common.sh lighttpd1 $NUM_CONTAINERS \
    results-lighttpd1-tr5_xiaomi_4 \
    chatafl-tr5 \
    out-lighttpd1-chatafl_tr5 \
    "-P HTTP -D 200000 -m none -q 3 -s 3 -E -K -R -t ${TEST_TIMEOUT}+" \
    $TIMEOUT $SKIPCOUNT &

echo "========================================="
echo "Phase 3: Lighttpd1 6个实验已启动（并行运行）"
echo "等待所有实验完成..."
echo "========================================="

wait

echo "========================================="
echo "Phase 3: Lighttpd1 所有实验完成！"
echo "========================================="

# ==========================================
# 实验完成
# ==========================================
echo ""
echo "========================================="
echo "所有第4轮实验完成！"
echo "========================================="
echo ""
echo "结果目录:"
echo "  Live555:"
echo "    - results-live555-aflnet_4"
echo "    - results-live555-tr1_xiaomi_4"
echo "    - results-live555-tr2_xiaomi_4"
echo "    - results-live555-tr3_xiaomi_4"
echo "    - results-live555-tr4_xiaomi_4"
echo "    - results-live555-tr5_xiaomi_4"
echo "  Pure-FTPd:"
echo "    - results-pure-ftpd-aflnet_4"
echo "    - results-pure-ftpd-tr1_xiaomi_4"
echo "    - results-pure-ftpd-tr2_xiaomi_4"
echo "    - results-pure-ftpd-tr3_xiaomi_4"
echo "    - results-pure-ftpd-tr4_xiaomi_4"
echo "    - results-pure-ftpd-tr5_xiaomi_4"
echo "  Lighttpd1:"
echo "    - results-lighttpd1-aflnet_4"
echo "    - results-lighttpd1-tr1_xiaomi_4"
echo "    - results-lighttpd1-tr2_xiaomi_4"
echo "    - results-lighttpd1-tr3_xiaomi_4"
echo "    - results-lighttpd1-tr4_xiaomi_4"
echo "    - results-lighttpd1-tr5_xiaomi_4"
