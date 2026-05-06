// ChatAFL/llm-validator.c

#include "llm-validator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

// 日志文件句柄
static FILE *grammar_log = NULL;
static FILE *enrichment_log = NULL;
static FILE *stall_log = NULL;

// 初始化验证日志
void init_validation_log(const char *out_dir) {
    char path[1024];

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

// 规范化候选输入
int llm_normalize_candidate(const char *raw, char **normalized) {
    if (!raw || !normalized) return -1;

    size_t len = strlen(raw);
    *normalized = malloc(len + 1);
    if (!*normalized) return -1;

    // 复制并清理
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

    // 格式验证
    if (strlen(msg) == 0) return LLM_VALID_FORMAT_FAIL;
    if (!strstr(msg, "\r\n\r\n")) return LLM_VALID_FORMAT_FAIL;

    // 协议级验证
    int valid = 0;
    if (strcmp(protocol, "RTSP") == 0) {
        valid = validate_rtsp_request_message(msg, ctx);
    } else if (strcmp(protocol, "FTP") == 0) {
        valid = validate_ftp_request_message(msg, ctx);
    } else if (strcmp(protocol, "HTTP") == 0) {
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

    // 逐条验证
    for (unsigned int i = 0; i < region_count; i++) {
        size_t msg_len = regions[i].end_byte - regions[i].start_byte;
        char *msg = malloc(msg_len + 1);
        if (!msg) continue;

        memcpy(msg, seq + regions[i].start_byte, msg_len);
        msg[msg_len] = '\0';

        llm_validation_result_t result = validate_llm_message(protocol, stage, msg, ctx);
        free(msg);

        if (result != LLM_VALID_OK) {
            free(regions);
            return result;
        }
    }

    free(regions);
    return LLM_VALID_OK;
}

// Protocol-level validators (stubs - will be implemented in Task 3-5)
// These stubs allow the module to compile independently.

int validate_rtsp_request_message(const char *message, protocol_context_t *ctx) {
    // TODO: Implement in Task 3
    (void)message;
    (void)ctx;
    return 1; // Temporarily accept all messages
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
