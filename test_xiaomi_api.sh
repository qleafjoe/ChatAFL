#!/bin/bash
set -euo pipefail

# Test Xiaomi mimo-v2.5-pro API compatibility
echo "=========================================="
echo "Test Xiaomi mimo-v2.5-pro API"
echo "=========================================="

source benchmark/models/xiaomi.env

echo "API URL: ${LLM_URL}"
echo "Model: ${LLM_MODEL}"
echo ""

echo "[Test 1] Basic API connection..."
HTTP_CODE=$(curl -s -o /tmp/xiaomi_test.json -w "%{http_code}" \
    -X POST "${LLM_URL}" \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer ${LLM_TOKEN}" \
    -d '{
        "model": "'"${LLM_MODEL}"'",
        "messages": [{"role": "user", "content": "Hello, respond with OK"}],
        "max_tokens": 10
    }' 2>&1)

if [[ "$HTTP_CODE" == "200" ]]; then
    echo "  PASS: HTTP ${HTTP_CODE}"
else
    echo "  FAIL: HTTP ${HTTP_CODE}"
    cat /tmp/xiaomi_test.json
    exit 1
fi

echo ""
echo "[Test 2] Check response format..."
if jq -e '.choices[0].message.content' /tmp/xiaomi_test.json > /dev/null 2>&1; then
    CONTENT=$(jq -r '.choices[0].message.content' /tmp/xiaomi_test.json)
    echo "  PASS: OpenAI compatible format"
    echo "  Content: ${CONTENT}"
else
    echo "  FAIL: Not OpenAI compatible"
    cat /tmp/xiaomi_test.json
    exit 1
fi

echo ""
echo "[Test 3] RTSP message generation..."
HTTP_CODE=$(curl -s -o /tmp/xiaomi_rtsp.json -w "%{http_code}" \
    -X POST "${LLM_URL}" \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer ${LLM_TOKEN}" \
    -d '{
        "model": "'"${LLM_MODEL}"'",
        "messages": [
            {"role": "system", "content": "Generate raw protocol messages only."},
            {"role": "user", "content": "Generate a single RTSP OPTIONS request with CSeq 1. Output ONLY the raw message."}
        ],
        "max_tokens": 200,
        "temperature": 0.3
    }' 2>&1)

if [[ "$HTTP_CODE" == "200" ]]; then
    echo "  PASS: HTTP ${HTTP_CODE}"
    CONTENT=$(jq -r '.choices[0].message.content' /tmp/xiaomi_rtsp.json)
    echo "  Generated RTSP:"
    echo "  ---"
    echo "  ${CONTENT}"
    echo "  ---"
else
    echo "  FAIL: HTTP ${HTTP_CODE}"
fi

echo ""
echo "=========================================="
echo "API compatibility test complete"
echo "=========================================="
