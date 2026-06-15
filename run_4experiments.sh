#!/bin/bash
set -euo pipefail

# Run 4 experiments in background
# 2 targets (RTSP, FTP) x 2 models (MiniMax, Xiaomi)

TIMESTAMP=$(date +%Y%m%d-%H%M%S)
LOG_DIR="run_logs"
mkdir -p "$LOG_DIR"

echo "=========================================="
echo "Starting 4 Model Comparison Experiments"
echo "=========================================="
echo "Timestamp: ${TIMESTAMP}"
echo "Targets: RTSP (Live555), FTP (PureFTPD)"
echo "Models: MiniMax-M2.7, mimo-v2.5-pro"
echo "Timeout: 720 min per experiment"
echo "=========================================="
echo ""

# Experiment 1: MiniMax + RTSP
echo "[1/4] Starting MiniMax + RTSP..."
source benchmark/models/minimax.env
nohup bash -c "
    source benchmark/models/minimax.env
    LLM_URL=\"\$LLM_URL\" LLM_TOKEN=\"\$LLM_TOKEN\" LLM_MODEL=\"\$LLM_MODEL\" \
        EXPERIMENT_ID=\"tr2-minimax-rtsp-${TIMESTAMP}\" \
        ./run.sh 1 720 live555 chatafl-tr2 \"tr2-minimax-rtsp-${TIMESTAMP}\"
" > "${LOG_DIR}/tr2-minimax-rtsp-${TIMESTAMP}.log" 2>&1 &
PID1=$!
echo "  PID: ${PID1}"

# Experiment 2: MiniMax + FTP
echo "[2/4] Starting MiniMax + FTP..."
nohup bash -c "
    source benchmark/models/minimax.env
    LLM_URL=\"\$LLM_URL\" LLM_TOKEN=\"\$LLM_TOKEN\" LLM_MODEL=\"\$LLM_MODEL\" \
        EXPERIMENT_ID=\"tr2-minimax-ftp-${TIMESTAMP}\" \
        ./run.sh 1 720 pure-ftpd chatafl-tr2 \"tr2-minimax-ftp-${TIMESTAMP}\"
" > "${LOG_DIR}/tr2-minimax-ftp-${TIMESTAMP}.log" 2>&1 &
PID2=$!
echo "  PID: ${PID2}"

# Experiment 3: Xiaomi + RTSP
echo "[3/4] Starting Xiaomi + RTSP..."
nohup bash -c "
    source benchmark/models/xiaomi.env
    LLM_URL=\"\$LLM_URL\" LLM_TOKEN=\"\$LLM_TOKEN\" LLM_MODEL=\"\$LLM_MODEL\" \
        EXPERIMENT_ID=\"tr2-xiaomi-rtsp-${TIMESTAMP}\" \
        ./run.sh 1 720 live555 chatafl-tr2 \"tr2-xiaomi-rtsp-${TIMESTAMP}\"
" > "${LOG_DIR}/tr2-xiaomi-rtsp-${TIMESTAMP}.log" 2>&1 &
PID3=$!
echo "  PID: ${PID3}"

# Experiment 4: Xiaomi + FTP
echo "[4/4] Starting Xiaomi + FTP..."
nohup bash -c "
    source benchmark/models/xiaomi.env
    LLM_URL=\"\$LLM_URL\" LLM_TOKEN=\"\$LLM_TOKEN\" LLM_MODEL=\"\$LLM_MODEL\" \
        EXPERIMENT_ID=\"tr2-xiaomi-ftp-${TIMESTAMP}\" \
        ./run.sh 1 720 pure-ftpd chatafl-tr2 \"tr2-xiaomi-ftp-${TIMESTAMP}\"
" > "${LOG_DIR}/tr2-xiaomi-ftp-${TIMESTAMP}.log" 2>&1 &
PID4=$!
echo "  PID: ${PID4}"

echo ""
echo "=========================================="
echo "All 4 experiments started!"
echo "=========================================="
echo ""
echo "Process IDs:"
echo "  MiniMax + RTSP: ${PID1}"
echo "  MiniMax + FTP:  ${PID2}"
echo "  Xiaomi + RTSP:  ${PID3}"
echo "  Xiaomi + FTP:   ${PID4}"
echo ""
echo "Log files:"
echo "  ${LOG_DIR}/tr2-minimax-rtsp-${TIMESTAMP}.log"
echo "  ${LOG_DIR}/tr2-minimax-ftp-${TIMESTAMP}.log"
echo "  ${LOG_DIR}/tr2-xiaomi-rtsp-${TIMESTAMP}.log"
echo "  ${LOG_DIR}/tr2-xiaomi-ftp-${TIMESTAMP}.log"
echo ""
echo "Monitor with:"
echo "  docker ps"
echo "  tail -f ${LOG_DIR}/*.log"
echo ""
echo "Results will be in:"
echo "  Result/tr2-minimax-rtsp-${TIMESTAMP}/"
echo "  Result/tr2-minimax-ftp-${TIMESTAMP}/"
echo "  Result/tr2-xiaomi-rtsp-${TIMESTAMP}/"
echo "  Result/tr2-xiaomi-ftp-${TIMESTAMP}/"
