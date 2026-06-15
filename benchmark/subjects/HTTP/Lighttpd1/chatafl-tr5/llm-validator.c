#include "llm-validator.h"
#include "alloc-inl.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

static FILE *grammar_log = NULL;
static FILE *enrichment_log = NULL;
static FILE *stall_log = NULL;

static FILE *get_stage_log(llm_generation_stage_t stage) {
  switch (stage) {
    case LLM_STAGE_GRAMMAR: return grammar_log;
    case LLM_STAGE_ENRICHMENT: return enrichment_log;
    case LLM_STAGE_STALL: return stall_log;
  }
  return NULL;
}

void init_validation_log(const char *out_dir) {
  char path[1024];
  char dir_path[1024];

  snprintf(dir_path, sizeof(dir_path), "%s/llm-validation", out_dir);
  if (mkdir(dir_path, 0755) && errno != EEXIST) {
    return;
  }

  snprintf(path, sizeof(path), "%s/llm-validation/grammar.csv", out_dir);
  grammar_log = fopen(path, "w");
  if (grammar_log) {
    fprintf(grammar_log, "time,stage,protocol,seed_id,llm_call_id,result,reason,input_bytes,normalized_bytes,region_count,state_count,response_code_seq,new_cov,new_state,new_transition,fault,exec_us,original_result,post_ctx_result,is_transition_critical,ctx_available,recovered_field_count,soft_accept_reason,recovered_fields\n");
    fflush(grammar_log);
  }

  snprintf(path, sizeof(path), "%s/llm-validation/enrichment.csv", out_dir);
  enrichment_log = fopen(path, "w");
  if (enrichment_log) {
    fprintf(enrichment_log, "time,stage,protocol,seed_id,llm_call_id,result,reason,input_bytes,normalized_bytes,region_count,state_count,response_code_seq,new_cov,new_state,new_transition,fault,exec_us,original_result,post_ctx_result,is_transition_critical,ctx_available,recovered_field_count,soft_accept_reason,recovered_fields\n");
    fflush(enrichment_log);
  }

  snprintf(path, sizeof(path), "%s/llm-validation/stall.csv", out_dir);
  stall_log = fopen(path, "w");
  if (stall_log) {
    fprintf(stall_log, "time,stage,protocol,seed_id,llm_call_id,result,reason,input_bytes,normalized_bytes,region_count,state_count,response_code_seq,new_cov,new_state,new_transition,fault,exec_us,original_result,post_ctx_result,is_transition_critical,ctx_available,recovered_field_count,soft_accept_reason,recovered_fields\n");
    fflush(stall_log);
  }
}

void close_validation_log(void) {
  if (grammar_log) {
    fclose(grammar_log);
    grammar_log = NULL;
  }
  if (enrichment_log) {
    fclose(enrichment_log);
    enrichment_log = NULL;
  }
  if (stall_log) {
    fclose(stall_log);
    stall_log = NULL;
  }
}

void log_llm_validation_record(const llm_validation_record_t *record) {
  FILE *log = get_stage_log(record->stage);
  if (!log || !record) return;

  fprintf(log,
          "%u,%d,%u,%u,%u,%d,%s,%u,%u,%u,%u,%s,%u,%u,%u,%u,%llu,%d,%d,%u,%u,%u,%s,%s\n",
          (unsigned)time(NULL), record->stage, record->protocol_type,
          record->seed_id, record->llm_call_id, record->result,
          record->reason, record->input_bytes, record->normalized_bytes,
          record->region_count, record->state_count, record->response_code_seq,
          record->has_new_cov, record->has_new_state,
          record->has_new_transition, record->fault,
          (unsigned long long)record->exec_us,
          record->original_validation_result,
          record->post_contextualize_validation_result,
          record->is_transition_critical,
          record->runtime_ctx_available,
          record->recovered_field_count,
          record->soft_accept_reason,
          record->recovered_fields);
  fflush(log);
}

int llm_normalize_candidate(const char *raw, char **normalized) {
  size_t len;
  size_t i;
  size_t j = 0;

  if (!raw || !normalized) return -1;

  len = strlen(raw);
  *normalized = ck_alloc(len + 1);
  if (!*normalized) return -1;

  for (i = 0; i < len; i++) {
    if (isprint((unsigned char)raw[i]) || raw[i] == '\r' || raw[i] == '\n') {
      (*normalized)[j++] = raw[i];
    }
  }
  (*normalized)[j] = '\0';
  return 0;
}

static int has_only_printable_protocol_bytes(const char *message) {
  size_t i;
  for (i = 0; message[i]; i++) {
    if (!isprint((unsigned char)message[i]) && message[i] != '\r' &&
        message[i] != '\n') {
      return 0;
    }
  }
  return 1;
}

static const char *strcasestr_local(const char *haystack, const char *needle) {
  size_t needle_len;
  const char *p;

  if (!haystack || !needle) return NULL;
  needle_len = strlen(needle);
  if (!needle_len) return haystack;

  for (p = haystack; *p; p++) {
    if (tolower((unsigned char)*p) == tolower((unsigned char)*needle) &&
        strncasecmp(p, needle, needle_len) == 0) {
      return p;
    }
  }
  return NULL;
}

static const char *find_header(const char *message, const char *header) {
  const char *p = message;
  while ((p = strcasestr_local(p, header)) != NULL) {
    if (p == message || p[-1] == '\n') return p;
    p++;
  }
  return NULL;
}

static int validate_header_lines(const char *message) {
  const char *line = strstr(message, "\r\n");
  if (!line) return 0;
  line += 2;

  while (*line && strncmp(line, "\r\n", 2) != 0) {
    const char *eol = strstr(line, "\r\n");
    if (!eol) return 0;
    if (!memchr(line, ':', (size_t)(eol - line))) return 0;
    line = eol + 2;
  }

  return 1;
}

static llm_validation_result_t validate_rtsp_format_only(const char *message) {
  char method[64] = {0};
  char uri[1024] = {0};
  char version[32] = {0};

  if (!message || !message[0]) return LLM_VALID_FORMAT_FAIL;
  if (!strstr(message, "\r\n\r\n")) return LLM_VALID_FORMAT_FAIL;
  if (!has_only_printable_protocol_bytes(message)) return LLM_VALID_FORMAT_FAIL;
  if (sscanf(message, "%63s %1023s %31s", method, uri, version) != 3) {
    return LLM_VALID_FORMAT_FAIL;
  }
  if (!validate_header_lines(message)) return LLM_VALID_FORMAT_FAIL;
  return LLM_VALID_OK;
}

static llm_validation_result_t validate_ftp_format_only(const char *message) {
  const char *line;

  if (!message || !message[0]) return LLM_VALID_FORMAT_FAIL;
  if (!strstr(message, "\r\n")) return LLM_VALID_FORMAT_FAIL;
  if (!has_only_printable_protocol_bytes(message)) return LLM_VALID_FORMAT_FAIL;

  line = message;
  while (*line) {
    const char *eol = strstr(line, "\r\n");
    if (!eol) return LLM_VALID_FORMAT_FAIL;
    if (eol != line) {
      const char *space = memchr(line, ' ', (size_t)(eol - line));
      size_t cmd_len = space && space < eol ? (size_t)(space - line)
                                            : (size_t)(eol - line);
      if (cmd_len == 0) return LLM_VALID_FORMAT_FAIL;
    }
    line = eol + 2;
  }

  return LLM_VALID_OK;
}

static llm_validation_result_t validate_http_format_only(const char *message) {
  char method[64] = {0};
  char uri[2048] = {0};
  char version[32] = {0};

  if (!message || !message[0]) return LLM_VALID_FORMAT_FAIL;
  if (!strstr(message, "\r\n\r\n")) return LLM_VALID_FORMAT_FAIL;
  if (!has_only_printable_protocol_bytes(message)) return LLM_VALID_FORMAT_FAIL;
  if (sscanf(message, "%63s %2047s %31s", method, uri, version) != 3) {
    return LLM_VALID_FORMAT_FAIL;
  }
  if (!validate_header_lines(message)) return LLM_VALID_FORMAT_FAIL;
  return LLM_VALID_OK;
}

static const char *rtsp_methods[] = {
    "OPTIONS", "DESCRIBE", "SETUP", "PLAY", "PAUSE", "TEARDOWN",
    "ANNOUNCE", "RECORD", "GET_PARAMETER", "SET_PARAMETER", "REDIRECT",
    NULL};

static const char *ftp_commands[] = {
    "USER", "PASS", "PWD",  "CWD",  "CDUP", "LIST", "NLST",
    "RETR", "STOR", "APPE", "DELE", "RNFR", "RNTO", "MKD",
    "RMD",  "SITE", "SYST", "STAT", "HELP", "NOOP", "QUIT",
    "PASV", "PORT", "TYPE", "MODE", "STRU", "REST", NULL};

static const char *http_methods[] = {
    "GET", "POST", "PUT", "DELETE", "HEAD",
    "OPTIONS", "PATCH", "TRACE", "CONNECT", NULL};

static int is_valid_ftp_command(const char *cmd, size_t cmd_len) {
  int i;
  for (i = 0; ftp_commands[i]; i++) {
    if (strlen(ftp_commands[i]) == cmd_len &&
        strncasecmp(cmd, ftp_commands[i], cmd_len) == 0) {
      return 1;
    }
  }
  return 0;
}

static llm_validation_result_t validate_rtsp_full(const char *message,
                                                  protocol_context_t *ctx) {
  char method[64] = {0};
  char uri[1024] = {0};
  char version[32] = {0};
  const char *cseq_start;
  int valid_method = 0;
  int i;

  llm_validation_result_t format_result = validate_rtsp_format_only(message);
  if (format_result != LLM_VALID_OK) return format_result;

  if (sscanf(message, "%63s %1023s %31s", method, uri, version) != 3) {
    return LLM_VALID_FORMAT_FAIL;
  }

  if (strcmp(version, "RTSP/1.0") != 0) return LLM_VALID_GRAMMAR_FAIL;
  if (strcmp(uri, "*") != 0 && strncmp(uri, "rtsp://", 7) != 0 &&
      strncmp(uri, "rtsps://", 8) != 0) {
    return LLM_VALID_GRAMMAR_FAIL;
  }

  for (i = 0; rtsp_methods[i]; i++) {
    if (strcmp(method, rtsp_methods[i]) == 0) {
      valid_method = 1;
      break;
    }
  }
  if (!valid_method) return LLM_VALID_GRAMMAR_FAIL;

  if (!find_header(message, "cseq:")) return LLM_VALID_GRAMMAR_FAIL;
  /* TR5: SETUP without Transport is CONTEXT_FAIL (recoverable from history) */
  if (strcmp(method, "SETUP") == 0 && !find_header(message, "transport:")) {
    return LLM_VALID_CONTEXT_FAIL;
  }
  /* PLAY/PAUSE/TEARDOWN without Session is CONTEXT_FAIL (recoverable from history) */
  if ((strcmp(method, "PLAY") == 0 || strcmp(method, "PAUSE") == 0 ||
       strcmp(method, "TEARDOWN") == 0) &&
      !find_header(message, "session:")) {
    return LLM_VALID_CONTEXT_FAIL;
  }

  ctx->type = PROTOCOL_RTSP;
  cseq_start = find_header(message, "cseq:");
  if (cseq_start) sscanf(cseq_start + 5, " %u", &ctx->ctx.rtsp.last_cseq);
  if (find_header(message, "session:")) ctx->ctx.rtsp.has_session = 1;
  if (find_header(message, "transport:")) ctx->ctx.rtsp.has_transport = 1;

  return LLM_VALID_OK;
}

static llm_validation_result_t validate_ftp_full(const char *message,
                                                 protocol_context_t *ctx) {
  const char *line = message;
  llm_validation_result_t format_result = validate_ftp_format_only(message);
  if (format_result != LLM_VALID_OK) return format_result;

  while (*line) {
    const char *eol = strstr(line, "\r\n");
    const char *space;
    size_t cmd_len;
    char cmd_buf[16] = {0};

    if (!eol) return LLM_VALID_FORMAT_FAIL;
    if (eol == line) {
      line = eol + 2;
      continue;
    }

    space = memchr(line, ' ', (size_t)(eol - line));
    cmd_len = (space && space < eol) ? (size_t)(space - line)
                                     : (size_t)(eol - line);
    if (cmd_len == 0 || cmd_len >= sizeof(cmd_buf)) return LLM_VALID_FORMAT_FAIL;
    if (!is_valid_ftp_command(line, cmd_len)) return LLM_VALID_GRAMMAR_FAIL;

    memcpy(cmd_buf, line, cmd_len);
    cmd_buf[cmd_len] = '\0';

    if (strcasecmp(cmd_buf, "PASS") == 0 && !ctx->ctx.ftp.has_user) {
      return LLM_VALID_CONTEXT_FAIL;
    }
    if ((strcasecmp(cmd_buf, "RETR") == 0 || strcasecmp(cmd_buf, "STOR") == 0 ||
         strcasecmp(cmd_buf, "LIST") == 0) &&
        !ctx->ctx.ftp.is_authed) {
      return LLM_VALID_CONTEXT_FAIL;
    }

    if (strcasecmp(cmd_buf, "USER") == 0) {
      ctx->ctx.ftp.has_user = 1;
      ctx->ctx.ftp.has_pass = 0;
      ctx->ctx.ftp.is_authed = 0;
    } else if (strcasecmp(cmd_buf, "PASS") == 0) {
      ctx->ctx.ftp.has_pass = 1;
      if (ctx->ctx.ftp.has_user) ctx->ctx.ftp.is_authed = 1;
    }

    line = eol + 2;
  }

  ctx->type = PROTOCOL_FTP;
  return LLM_VALID_OK;
}

static llm_validation_result_t validate_http_full(const char *message,
                                                  protocol_context_t *ctx) {
  char method[64] = {0};
  char uri[2048] = {0};
  char version[32] = {0};
  const char *header_body_sep;
  const char *cl_header;
  int valid_method = 0;
  int i;

  llm_validation_result_t format_result = validate_http_format_only(message);
  if (format_result != LLM_VALID_OK) return format_result;

  if (sscanf(message, "%63s %2047s %31s", method, uri, version) != 3) {
    return LLM_VALID_FORMAT_FAIL;
  }

  for (i = 0; http_methods[i]; i++) {
    if (strcmp(method, http_methods[i]) == 0) {
      valid_method = 1;
      break;
    }
  }
  if (!valid_method) return LLM_VALID_GRAMMAR_FAIL;
  if (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0) {
    return LLM_VALID_GRAMMAR_FAIL;
  }

  header_body_sep = strstr(message, "\r\n\r\n");
  if (!header_body_sep) return LLM_VALID_FORMAT_FAIL;

  cl_header = find_header(message, "content-length:");
  if (cl_header) {
    const char *cl_value = cl_header + 15;
    const char *body_start;
    const char *body_end;
    long content_length;
    long actual_body_len;

    while (*cl_value == ' ' || *cl_value == '\t') cl_value++;
    content_length = atol(cl_value);
    if (content_length < 0) return LLM_VALID_GRAMMAR_FAIL;

    body_start = header_body_sep + 4;
    body_end = message + strlen(message);
    actual_body_len = (long)(body_end - body_start);
    if (actual_body_len != content_length) return LLM_VALID_GRAMMAR_FAIL;
  }

  ctx->type = PROTOCOL_HTTP;
  if (find_header(message, "host:")) ctx->ctx.http.has_host = 1;
  if (cl_header) ctx->ctx.http.has_content_length = 1;
  return LLM_VALID_OK;
}

llm_validation_result_t validate_llm_message_with_mode(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *msg,
    protocol_context_t *ctx,
    llm_validation_mode_t mode) {
  (void)stage;

  if (!protocol || !msg || !ctx) return LLM_VALID_FORMAT_FAIL;
  if (mode == LLM_VALIDATE_DISABLED) return LLM_VALID_OK;

  if (strcmp(protocol, "RTSP") == 0) {
    ctx->type = PROTOCOL_RTSP;
    return mode == LLM_VALIDATE_FORMAT_ONLY ? validate_rtsp_format_only(msg)
                                            : validate_rtsp_full(msg, ctx);
  }
  if (strcmp(protocol, "FTP") == 0) {
    ctx->type = PROTOCOL_FTP;
    return mode == LLM_VALIDATE_FORMAT_ONLY ? validate_ftp_format_only(msg)
                                            : validate_ftp_full(msg, ctx);
  }
  if (strcmp(protocol, "HTTP") == 0) {
    ctx->type = PROTOCOL_HTTP;
    return mode == LLM_VALIDATE_FORMAT_ONLY ? validate_http_format_only(msg)
                                            : validate_http_full(msg, ctx);
  }

  return LLM_VALID_GRAMMAR_FAIL;
}

llm_validation_result_t validate_llm_message(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *msg,
    protocol_context_t *ctx) {
  return validate_llm_message_with_mode(protocol, stage, msg, ctx,
                                        LLM_VALIDATE_FULL);
}

llm_validation_result_t validate_llm_sequence_with_mode(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *seq,
    protocol_context_t *ctx,
    llm_validation_mode_t mode) {
  region_t *regions = NULL;
  unsigned int region_count = 0;
  size_t seq_len;
  unsigned int i;

  if (!protocol || !seq || !ctx) return LLM_VALID_FORMAT_FAIL;
  if (mode == LLM_VALIDATE_DISABLED) return LLM_VALID_OK;

  if (strcmp(protocol, "RTSP") == 0) {
    regions = extract_requests_rtsp((unsigned char *)seq, (unsigned int)strlen(seq),
                                    &region_count);
  } else if (strcmp(protocol, "FTP") == 0) {
    regions = extract_requests_ftp((unsigned char *)seq, (unsigned int)strlen(seq),
                                   &region_count);
  } else if (strcmp(protocol, "HTTP") == 0) {
    regions = extract_requests_http((unsigned char *)seq, (unsigned int)strlen(seq),
                                    &region_count);
  }

  if (!regions || region_count == 0) return LLM_VALID_FORMAT_FAIL;

  seq_len = strlen(seq);
  for (i = 0; i < region_count; i++) {
    size_t msg_len;
    char *msg;
    llm_validation_result_t result;

    if (regions[i].start_byte < 0 || regions[i].end_byte < 0 ||
        regions[i].end_byte <= regions[i].start_byte) {
      continue;
    }
    if ((size_t)regions[i].end_byte >= seq_len) continue;

    msg_len = (size_t)(regions[i].end_byte - regions[i].start_byte + 1);
    msg = ck_alloc(msg_len + 1);
    if (!msg) continue;

    memcpy(msg, seq + regions[i].start_byte, msg_len);
    msg[msg_len] = '\0';

    result = validate_llm_message_with_mode(protocol, stage, msg, ctx, mode);
    ck_free(msg);
    if (result != LLM_VALID_OK) {
      ck_free(regions);
      return result;
    }
  }

  ck_free(regions);
  return LLM_VALID_OK;
}

llm_validation_result_t validate_llm_sequence(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *seq,
    protocol_context_t *ctx) {
  return validate_llm_sequence_with_mode(protocol, stage, seq, ctx,
                                         LLM_VALIDATE_FULL);
}

llm_validation_result_t classify_llm_execution_gain(
    u8 has_new_cov,
    u8 has_new_state,
    u8 has_new_transition) {
  if (has_new_cov || has_new_state || has_new_transition) return LLM_VALID_OK;
  return LLM_VALID_NO_GAIN;
}

int validate_rtsp_request_message(const char *message, protocol_context_t *ctx) {
  return validate_rtsp_full(message, ctx) == LLM_VALID_OK;
}

int validate_ftp_request_message(const char *message, protocol_context_t *ctx) {
  return validate_ftp_full(message, ctx) == LLM_VALID_OK;
}

int validate_http_request_message(const char *message, protocol_context_t *ctx) {
  return validate_http_full(message, ctx) == LLM_VALID_OK;
}

int validate_grammar_pattern(const char *message_type, const char *protocol) {
  int i;

  if (!message_type || !protocol) return 0;

  if (strcmp(protocol, "RTSP") == 0) {
    for (i = 0; rtsp_methods[i]; i++) {
      if (strcasecmp(message_type, rtsp_methods[i]) == 0) return 1;
    }
  } else if (strcmp(protocol, "FTP") == 0) {
    for (i = 0; ftp_commands[i]; i++) {
      if (strcasecmp(message_type, ftp_commands[i]) == 0) return 1;
    }
  } else if (strcmp(protocol, "HTTP") == 0) {
    for (i = 0; http_methods[i]; i++) {
      if (strcmp(message_type, http_methods[i]) == 0) return 1;
    }
  }

  return 0;
}

int validate_protocol_request_message(const char *message, protocol_context_t *ctx) {
  if (!message || !ctx) return 0;
  return validate_rtsp_request_message(message, ctx);
}

const char *get_validation_error_detail(
    const char *protocol,
    llm_validation_result_t result,
    const char *failed_message) {
  static char detail[256];

  if (result == LLM_VALID_FORMAT_FAIL) {
    if (failed_message && !strstr(failed_message, "\r\n\r\n"))
      snprintf(detail, sizeof(detail),
               "Message missing CRLF CRLF terminator (\\r\\n\\r\\n). "
               "Ensure the message ends with proper HTTP/RTSP header terminator.");
    else
      snprintf(detail, sizeof(detail),
               "Message format invalid: non-printable characters, malformed request line, "
               "or missing 'METHOD URI VERSION' format.");
    return detail;
  }

  if (result == LLM_VALID_GRAMMAR_FAIL) {
    char method[64] = {0};
    if (failed_message && sscanf(failed_message, "%63s", method) == 1) {
      if (strcmp(protocol, "RTSP") == 0)
        snprintf(detail, sizeof(detail),
                 "Method '%s' is not valid for RTSP. "
                 "Valid methods: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, "
                 "ANNOUNCE, RECORD, GET_PARAMETER, SET_PARAMETER. "
                 "Also ensure CSeq header is present.", method);
      else if (strcmp(protocol, "FTP") == 0)
        snprintf(detail, sizeof(detail),
                 "Command '%s' is not valid for FTP. "
                 "Valid commands: USER, PASS, PWD, CWD, LIST, RETR, STOR, QUIT, "
                 "PASV, PORT, TYPE, MODE, NOOP.", method);
      else if (strcmp(protocol, "HTTP") == 0)
        snprintf(detail, sizeof(detail),
                 "Method '%s' is not valid for HTTP. "
                 "Valid methods: GET, POST, PUT, DELETE, HEAD, OPTIONS, PATCH.", method);
      else
        snprintf(detail, sizeof(detail),
                 "Method '%s' is not valid for %s protocol.", method, protocol);
    } else {
      snprintf(detail, sizeof(detail),
               "Grammar validation failed: invalid method or missing required headers.");
    }
    return detail;
  }

  if (result == LLM_VALID_CONTEXT_FAIL) {
    if (strcmp(protocol, "RTSP") == 0)
      snprintf(detail, sizeof(detail),
               "Context error: PLAY/PAUSE/TEARDOWN require a Session header "
               "obtained from a prior SETUP request.");
    else if (strcmp(protocol, "FTP") == 0)
      snprintf(detail, sizeof(detail),
               "Context error: PASS requires prior USER command; "
               "RETR/STOR/LIST require prior authentication (USER then PASS).");
    else if (strcmp(protocol, "HTTP") == 0)
      snprintf(detail, sizeof(detail),
               "Context error: request requires Host header.");
    else
      snprintf(detail, sizeof(detail), "Protocol context state violation.");
    return detail;
  }

  snprintf(detail, sizeof(detail), "Unknown validation error (code %d).", result);
  return detail;
}
