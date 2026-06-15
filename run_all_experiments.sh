#!/bin/bash
set -e

echo "============================================"
echo " ChatAFL Ablation Experiments"
echo " Target: Live555 (RTSP)"
echo " Duration: 1600 min per experiment"
echo "============================================"

export LLM_URL="https://api.minimaxi.com/v1/text/chatcompletion_v2"
export LLM_TOKEN="sk-cp-EK3rwNPjpttunXcODVKSpsvJh4dySqRdtbgbjcmLxdlSHRyoIuWJzFPXFUr8I8rponL4y-xwRMMcO3eodW7dwfO2hqL3G6cCQBtIufVHuRX11_JV1YK5YFs"
export LLM_MODEL="MiniMax-M2.7"

EXPERIMENT_ID="ablation_$(date +%Y%m%d_%H%M%S)"
DURATION=1600

echo ""
echo "Experiment ID: $EXPERIMENT_ID"
echo "Duration: $DURATION minutes"
echo ""

# Run aflnet (baseline - no LLM)
echo "[1/5] Running aflnet (baseline)..."
./run.sh 1 $DURATION live555 aflnet "${EXPERIMENT_ID}" &
AFLNET_PID=$!

# Wait a bit before starting LLM-based fuzzers
sleep 10

# Run chatafl (main)
# echo "[2/5] Running chatafl (main)..."
# ./run.sh 1 $DURATION live555 chatafl "${EXPERIMENT_ID}" &
# CHATAFL_PID=$!

# sleep 10

# Run chatafl-v0 (no validation)
echo "[3/5] Running chatafl-v0 (no validation)..."
./run.sh 1 $DURATION live555 chatafl-v0 "${EXPERIMENT_ID}" &
V0_PID=$!

sleep 10

# Run chatafl-v1 (format validation)
echo "[4/5] Running chatafl-v1 (format validation)..."
./run.sh 1 $DURATION live555 chatafl-v1 "${EXPERIMENT_ID}" &
V1_PID=$!

sleep 10

# Run chatafl-v2 (full validation)
echo "[5/5] Running chatafl-v2 (full validation)..."
./run.sh 1 $DURATION live555 chatafl-v2 "${EXPERIMENT_ID}" &
V2_PID=$!

echo ""
echo "All experiments started!"
echo "PIDs: aflnet=$AFLNET_PID, chatafl=$CHATAFL_PID, v0=$V0_PID, v1=$V1_PID, v2=$V2_PID"
echo ""
echo "Monitor progress with:"
echo "  docker ps"
echo "  tail -f benchmark/results-live555-${EXPERIMENT_ID}/*/output.log"
echo ""
echo "Estimated completion: $(date -d "+1600 minutes" '+%Y-%m-%d %H:%M')"

# Wait for all experiments
wait $AFLNET_PID $CHATAFL_PID $V0_PID $V1_PID $V2_PID

echo ""
echo "============================================"
echo " All experiments completed!"
echo "============================================"
