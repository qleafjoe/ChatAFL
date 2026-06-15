#!/bin/bash
set -euo pipefail

# TR2 Multi-Model Comparison Experiment
# Usage: ./run_model_comparison.sh [build|run|all]

MODE="${1:-all}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=========================================="
echo "TR2 Multi-Model Comparison Experiment"
echo "=========================================="
echo "Targets: RTSP (Live555), FTP (PureFTPD)"
echo "Models: MiniMax-M2.7, mimo-v2.5-pro"
echo "Containers: 4 (2 targets x 2 models)"
echo "Timeout: 720 min per container"
echo "=========================================="
echo ""

# Step 1: Test Xiaomi API compatibility
test_xiaomi_api() {
    echo "[Step 1] Testing Xiaomi API compatibility..."
    if bash "${SCRIPT_DIR}/test_xiaomi_api.sh"; then
        echo "Xiaomi API test PASSED"
        return 0
    else
        echo "Xiaomi API test FAILED"
        return 1
    fi
}

# Step 2: Build Docker images
build_images() {
    echo ""
    echo "[Step 2] Building Docker images..."
    
    # Build RTSP (Live555) image
    echo "  Building live555 image..."
    cd "${SCRIPT_DIR}/benchmark/subjects/RTSP/Live555"
    docker build -t profuzzbench/live555 .
    cd "${SCRIPT_DIR}"
    
    # Build FTP (PureFTPD) image
    echo "  Building pure-ftpd image..."
    cd "${SCRIPT_DIR}/benchmark/subjects/FTP/PureFTPD"
    docker build -t profuzzbench/pure-ftpd .
    cd "${SCRIPT_DIR}"
    
    echo "  Docker images built successfully"
}

# Step 3: Run experiments
run_experiments() {
    echo ""
    echo "[Step 3] Running experiments..."
    
    TIMESTAMP=$(date +%Y%m%d-%H%M%S)
    
    # Experiment 1: MiniMax + RTSP
    echo ""
    echo "--- Experiment 1/4: MiniMax + RTSP ---"
    source benchmark/models/minimax.env
    LLM_URL="${LLM_URL}" LLM_TOKEN="${LLM_TOKEN}" LLM_MODEL="${LLM_MODEL}" \
        EXPERIMENT_ID="tr2-minimax-rtsp-${TIMESTAMP}" \
        ./run.sh 1 720 live555 chatafl-tr2 "tr2-minimax-rtsp-${TIMESTAMP}"
    
    # Experiment 2: MiniMax + FTP
    echo ""
    echo "--- Experiment 2/4: MiniMax + FTP ---"
    source benchmark/models/minimax.env
    LLM_URL="${LLM_URL}" LLM_TOKEN="${LLM_TOKEN}" LLM_MODEL="${LLM_MODEL}" \
        EXPERIMENT_ID="tr2-minimax-ftp-${TIMESTAMP}" \
        ./run.sh 1 720 pure-ftpd chatafl-tr2 "tr2-minimax-ftp-${TIMESTAMP}"
    
    # Experiment 3: Xiaomi + RTSP
    echo ""
    echo "--- Experiment 3/4: Xiaomi + RTSP ---"
    source benchmark/models/xiaomi.env
    LLM_URL="${LLM_URL}" LLM_TOKEN="${LLM_TOKEN}" LLM_MODEL="${LLM_MODEL}" \
        EXPERIMENT_ID="tr2-xiaomi-rtsp-${TIMESTAMP}" \
        ./run.sh 1 720 live555 chatafl-tr2 "tr2-xiaomi-rtsp-${TIMESTAMP}"
    
    # Experiment 4: Xiaomi + FTP
    echo ""
    echo "--- Experiment 4/4: Xiaomi + FTP ---"
    source benchmark/models/xiaomi.env
    LLM_URL="${LLM_URL}" LLM_TOKEN="${LLM_TOKEN}" LLM_MODEL="${LLM_MODEL}" \
        EXPERIMENT_ID="tr2-xiaomi-ftp-${TIMESTAMP}" \
        ./run.sh 1 720 pure-ftpd chatafl-tr2 "tr2-xiaomi-ftp-${TIMESTAMP}"
    
    echo ""
    echo "=========================================="
    echo "All experiments started!"
    echo "Results will be in: Result/tr2-*-rtsp-${TIMESTAMP}/ and Result/tr2-*-ftp-${TIMESTAMP}/"
    echo "=========================================="
}

# Main execution
case "$MODE" in
    test)
        test_xiaomi_api
        ;;
    build)
        build_images
        ;;
    run)
        run_experiments
        ;;
    all)
        test_xiaomi_api
        build_images
        run_experiments
        ;;
    *)
        echo "Usage: $0 {test|build|run|all}"
        echo ""
        echo "  test  - Test Xiaomi API compatibility only"
        echo "  build - Build Docker images only"
        echo "  run   - Run experiments only"
        echo "  all   - Test + Build + Run (default)"
        exit 1
        ;;
esac
