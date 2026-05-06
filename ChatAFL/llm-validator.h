// ChatAFL/llm-validator.h

#ifndef LLN_VALIDATOR_H
#define LLN_VALIDATOR_H

#include "types.h"
#include "aflnet.h"

// Validation result enum
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

// Core validation interfaces
int llm_normalize_candidate(const char *raw, char **normalized);
llm_validation_result_t validate_llm_message(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *msg,
    protocol_context_t *ctx
);
llm_validation_result_t validate_llm_sequence(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *seq,
    protocol_context_t *ctx
);

// Protocol-level validators
int validate_rtsp_request_message(const char *message, protocol_context_t *ctx);
int validate_ftp_request_message(const char *message, protocol_context_t *ctx);
int validate_http_request_message(const char *message, protocol_context_t *ctx);

// Logging interfaces
void log_llm_validation_record(const llm_validation_record_t *record);
void init_validation_log(const char *out_dir);
void close_validation_log();

#endif // LLN_VALIDATOR_H
