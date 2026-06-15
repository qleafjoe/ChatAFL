/*
 * ChatAFL/llm-validator.h - Validation-Driven LLM Fuzzing Framework
 *
 * Unified validation helpers for LLM-generated protocol messages across
 * grammar extraction, seed enrichment, and stall-breaking paths.
 */

#ifndef LLM_VALIDATOR_H
#define LLM_VALIDATOR_H

#include "types.h"
#include "aflnet.h"

/* Validation result enum. */
typedef enum {
  LLM_VALID_OK = 0,
  LLM_VALID_FORMAT_FAIL,
  LLM_VALID_GRAMMAR_FAIL,
  LLM_VALID_CONTEXT_FAIL,
  LLM_VALID_NO_GAIN
} llm_validation_result_t;

/* Validation stage enum. */
typedef enum {
  LLM_STAGE_GRAMMAR = 0,
  LLM_STAGE_ENRICHMENT,
  LLM_STAGE_STALL
} llm_generation_stage_t;

/* Validation mode enum. */
typedef enum {
  LLM_VALIDATE_DISABLED = 0,
  LLM_VALIDATE_FORMAT_ONLY,
  LLM_VALIDATE_FULL
} llm_validation_mode_t;

/* Protocol type enum. */
typedef enum {
  PROTOCOL_RTSP = 0,
  PROTOCOL_FTP,
  PROTOCOL_HTTP
} protocol_type_t;

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

typedef struct {
  llm_generation_stage_t stage;
  llm_validation_result_t result;
  char reason[128];
  u32 protocol_type;
  u32 seed_id;
  u32 llm_call_id;
  u32 input_bytes;
  u32 normalized_bytes;
  u32 region_count;
  u32 state_count;
  char response_code_seq[128];
  u8 has_new_cov;
  u8 has_new_state;
  u8 has_new_transition;
  u8 fault;
  u64 exec_us;
  /* TR5-specific fields for contextual soft validation analysis */
  llm_validation_result_t original_validation_result;      /* result before soft accept attempt */
  llm_validation_result_t post_contextualize_validation_result; /* result after contextualize */
  u8 is_transition_critical;                               /* 1 if candidate is transition-critical method */
  u8 runtime_ctx_available;                                /* 1 if runtime context was extracted */
  u8 recovered_field_count;                                /* number of fields recovered by contextualize */
  char soft_accept_reason[64];                             /* reason for soft accept decision */
  char recovered_fields[128];                              /* comma-separated list of recovered field names */
} llm_validation_record_t;

int llm_normalize_candidate(const char *raw, char **normalized);

llm_validation_result_t validate_llm_message(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *msg,
    protocol_context_t *ctx
);

llm_validation_result_t validate_llm_message_with_mode(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *msg,
    protocol_context_t *ctx,
    llm_validation_mode_t mode
);

llm_validation_result_t validate_llm_sequence(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *seq,
    protocol_context_t *ctx
);

llm_validation_result_t validate_llm_sequence_with_mode(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *seq,
    protocol_context_t *ctx,
    llm_validation_mode_t mode
);

llm_validation_result_t classify_llm_execution_gain(
    u8 has_new_cov,
    u8 has_new_state,
    u8 has_new_transition
);

int validate_rtsp_request_message(const char *message, protocol_context_t *ctx);
int validate_ftp_request_message(const char *message, protocol_context_t *ctx);
int validate_http_request_message(const char *message, protocol_context_t *ctx);
int validate_grammar_pattern(const char *message_type, const char *protocol);
int validate_protocol_request_message(const char *message, protocol_context_t *ctx);

void log_llm_validation_record(const llm_validation_record_t *record);
void init_validation_log(const char *out_dir);
void close_validation_log(void);

/* Feedback retry: return human-readable error description for a validation result.
   Returns a pointer to a static buffer — caller must NOT free it. */
const char *get_validation_error_detail(
    const char *protocol,
    llm_validation_result_t result,
    const char *failed_message);

#endif /* LLM_VALIDATOR_H */
