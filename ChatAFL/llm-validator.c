// ChatAFL/llm-validator.c

#include "llm-validator.h"
#include "alloc-inl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

// 日志文件句柄
static FILE *grammar_log = NULL;
static FILE *enrichment_log = NULL;
static FILE *stall_log = NULL;

// 初始化验证日志
void init_validation_log(const char *out_dir) {
    char path[1024];
    char dir_path[1024];

    // Create llm-validation directory
    snprintf(dir_path, sizeof(dir_path), "%s/llm-validation", out_dir);
    if (mkdir(dir_path, 0755) && errno != EEXIST) {
        return; // Failed to create directory
    }

    snprintf(path, sizeof(path), "%s/llm-validation/grammar.csv", out_dir);
    grammar_log = fopen(path, "w");
    if (grammar_log) {
        fprintf(grammar_log, "time,stage,protocol,seed_id,llm_call_id,result,reason,input_bytes,normalized_bytes,region_count,state_count,response_code_seq,new_cov,new_state,new_transition,fault,exec_us\n");
    }

    snprintf(path, sizeof(path), "%s/llm-validation/enrichment.csv", out_dir);
    enrichment_log = fopen(path, "w");
    if (enrichment_log) {
        fprintf(enrichment_log, "time,stage,protocol,seed_id,llm_call_id,result,reason,input_bytes,normalized_bytes,region_count,state_count,response_code_seq,new_cov,new_state,new_transition,fault,exec_us\n");
    }

    snprintf(path, sizeof(path), "%s/llm-validation/stall.csv", out_dir);
    stall_log = fopen(path, "w");
    if (stall_log) {
        fprintf(stall_log, "time,stage,protocol,seed_id,llm_call_id,result,reason,input_bytes,normalized_bytes,region_count,state_count,response_code_seq,new_cov,new_state,new_transition,fault,exec_us\n");
    }
}

// 关闭验证日志
void close_validation_log(void) {
    if (grammar_log) { fclose(grammar_log); grammar_log = NULL; }
    if (enrichment_log) { fclose(enrichment_log); enrichment_log = NULL; }
    if (stall_log) { fclose(stall_log); stall_log = NULL; }
}

// 记录验证结果
void log_llm_validation_record(const llm_validation_record_t *record) {
    FILE *log = NULL;
    switch (record->stage) {
        case LLM_STAGE_GRAMMAR: log = grammar_log; break;
        case LLM_STAGE_ENRICHMENT: log = enrichment_log; break;
        case LLM_STAGE_STALL: log = stall_log; break;
    }

    if (log) {
        fprintf(log, "%u,%d,%d,%u,%u,%d,%s,%u,%u,%u,%u,%s,%u,%u,%u,%u,%u\n",
                (unsigned)time(NULL),
                record->stage,
                0, // protocol type
                0, // seed_id
                0, // llm_call_id
                record->result,
                record->reason,
                0, // input_bytes
                0, // normalized_bytes
                record->region_count,
                record->state_count,
                "", // response_code_seq
                record->has_new_cov,
                record->has_new_state,
                record->has_new_transition,
                0, // fault
                0  // exec_us
        );
    }
}

/* 规范化候选输入 */
int llm_normalize_candidate(const char *raw, char **normalized) {
    if (!raw || !normalized) return -1;

    size_t len = strlen(raw);
    *normalized = ck_alloc(len + 1);
    if (!*normalized) return -1;

    /* 复制并清理 */
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (isprint((unsigned char)raw[i]) || raw[i] == '\r' || raw[i] == '\n') {
            (*normalized)[j++] = raw[i];
        }
    }
    (*normalized)[j] = '\0';

    return 0;
}

// 验证单条消息
llm_validation_result_t validate_llm_message(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *msg,
    protocol_context_t *ctx
) {
    if (!protocol || !msg || !ctx) return LLM_VALID_FORMAT_FAIL;

    // 基础格式验证
    if (strlen(msg) == 0) return LLM_VALID_FORMAT_FAIL;

    // 协议级验证（包含协议特定的格式检查）
    int valid = 0;
    if (strcmp(protocol, "RTSP") == 0) {
        // RTSP 需要 \r\n\r\n 终止符
        if (!strstr(msg, "\r\n\r\n")) return LLM_VALID_FORMAT_FAIL;
        valid = validate_rtsp_request_message(msg, ctx);
    } else if (strcmp(protocol, "FTP") == 0) {
        // FTP 命令以 \r\n 终止
        if (!strstr(msg, "\r\n")) return LLM_VALID_FORMAT_FAIL;
        valid = validate_ftp_request_message(msg, ctx);
    } else if (strcmp(protocol, "HTTP") == 0) {
        // HTTP 需要 \r\n\r\n 终止符
        if (!strstr(msg, "\r\n\r\n")) return LLM_VALID_FORMAT_FAIL;
        valid = validate_http_request_message(msg, ctx);
    } else {
        return LLM_VALID_GRAMMAR_FAIL; // 不支持的协议
    }

    if (!valid) return LLM_VALID_GRAMMAR_FAIL;

    return LLM_VALID_OK;
}

// 验证消息序列
llm_validation_result_t validate_llm_sequence(
    const char *protocol,
    llm_generation_stage_t stage,
    const char *seq,
    protocol_context_t *ctx
) {
    if (!protocol || !seq || !ctx) return LLM_VALID_FORMAT_FAIL;

    // 使用 extract_requests_* 切分序列
    region_t *regions = NULL;
    unsigned int region_count = 0;

    if (strcmp(protocol, "RTSP") == 0) {
        regions = extract_requests_rtsp((unsigned char *)seq, (unsigned int)strlen(seq), &region_count);
    } else if (strcmp(protocol, "FTP") == 0) {
        regions = extract_requests_ftp((unsigned char *)seq, (unsigned int)strlen(seq), &region_count);
    } else if (strcmp(protocol, "HTTP") == 0) {
        regions = extract_requests_http((unsigned char *)seq, (unsigned int)strlen(seq), &region_count);
    }

    if (!regions || region_count == 0) return LLM_VALID_FORMAT_FAIL;

    /*逐条验证 */
    size_t seq_len = strlen(seq);
    for (unsigned int i = 0; i < region_count; i++) {
        /* Guard against malformed regions with negative indices */
        if (regions[i].start_byte < 0 || regions[i].end_byte < 0 ||
            regions[i].end_byte <= regions[i].start_byte) continue;

        /* Defensive bounds check against sequence length */
        if ((size_t)regions[i].end_byte > seq_len) continue;

        size_t msg_len = (size_t)(regions[i].end_byte - regions[i].start_byte);
        char *msg = ck_alloc(msg_len + 1);
        if (!msg) continue;

        memcpy(msg, seq + regions[i].start_byte, msg_len);
        msg[msg_len] = '\0';

        llm_validation_result_t result = validate_llm_message(protocol, stage, msg, ctx);
        ck_free(msg);

        if (result != LLM_VALID_OK) {
            ck_free(regions);
            return result;
        }
    }

    ck_free(regions);
    return LLM_VALID_OK;
}

// Protocol-level validators
// FTP and HTTP are stubs - will be implemented in Task 4-5

/* 合法 RTSP 方法集合 */
static const char *rtsp_methods[] = {
    "OPTIONS", "DESCRIBE", "SETUP", "PLAY", "PAUSE",
    "TEARDOWN", "ANNOUNCE", "RECORD", "GET_PARAMETER",
    "SET_PARAMETER", "REDIRECT", NULL
};

/* Case-insensitive substring search (RFC 2326 headers are case-insensitive) */
static const char *strcasestr_local(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return haystack;

    for (const char *p = haystack; *p; p++) {
        if (tolower((unsigned char)*p) == tolower((unsigned char)*needle) &&
            strncasecmp(p, needle, needle_len) == 0) {
            return p;
        }
    }
    return NULL;
}

/* Find a header at the start of a line (not a substring of another header name) */
static const char *find_header(const char *message, const char *header) {
    const char *p = message;
    while ((p = strcasestr_local(p, header)) != NULL) {
        /* Check that match is at start of message or after \n */
        if (p == message || p[-1] == '\n') {
            return p;
        }
        p++; /* Skip this false match, continue searching */
    }
    return NULL;
}

int validate_rtsp_request_message(const char *message, protocol_context_t *ctx) {
    if (!message || !ctx) return 0;

    /* 1. 必须以 \r\n\r\n 结束 */
    if (!strstr(message, "\r\n\r\n")) return 0;

    /* 2. 验证可打印字符（允许 \r\n） */
    for (size_t i = 0; message[i]; i++) {
        if (!isprint((unsigned char)message[i]) && message[i] != '\r' &&
            message[i] != '\n') return 0;
    }

    /* 3. 解析请求行 */
    char method[64] = {0};
    char uri[1024] = {0};
    char version[32] = {0};

    if (sscanf(message, "%63s %1023s %31s", method, uri, version) != 3) return 0;

    /* 4. 验证 RTSP 版本 */
    if (strcmp(version, "RTSP/1.0") != 0) return 0;

    /* 5. 验证 URI 格式 */
    if (strcmp(uri, "*") != 0 && strncmp(uri, "rtsp://", 7) != 0 &&
        strncmp(uri, "rtsps://", 8) != 0) return 0;

    /* 6. 检查方法是否在合法集合中 */
    int valid_method = 0;
    for (int i = 0; rtsp_methods[i]; i++) {
        if (strcmp(method, rtsp_methods[i]) == 0) {
            valid_method = 1;
            break;
        }
    }
    if (!valid_method) return 0;

    /* 7. 验证 header 行格式（每行必须是 Key: Value 或空行） */
    const char *line = strstr(message, "\r\n");
    if (!line) return 0;
    line += 2; /* 跳过请求行 */
    while (*line && strncmp(line, "\r\n", 2) != 0) {
        const char *eol = strstr(line, "\r\n");
        if (!eol) return 0;
        /* header 行必须包含 ':' */
        if (!memchr(line, ':', (size_t)(eol - line))) return 0;
        line = eol + 2;
    }

    /* 8. 检查必需头字段（case-insensitive, at line start） */
    if (!find_header(message, "cseq:")) return 0;

    /* SETUP 必须有 Transport */
    if (strcmp(method, "SETUP") == 0 && !find_header(message, "transport:")) return 0;

    /* PLAY/PAUSE/TEARDOWN 必须有 Session */
    if ((strcmp(method, "PLAY") == 0 || strcmp(method, "PAUSE") == 0 || strcmp(method, "TEARDOWN") == 0) &&
        !find_header(message, "session:")) return 0;

    /* 9. 更新上下文 */
    ctx->type = PROTOCOL_RTSP;

    /* 提取 CSeq */
    const char *cseq_start = find_header(message, "cseq:");
    if (cseq_start) {
        sscanf(cseq_start + 5, " %u", &ctx->ctx.rtsp.last_cseq);
    }

    /* 检查 Session */
    if (find_header(message, "session:")) {
        ctx->ctx.rtsp.has_session = 1;
    }

    /* 检查 Transport */
    if (find_header(message, "transport:")) {
        ctx->ctx.rtsp.has_transport = 1;
    }

    return 1;
}

/* Backward compatibility wrapper for benchmark code */
int validate_protocol_request_message(const char *message, protocol_context_t *ctx) {
    if (!message || !ctx) return 0;
    return validate_rtsp_request_message(message, ctx);
}

int validate_ftp_request_message(const char *message, protocol_context_t *ctx) {
    // TODO: Implement in Task 4
    (void)message;
    (void)ctx;
    return 1; // Temporarily accept all messages
}

int validate_http_request_message(const char *message, protocol_context_t *ctx) {
    // TODO: Implement in Task 5
    (void)message;
    (void)ctx;
    return 1; // Temporarily accept all messages
}
