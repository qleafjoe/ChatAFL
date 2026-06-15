#!/bin/bash
# ChatAFL-V2: full validation (grammar + context) + auto feedback retry
: "${AFL_LLM_VALIDATION:=1}"
: "${AFL_LLM_VALIDATION_STRICT:=1}"
: "${AFL_LLM_POST_GAIN:=0}"
export AFL_LLM_VALIDATION
export AFL_LLM_VALIDATION_STRICT
export AFL_LLM_POST_GAIN
