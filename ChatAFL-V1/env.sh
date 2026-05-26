#!/bin/bash
# ChatAFL-V1: format-only validation + auto feedback retry
: "${AFL_LLM_VALIDATION:=1}"
: "${AFL_LLM_VALIDATION_STRICT:=0}"
: "${AFL_LLM_POST_GAIN:=0}"
export AFL_LLM_VALIDATION
export AFL_LLM_VALIDATION_STRICT
export AFL_LLM_POST_GAIN
