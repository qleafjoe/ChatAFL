#!/bin/bash
# ChatAFL: full pre-validation + post-execution gain attribution + feedback retry
: "${AFL_LLM_VALIDATION:=1}"
: "${AFL_LLM_VALIDATION_STRICT:=1}"
: "${AFL_LLM_POST_GAIN:=1}"
: "${AFL_LLM_FEEDBACK:=1}"
: "${AFL_LLM_FEEDBACK_MAX_RETRIES:=3}"
: "${AFL_LLM_SKIP_STARTUP:=0}"
export AFL_LLM_VALIDATION
export AFL_LLM_VALIDATION_STRICT
export AFL_LLM_POST_GAIN
export AFL_LLM_FEEDBACK
export AFL_LLM_FEEDBACK_MAX_RETRIES
export AFL_LLM_SKIP_STARTUP
