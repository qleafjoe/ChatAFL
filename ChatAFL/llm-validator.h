/*
 * ChatAFL/llm-validator.h - Validation-Driven LLM Fuzzing Framework
 *
 * This module implements a unified validation framework for LLM-generated
 * protocol messages in ChatAFL. It provides multi-level validation
 * (format, grammar, context) across three LLM data paths:
 * grammar extraction, seed enrichment, and stall breaking.
 *
 * Copyright 2026 ChatAFL Project
 * Licensed under the Apache License, Version 2.0
 */

#ifndef LLM_VALIDATOR_H
#define LLM_VALIDATOR_H

#include "types.h"
#include "aflnet.h"

/*
 * Validation result enum
 *
 * Pre-execution validation results (can reject before execution):
 *   LLM_VALID_OK         - Message passed all validation checks
 *   LLM_VALID_FORMAT_FAIL  - Message has format errors (e.g., missing \r\n\r\n)
 *   LLM_VALID_GRAMMAR_FAIL - Message has grammar errors (e.g., invalid method)
 *   LLM_VALID_CONTEXT_FAIL - Message violates session dependencies
 *
 * Post-execution classification (requires execution feedback):
 *   LLM_VALID_NO_GAIN - Message produced no new coverage/state/transition
 *
 * Note: LLM_VALID_STATE_FAIL was considered but removed because determining
 * whether a message advances the state machine requires execution feedback,
 * making it a post-execution classification rather than pre-execution validation.
 */
typedef enum {
  LLM_VALID_OK = 0,
  LLM_VALID_FORMAT_FAIL,    // Format error (pre-execution)
  LLM_VALID_GRAMMAR_FAIL,   // Grammar error (pre-execution)
  LLM_VALID_CONTEXT_FAIL,   // Context error (pre-execution)
  LLM_VALID_NO_GAIN         // No gain (post-execution classification)
} llm_validation_result_t;

// Validation stage enum
typedef enum {
  LLM_STAGE_GRAMMAR = 0,
  LLM_STAGE_ENRICHMENT,
  LLM_STAGE_STALL
} llm_generation_stage_t;

// Protocol type enum
typedef enum {
  PROTOCOL_RTSP = 0,
  PROTOCOL_FTP,
  PROTOCOL_HTTP
} protocol_type_t;

// Protocol context structure (tagged union)
typedef struct {
  u32 last_cseq;
  u8 has_session;
  u8 has_transport;
} rtsp_context_t;

typedef struct {
  u8 has_user;
  u8 has_pass;
  u8 is_authed;
} ftp_context_t;

typedef struct {
  u8 has_content_length;
  u8 has_host;
} http_context_t;

typedef struct {
  protocol_type_t type;
  union {
    rtsp_context_t rtsp;
    ftp_context_t ftp;
    http_context_t http;
  } ctx;
} protocol_context_t;

// Validation record structure
typedef struct {
  llm_generation_stage_t stage;
  llm_validation_result_t result;
  char reason[128];
  u32 region_count;
  u32 state_count;
  u8 has_new_cov;
  u8 has_new_state;
  u8 has_new_transition;
} llm_validation_record_t;

/*
 * Core validation interfaces
 */

/*
 * llm_normalize_candidate - Normalize LLM output for validation
 * @raw: Raw LLM output string
 * @normalized: Output pointer (caller must free with ck_free)
 *
 * Returns: 0 on success, -1 on error
 *
 * Memory ownership: Caller must free *normalized with ck_free().
 */
int llm_normalize_candidate(const char *raw, char **normalized);

/*
 * validate_llm_message - Validate a single LLM-generated message
 * @protocol: Protocol name ("RTSP", "FTP", "HTTP")
 * @stage: LLM generation stage (grammar/enrichment/stall)
 * @msg: Message to validate
 * @ctx: Protocol context (updated by this function)
 *
 * Returns: LLM_VALID_OK if valid, or specific failure code
 */
llm_validation_result_t validate_llm_message(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *msg,
    protocol_context_t *ctx
);

/*
 * validate_llm_sequence - Validate an LLM-generated message sequence
 * @protocol: Protocol name ("RTSP", "FTP", "HTTP")
 * @stage: LLM generation stage (grammar/enrichment/stall)
 * @seq: Message sequence to validate
 * @ctx: Protocol context (updated by this function)
 *
 * Returns: LLM_VALID_OK if valid, or specific failure code
 */
llm_validation_result_t validate_llm_sequence(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *seq,
    protocol_context_t *ctx
);

/*
 * Protocol-level validators
 *
 * Return: 1 if valid, 0 if invalid
 * Note: Returns int for backward compatibility with existing
 *       validate_protocol_request_message() in benchmark code.
 */
int validate_rtsp_request_message(const char *message, protocol_context_t *ctx);
int validate_ftp_request_message(const char *message, protocol_context_t *ctx);
int validate_http_request_message(const char *message, protocol_context_t *ctx);

// Logging interfaces
void log_llm_validation_record(const llm_validation_record_t *record);
void init_validation_log(const char *out_dir);
void close_validation_log(void);

#endif // LLM_VALIDATOR_H
