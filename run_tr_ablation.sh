#!/bin/bash
set -euo pipefail

MODE="${1:-smoke}"
FUZZERS="chatafl-tr1,chatafl-tr2,chatafl-tr3,chatafl-tr4,chatafl-tr5"

if [[ -z "${LLM_URL:-}" || -z "${LLM_TOKEN:-}" || -z "${LLM_MODEL:-}" ]]; then
  if [[ -f "./run_all_experiments.sh" ]]; then
    eval "$(grep -E '^export LLM_(URL|TOKEN|MODEL)=' ./run_all_experiments.sh)"
  fi
fi

case "$MODE" in
  smoke)
    NUM_CONTAINERS="${NUM_CONTAINERS:-1}"
    TIMEOUT="${TIMEOUT:-10}"
    TARGETS="${TARGETS:-live555}"
    ;;
  short)
    NUM_CONTAINERS="${NUM_CONTAINERS:-3}"
    TIMEOUT="${TIMEOUT:-120}"
    TARGETS="${TARGETS:-live555,pure-ftpd}"
    ;;
  full)
    NUM_CONTAINERS="${NUM_CONTAINERS:-10}"
    TIMEOUT="${TIMEOUT:-720}"
    TARGETS="${TARGETS:-live555,pure-ftpd}"
    ;;
  *)
    echo "Usage: $0 {smoke|short|full}" >&2
    exit 1
    ;;
esac

EXPERIMENT_ID="${EXPERIMENT_ID:-tr-${MODE}-$(date +%Y%m%d-%H%M%S)}"

echo "# TR ablation mode: ${MODE}"
echo "# NUM_CONTAINERS: ${NUM_CONTAINERS}"
echo "# TIMEOUT: ${TIMEOUT} min"
echo "# TARGETS: ${TARGETS}"
echo "# FUZZERS: ${FUZZERS}"
echo "# EXPERIMENT_ID: ${EXPERIMENT_ID}"
echo "# LLM_URL: ${LLM_URL:-<unset>}"
echo "# LLM_MODEL: ${LLM_MODEL:-<unset>}"

LLM_URL="${LLM_URL:-}" \
LLM_TOKEN="${LLM_TOKEN:-}" \
LLM_MODEL="${LLM_MODEL:-}" \
TEST_TIMEOUT="${TEST_TIMEOUT:-5000}" \
SKIPCOUNT="${SKIPCOUNT:-1}" \
EXPERIMENT_ID="${EXPERIMENT_ID}" \
  ./run.sh "${NUM_CONTAINERS}" "${TIMEOUT}" "${TARGETS}" "${FUZZERS}" "${EXPERIMENT_ID}"
