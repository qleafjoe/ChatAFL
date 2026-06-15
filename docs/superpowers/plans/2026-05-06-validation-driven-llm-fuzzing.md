# Validation-Driven LLM Fuzzing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 Validation-Driven LLM Fuzzing 框架，通过统一的验证机制控制 LLM 输出质量，提升协议 fuzzing 效果。

**Architecture:** 创建独立的 `llm-validator.h/c` 模块，实现 RTSP/FTP/HTTP 三种协议的 validator，集成到 ChatAFL 的 grammar/enrichment/stall 三条 LLM 数据路径，通过环境变量控制验证行为，支持消融实验。

**Tech Stack:** C, PCRE2, CURL, json-c, AFL/AFLNet

---

## 文件结构

### 新增文件
- `ChatAFL/llm-validator.h` - Validator 框架头文件
- `ChatAFL/llm-validator.c` - Validator 框架实现
- `ChatAFL/test_llm_validator.c` - 单元测试
- `ChatAFL/test_llm_integration.c` - 集成测试

### 修改文件
- `ChatAFL/chat-llm.h` - 添加 validator 集成声明
- `ChatAFL/chat-llm.c` - 集成 validator 调用
- `ChatAFL/afl-fuzz.c` - 添加环境变量开关、集成 validator
- `ChatAFL/Makefile` - 添加 llm-validator.o

### 不修改文件
- `ChatAFL/aflnet.c` - 复用现有 `extract_requests_*` 函数

---

## Task 1: 创建 Validator 框架头文件

**Files:**
- Create: `ChatAFL/llm-validator.h`

- [ ] **Step 1: 创建头文件框架**

```c
// ChatAFL/llm-validator.h

#ifndef LLN_VALIDATOR_H
#define LLN_VALIDATOR_H

#include "aflnet.h"

// 验证结果枚举
typedef enum {
  LLM_VALID_OK = 0,
  LLM_VALID_FORMAT_FAIL,    // 格式错误（预执行）
  LLM_VALID_GRAMMAR_FAIL,   // 语法错误（预执行）
  LLM_VALID_CONTEXT_FAIL,   // 上下文错误（预执行）
  LLM_VALID_NO_GAIN         // 无收益（执行后分类）
} llm_validation_result_t;

// 验证阶段枚举
typedef enum {
  LLM_STAGE_GRAMMAR = 0,
  LLM_STAGE_ENRICHMENT,
  LLM_STAGE_STALL
} llm_generation_stage_t;

// 协议类型枚举
typedef enum {
  PROTOCOL_RTSP = 0,
  PROTOCOL_FTP,
  PROTOCOL_HTTP
} protocol_type_t;

// 协议上下文结构（tagged union）
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

// 验证记录结构
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

// 核心验证接口
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

// 协议级 validator
int validate_rtsp_request_message(const char *message, protocol_context_t *ctx);
int validate_ftp_request_message(const char *message, protocol_context_t *ctx);
int validate_http_request_message(const char *message, protocol_context_t *ctx);

// 日志接口
void log_llm_validation_record(const llm_validation_record_t *record);
void init_validation_log(const char *out_dir);
void close_validation_log();

#endif // LLN_VALIDATOR_H
```

- [ ] **Step 2: 验证头文件语法**

Run: `gcc -fsyntax-only -I. ChatAFL/llm-validator.h`
Expected: 无错误输出

- [ ] **Step 3: Commit**

```bash
git add ChatAFL/llm-validator.h
git commit -m "feat: create llm-validator.h header file"
```

---

## Task 2: 创建 Validator 框架实现文件

**Files:**
- Create: `ChatAFL/llm-validator.c`

- [ ] **Step 1: 创建实现文件框架**

```c
// ChatAFL/llm-validator.c

#include "llm-validator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
void close_validation_log() {
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
    u32 region_count = 0;

    if (strcmp(protocol, "RTSP") == 0) {
        regions = extract_requests_rtsp(seq, strlen(seq), &region_count);
    } else if (strcmp(protocol, "FTP") == 0) {
        regions = extract_requests_ftp(seq, strlen(seq), &region_count);
    } else if (strcmp(protocol, "HTTP") == 0) {
        regions = extract_requests_http(seq, strlen(seq), &region_count);
    }

    if (!regions || region_count == 0) return LLM_VALID_FORMAT_FAIL;

    // 逐条验证
    for (u32 i = 0; i < region_count; i++) {
        char *msg = malloc(regions[i].endByte - regions[i].startByte + 1);
        if (!msg) continue;

        memcpy(msg, seq + regions[i].startByte, regions[i].endByte - regions[i].startByte);
        msg[regions[i].endByte - regions[i].startByte] = '\0';

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
```

- [ ] **Step 2: 验证实现文件语法**

Run: `gcc -c -I. ChatAFL/llm-validator.c -o /dev/null`
Expected: 无错误输出

- [ ] **Step 3: Commit**

```bash
git add ChatAFL/llm-validator.c
git commit -m "feat: create llm-validator.c implementation"
```

---

## Task 3: 实现 RTSP Validator

**Files:**
- Modify: `ChatAFL/llm-validator.c`

- [ ] **Step 1: 添加 RTSP 方法集合和验证函数**

在 `ChatAFL/llm-validator.c` 中添加：

```c
// 合法 RTSP 方法集合
static const char *rtsp_methods[] = {
    "OPTIONS", "DESCRIBE", "SETUP", "PLAY", "PAUSE",
    "TEARDOWN", "ANNOUNCE", "RECORD", "GET_PARAMETER",
    "SET_PARAMETER", "REDIRECT", NULL
};

int validate_rtsp_request_message(const char *message, protocol_context_t *ctx) {
    if (!message || !ctx) return 0;

    // 1. 必须以 \r\n\r\n 结束
    if (!strstr(message, "\r\n\r\n")) return 0;

    // 2. 解析请求行
    char method[64] = {0};
    char uri[1024] = {0};
    char version[32] = {0};

    if (sscanf(message, "%63s %1023s %31s", method, uri, version) != 3) return 0;

    // 3. 检查方法是否在合法集合中
    int valid_method = 0;
    for (int i = 0; rtsp_methods[i]; i++) {
        if (strcmp(method, rtsp_methods[i]) == 0) {
            valid_method = 1;
            break;
        }
    }
    if (!valid_method) return 0;

    // 4. 检查必需头字段
    if (!strstr(message, "CSeq:")) return 0;

    // SETUP 必须有 Transport
    if (strcmp(method, "SETUP") == 0 && !strstr(message, "Transport:")) return 0;

    // PLAY/PAUSE/TEARDOWN 必须有 Session
    if ((strcmp(method, "PLAY") == 0 || strcmp(method, "PAUSE") == 0 || strcmp(method, "TEARDOWN") == 0) &&
        !strstr(message, "Session:")) return 0;

    // 5. 更新上下文
    ctx->type = PROTOCOL_RTSP;

    // 提取 CSeq
    const char *cseq_start = strstr(message, "CSeq:");
    if (cseq_start) {
        sscanf(cseq_start, "CSeq: %u", &ctx->ctx.rtsp.last_cseq);
    }

    // 检查 Session
    if (strstr(message, "Session:")) {
        ctx->ctx.rtsp.has_session = 1;
    }

    // 检查 Transport
    if (strstr(message, "Transport:")) {
        ctx->ctx.rtsp.has_transport = 1;
    }

    return 1;
}
```

- [ ] **Step 2: 验证实现**

Run: `gcc -c -I. ChatAFL/llm-validator.c -o /dev/null`
Expected: 无错误输出

- [ ] **Step 3: Commit**

```bash
git add ChatAFL/llm-validator.c
git commit -m "feat: implement RTSP validator"
```

---

## Task 4: 实现 FTP Validator

**Files:**
- Modify: `ChatAFL/llm-validator.c`

- [ ] **Step 1: 添加 FTP 命令集合和验证函数**

在 `ChatAFL/llm-validator.c` 中添加：

```c
// 合法 FTP 命令集合
static const char *ftp_commands[] = {
    "USER", "PASS", "PWD", "CWD", "CDUP", "LIST", "NLST",
    "RETR", "STOR", "APPE", "DELE", "RNFR", "RNTO", "MKD",
    "RMD", "SITE", "SYST", "STAT", "HELP", "NOOP", "QUIT",
    "PASV", "PORT", "TYPE", "MODE", "STRU", "REST", NULL
};

int validate_ftp_request_message(const char *message, protocol_context_t *ctx) {
    if (!message || !ctx) return 0;

    // 1. 每行必须以 \r\n 结束
    const char *line_end = strstr(message, "\r\n");
    if (!line_end) return 0;

    // 2. 解析命令
    char command[16] = {0};
    if (sscanf(message, "%15s", command) != 1) return 0;

    // 3. 检查命令是否在合法集合中
    int valid_command = 0;
    for (int i = 0; ftp_commands[i]; i++) {
        if (strcmp(command, ftp_commands[i]) == 0) {
            valid_command = 1;
            break;
        }
    }
    if (!valid_command) return 0;

    // 4. 检查会话依赖
    ctx->type = PROTOCOL_FTP;

    // PASS 不能先于 USER
    if (strcmp(command, "PASS") == 0 && !ctx->ctx.ftp.has_user) return 0;

    // 未认证前不能 RETR/STOR/LIST
    if ((strcmp(command, "RETR") == 0 || strcmp(command, "STOR") == 0 || strcmp(command, "LIST") == 0) &&
        !ctx->ctx.ftp.is_authed) return 0;

    // 5. 更新上下文
    if (strcmp(command, "USER") == 0) {
        ctx->ctx.ftp.has_user = 1;
    } else if (strcmp(command, "PASS") == 0) {
        ctx->ctx.ftp.has_pass = 1;
        if (ctx->ctx.ftp.has_user) {
            ctx->ctx.ftp.is_authed = 1;
        }
    }

    return 1;
}
```

- [ ] **Step 2: 验证实现**

Run: `gcc -c -I. ChatAFL/llm-validator.c -o /dev/null`
Expected: 无错误输出

- [ ] **Step 3: Commit**

```bash
git add ChatAFL/llm-validator.c
git commit -m "feat: implement FTP validator"
```

---

## Task 5: 实现 HTTP Validator

**Files:**
- Modify: `ChatAFL/llm-validator.c`

- [ ] **Step 1: 添加 HTTP 方法集合和验证函数**

在 `ChatAFL/llm-validator.c` 中添加：

```c
// 合法 HTTP 方法集合
static const char *http_methods[] = {
    "GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS",
    "PATCH", "TRACE", "CONNECT", NULL
};

int validate_http_request_message(const char *message, protocol_context_t *ctx) {
    if (!message || !ctx) return 0;

    // 1. 解析请求行
    char method[16] = {0};
    char uri[1024] = {0};
    char version[32] = {0};

    if (sscanf(message, "%15s %1023s %31s", method, uri, version) != 3) return 0;

    // 2. 检查方法是否在合法集合中
    int valid_method = 0;
    for (int i = 0; http_methods[i]; i++) {
        if (strcmp(method, http_methods[i]) == 0) {
            valid_method = 1;
            break;
        }
    }
    if (!valid_method) return 0;

    // 3. 检查 header 格式
    const char *header_start = strstr(message, "\r\n");
    if (!header_start) return 0;

    // 4. 检查 Content-Length 与 body 一致性
    ctx->type = PROTOCOL_HTTP;

    const char *content_length_start = strstr(message, "Content-Length:");
    if (content_length_start) {
        ctx->ctx.http.has_content_length = 1;
    }

    // 5. 检查 Host 头
    if (strstr(message, "Host:")) {
        ctx->ctx.http.has_host = 1;
    }

    return 1;
}
```

- [ ] **Step 2: 验证实现**

Run: `gcc -c -I. ChatAFL/llm-validator.c -o /dev/null`
Expected: 无错误输出

- [ ] **Step 3: Commit**

```bash
git add ChatAFL/llm-validator.c
git commit -m "feat: implement HTTP validator"
```

---

## Task 6: 创建单元测试

**Files:**
- Create: `ChatAFL/test_llm_validator.c`

- [ ] **Step 1: 创建测试文件**

```c
// ChatAFL/test_llm_validator.c

#include "llm-validator.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void test_rtsp_validator() {
    printf("Testing RTSP validator...\n");

    protocol_context_t ctx = {0};

    // 测试合法 PLAY 请求
    const char *valid_play = "PLAY rtsp://example.com/stream RTSP/1.0\r\nCSeq: 4\r\nSession: 12345\r\n\r\n";
    assert(validate_rtsp_request_message(valid_play, &ctx) == 1);
    assert(ctx.type == PROTOCOL_RTSP);
    assert(ctx.ctx.rtsp.has_session == 1);

    // 测试非法 SETUP 请求（缺少 Transport）
    const char *invalid_setup = "SETUP rtsp://example.com/stream RTSP/1.0\r\nCSeq: 5\r\n\r\n";
    ctx = (protocol_context_t){0};
    assert(validate_rtsp_request_message(invalid_setup, &ctx) == 0);

    // 测试非法方法
    const char *invalid_method = "INVALID rtsp://example.com/stream RTSP/1.0\r\nCSeq: 6\r\n\r\n";
    ctx = (protocol_context_t){0};
    assert(validate_rtsp_request_message(invalid_method, &ctx) == 0);

    printf("RTSP validator tests passed!\n");
}

void test_ftp_validator() {
    printf("Testing FTP validator...\n");

    protocol_context_t ctx = {0};

    // 测试 USER 命令
    const char *user_cmd = "USER anonymous\r\n";
    assert(validate_ftp_request_message(user_cmd, &ctx) == 1);
    assert(ctx.ctx.ftp.has_user == 1);

    // 测试 PASS 命令
    const char *pass_cmd = "PASS guest@\r\n";
    assert(validate_ftp_request_message(pass_cmd, &ctx) == 1);
    assert(ctx.ctx.ftp.has_pass == 1);
    assert(ctx.ctx.ftp.is_authed == 1);

    // 测试非法命令顺序（PASS 先于 USER）
    ctx = (protocol_context_t){0};
    const char *invalid_pass = "PASS guest@\r\n";
    assert(validate_ftp_request_message(invalid_pass, &ctx) == 0);

    printf("FTP validator tests passed!\n");
}

void test_http_validator() {
    printf("Testing HTTP validator...\n");

    protocol_context_t ctx = {0};

    // 测试合法 GET 请求
    const char *valid_get = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n";
    assert(validate_http_request_message(valid_get, &ctx) == 1);
    assert(ctx.type == PROTOCOL_HTTP);
    assert(ctx.ctx.http.has_host == 1);

    // 测试非法方法
    const char *invalid_method = "INVALID /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n";
    ctx = (protocol_context_t){0};
    assert(validate_http_request_message(invalid_method, &ctx) == 0);

    printf("HTTP validator tests passed!\n");
}

void test_llm_message_validation() {
    printf("Testing LLM message validation...\n");

    protocol_context_t ctx = {0};
    ctx.type = PROTOCOL_RTSP;

    // 测试 RTSP 消息验证
    const char *valid_msg = "PLAY rtsp://example.com/stream RTSP/1.0\r\nCSeq: 4\r\nSession: 12345\r\n\r\n";
    assert(validate_llm_message("RTSP", LLM_STAGE_STALL, valid_msg, &ctx) == LLM_VALID_OK);

    // 测试空消息
    assert(validate_llm_message("RTSP", LLM_STAGE_STALL, "", &ctx) == LLM_VALID_FORMAT_FAIL);

    // 测试不支持的协议
    assert(validate_llm_message("UNSUPPORTED", LLM_STAGE_STALL, valid_msg, &ctx) == LLM_VALID_GRAMMAR_FAIL);

    printf("LLM message validation tests passed!\n");
}

int main() {
    test_rtsp_validator();
    test_ftp_validator();
    test_http_validator();
    test_llm_message_validation();

    printf("\nAll tests passed!\n");
    return 0;
}
```

- [ ] **Step 2: 编译并运行测试**

Run: `gcc -I. ChatAFL/test_llm_validator.c ChatAFL/llm-validator.c ChatAFL/aflnet.c -o ChatAFL/test_llm_validator -lpcre2-8 && ./ChatAFL/test_llm_validator`
Expected: 所有测试通过

- [ ] **Step 3: Commit**

```bash
git add ChatAFL/test_llm_validator.c
git commit -m "test: add llm-validator unit tests"
```

---

## Task 7: 更新 Makefile

**Files:**
- Modify: `ChatAFL/Makefile`

- [ ] **Step 1: 添加 llm-validator.o 到构建**

在 `ChatAFL/Makefile` 中找到 `afl-fuzz` 目标，添加 `llm-validator.o`：

```makefile
afl-fuzz: afl-fuzz.c aflnet.o chat-llm.o llm-validator.o | test-instr
	$(CC) $(CFLAGS) $(CFLAGS_FLTO) $(LDFLAGS) afl-fuzz.c aflnet.o chat-llm.o llm-validator.o -o $@ $(LIBS) -lcurl -ljson-c -lpcre2-8
```

添加 `llm-validator.o` 的编译规则：

```makefile
llm-validator.o: llm-validator.c llm-validator.h
	$(CC) $(CFLAGS) -c llm-validator.c
```

- [ ] **Step 2: 验证构建**

Run: `cd ChatAFL && make clean && make afl-fuzz`
Expected: 构建成功

- [ ] **Step 3: Commit**

```bash
git add ChatAFL/Makefile
git commit -m "build: add llm-validator.o to Makefile"
```

---

## Task 8: 集成 Validator 到 Stall Breaking 路径

**Files:**
- Modify: `ChatAFL/afl-fuzz.c:6961-7005`

- [ ] **Step 1: 添加环境变量读取**

在 `ChatAFL/afl-fuzz.c` 的全局变量区域添加：

```c
// LLM 验证开关
u8 afl_llm_validation = 0;
u8 afl_llm_validation_strict = 0;
u8 afl_llm_validation_permissive = 0;
```

在 `main()` 函数中读取环境变量：

```c
// 读取 LLM 验证开关
if (getenv("AFL_LLM_VALIDATION")) {
    afl_llm_validation = atoi(getenv("AFL_LLM_VALIDATION"));
}
if (getenv("AFL_LLM_VALIDATION_STRICT")) {
    afl_llm_validation_strict = atoi(getenv("AFL_LLM_VALIDATION_STRICT"));
}
if (getenv("AFL_LLM_VALIDATION_PERMISSIVE")) {
    afl_llm_validation_permissive = atoi(getenv("AFL_LLM_VALIDATION_PERMISSIVE"));
}
```

- [ ] **Step 2: 集成到 Stall Breaking 路径**

在 `ChatAFL/afl-fuzz.c` 的 stall breaking 路径（约 6961-7005 行）添加验证逻辑：

```c
// 提取消息
stall_message = extract_stalled_message(response);
format_request_message(stall_message);

// 验证 LLM 输出
if (afl_llm_validation) {
    llm_validation_record_t record = {0};
    record.stage = LLM_STAGE_STALL;

    protocol_context_t ctx = {0};
    // 根据协议名称设置类型
    if (strcmp(protocol_name, "RTSP") == 0) {
        ctx.type = PROTOCOL_RTSP;
    } else if (strcmp(protocol_name, "FTP") == 0) {
        ctx.type = PROTOCOL_FTP;
    } else if (strcmp(protocol_name, "HTTP") == 0) {
        ctx.type = PROTOCOL_HTTP;
    }

    record.result = validate_llm_message(protocol_name, LLM_STAGE_STALL,
                                         stall_message, &ctx);

    if (record.result != LLM_VALID_OK) {
        snprintf(record.reason, sizeof(record.reason),
                 "Validation failed: %d", record.result);
        log_llm_validation_record(&record);

        // 宽松模式：记录但不拒绝
        if (!afl_llm_validation_permissive) {
            free(stall_message);
            goto free_stall;
        }
    }
}

// 继续原有逻辑
common_fuzz_stuff(argv, stall_message, len);
```

- [ ] **Step 3: 验证构建**

Run: `cd ChatAFL && make clean && make afl-fuzz`
Expected: 构建成功

- [ ] **Step 4: Commit**

```bash
git add ChatAFL/afl-fuzz.c
git commit -m "feat: integrate validator into stall breaking path"
```

---

## Task 9: 集成 Validator 到 Seed Enrichment 路径

**Files:**
- Modify: `ChatAFL/afl-fuzz.c:2736-2767`

- [ ] **Step 1: 集成到 Seed Enrichment 路径**

在 `ChatAFL/afl-fuzz.c` 的 seed enrichment 路径（约 2736-2767 行）添加验证逻辑：

```c
// 生成 enriched 序列
enriched_seq = enrich_sequence(...);
format_request_message(enriched_seq);

// 验证 LLM 输出
if (afl_llm_validation) {
    llm_validation_record_t record = {0};
    record.stage = LLM_STAGE_ENRICHMENT;

    protocol_context_t ctx = {0};
    if (strcmp(protocol_name, "RTSP") == 0) {
        ctx.type = PROTOCOL_RTSP;
    } else if (strcmp(protocol_name, "FTP") == 0) {
        ctx.type = PROTOCOL_FTP;
    } else if (strcmp(protocol_name, "HTTP") == 0) {
        ctx.type = PROTOCOL_HTTP;
    }

    record.result = validate_llm_sequence(protocol_name, LLM_STAGE_ENRICHMENT,
                                          enriched_seq, &ctx);

    if (record.result != LLM_VALID_OK) {
        snprintf(record.reason, sizeof(record.reason),
                 "Enrichment validation failed: %d", record.result);
        log_llm_validation_record(&record);

        // 宽松模式：记录但不拒绝
        if (!afl_llm_validation_permissive) {
            free(enriched_seq);
            continue;
        }
    }
}

// 写入新 seed
write_new_seeds(enriched_seq);
```

- [ ] **Step 2: 验证构建**

Run: `cd ChatAFL && make clean && make afl-fuzz`
Expected: 构建成功

- [ ] **Step 3: Commit**

```bash
git add ChatAFL/afl-fuzz.c
git commit -m "feat: integrate validator into seed enrichment path"
```

---

## Task 10: 集成 Validator 到 Grammar Extraction 路径

**Files:**
- Modify: `ChatAFL/afl-fuzz.c:434-548`

- [ ] **Step 1: 添加 Grammar 验证函数**

在 `ChatAFL/llm-validator.c` 中添加：

```c
// 验证 grammar pattern
int validate_grammar_pattern(const char *pattern, const char *protocol) {
    if (!pattern || !protocol) return 0;

    // 1. 检查消息类型是否合法
    if (strcmp(protocol, "RTSP") == 0) {
        for (int i = 0; rtsp_methods[i]; i++) {
            if (strstr(pattern, rtsp_methods[i])) return 1;
        }
    } else if (strcmp(protocol, "FTP") == 0) {
        for (int i = 0; ftp_commands[i]; i++) {
            if (strstr(pattern, ftp_commands[i])) return 1;
        }
    } else if (strcmp(protocol, "HTTP") == 0) {
        for (int i = 0; http_methods[i]; i++) {
            if (strstr(pattern, http_methods[i])) return 1;
        }
    }

    return 0;
}
```

- [ ] **Step 2: 集成到 Grammar Extraction 路径**

在 `ChatAFL/afl-fuzz.c` 的 grammar extraction 路径（约 434-548 行）添加验证逻辑：

```c
// 提取 pattern
pattern = extract_message_pattern(grammar);

// 验证 grammar pattern
if (afl_llm_validation) {
    if (!validate_grammar_pattern(pattern, protocol_name)) {
        log_grammar_validation_failure(pattern);

        // 宽松模式：记录但不拒绝
        if (!afl_llm_validation_permissive) {
            free(pattern);
            continue;
        }
    }
}

// 添加到 patterns
kl_push(rang, protocol_patterns, pattern);
```

- [ ] **Step 3: 验证构建**

Run: `cd ChatAFL && make clean && make afl-fuzz`
Expected: 构建成功

- [ ] **Step 4: Commit**

```bash
git add ChatAFL/afl-fuzz.c
git commit -m "feat: integrate validator into grammar extraction path"
```

---

## Task 11: 创建集成测试

**Files:**
- Create: `ChatAFL/test_llm_integration.c`

- [ ] **Step 1: 创建集成测试文件**

```c
// ChatAFL/test_llm_integration.c

#include "llm-validator.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void test_validation_with_context() {
    printf("Testing validation with context...\n");

    // 测试 RTSP 序列验证
    protocol_context_t ctx = {0};
    ctx.type = PROTOCOL_RTSP;

    const char *rtsp_seq = 
        "OPTIONS rtsp://example.com RTSP/1.0\r\nCSeq: 1\r\n\r\n"
        "DESCRIBE rtsp://example.com/stream RTSP/1.0\r\nCSeq: 2\r\nAccept: application/sdp\r\n\r\n"
        "SETUP rtsp://example.com/stream/track1 RTSP/1.0\r\nCSeq: 3\r\nTransport: RTP/AVP;unicast;client_port=3000-3001\r\n\r\n"
        "PLAY rtsp://example.com/stream RTSP/1.0\r\nCSeq: 4\r\nSession: 12345\r\n\r\n";

    llm_validation_result_t result = validate_llm_sequence("RTSP", LLM_STAGE_ENRICHMENT, rtsp_seq, &ctx);
    assert(result == LLM_VALID_OK);

    // 测试 FTP 序列验证
    ctx = (protocol_context_t){0};
    ctx.type = PROTOCOL_FTP;

    const char *ftp_seq = 
        "USER anonymous\r\n"
        "PASS guest@\r\n"
        "LIST\r\n";

    result = validate_llm_sequence("FTP", LLM_STAGE_ENRICHMENT, ftp_seq, &ctx);
    assert(result == LLM_VALID_OK);

    // 测试非法 FTP 序列（PASS 先于 USER）
    ctx = (protocol_context_t){0};
    ctx.type = PROTOCOL_FTP;

    const char *invalid_ftp_seq = 
        "PASS guest@\r\n"
        "USER anonymous\r\n";

    result = validate_llm_sequence("FTP", LLM_STAGE_ENRICHMENT, invalid_ftp_seq, &ctx);
    assert(result != LLM_VALID_OK);

    printf("Validation with context tests passed!\n");
}

void test_normalization() {
    printf("Testing normalization...\n");

    // 测试规范化
    const char *raw = "PLAY rtsp://example.com RTSP/1.0\r\nCSeq: 4\r\nSession: 12345\r\n\r\n";
    char *normalized = NULL;

    int ret = llm_normalize_candidate(raw, &normalized);
    assert(ret == 0);
    assert(normalized != NULL);
    assert(strcmp(raw, normalized) == 0);

    free(normalized);

    printf("Normalization tests passed!\n");
}

int main() {
    test_validation_with_context();
    test_normalization();

    printf("\nAll integration tests passed!\n");
    return 0;
}
```

- [ ] **Step 2: 编译并运行测试**

Run: `gcc -I. ChatAFL/test_llm_integration.c ChatAFL/llm-validator.c ChatAFL/aflnet.c -o ChatAFL/test_llm_integration -lpcre2-8 && ./ChatAFL/test_llm_integration`
Expected: 所有测试通过

- [ ] **Step 3: Commit**

```bash
git add ChatAFL/test_llm_integration.c
git commit -m "test: add llm-validator integration tests"
```

---

## Task 12: 初始化验证日志

**Files:**
- Modify: `ChatAFL/afl-fuzz.c`

- [ ] **Step 1: 在 main() 中初始化验证日志**

在 `ChatAFL/afl-fuzz.c` 的 `main()` 函数中，初始化验证日志：

```c
// 初始化验证日志
if (afl_llm_validation) {
    init_validation_log(out_dir);
}
```

在程序退出时关闭日志：

```c
// 关闭验证日志
if (afl_llm_validation) {
    close_validation_log();
}
```

- [ ] **Step 2: 验证构建**

Run: `cd ChatAFL && make clean && make afl-fuzz`
Expected: 构建成功

- [ ] **Step 3: Commit**

```bash
git add ChatAFL/afl-fuzz.c
git commit -m "feat: initialize and close validation log"
```

---

## Task 13: 端到端测试

**Files:**
- None（使用现有测试基础设施）

- [ ] **Step 1: 运行现有测试**

Run: `cd ChatAFL && make test`
Expected: 所有现有测试通过

- [ ] **Step 2: 运行 validator 测试**

Run: `./ChatAFL/test_llm_validator && ./ChatAFL/test_llm_integration`
Expected: 所有测试通过

- [ ] **Step 3: 测试环境变量开关**

Run: `AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=1 AFL_LLM_VALIDATION_PERMISSIVE=0 ./ChatAFL/afl-fuzz --help`
Expected: 程序正常启动，无崩溃

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "test: verify end-to-end functionality"
```

---

## Task 14: 最终验证

**Files:**
- None

- [ ] **Step 1: 完整构建测试**

Run: `cd ChatAFL && make clean && make`
Expected: 所有目标构建成功

- [ ] **Step 2: 运行所有测试**

Run: `./ChatAFL/test_llm_validator && ./ChatAFL/test_llm_integration`
Expected: 所有测试通过

- [ ] **Step 3: 验证消融实验开关**

Run: `AFL_LLM_VALIDATION=0 ./ChatAFL/afl-fuzz --help && AFL_LLM_VALIDATION=1 ./ChatAFL/afl-fuzz --help`
Expected: 两种模式都能正常启动

- [ ] **Step 4: Final Commit**

```bash
git add -A
git commit -m "feat: complete validation-driven LLM fuzzing implementation"
```

---

## 审计意见采纳总结

### 已采纳的修改

1. ✅ **代码同步**：从 benchmark 移植 RTSP validator
2. ✅ **接口参数**：添加 `protocol_context_t *ctx` 参数
3. ✅ **规范化函数**：`llm_normalize_candidate()` 作为封装
4. ✅ **protocol_context_t 重新设计**：使用 tagged union
5. ✅ **重试机制限制**：不添加额外重试
6. ✅ **区分 NO_GAIN**：作为执行后分类
7. ✅ **添加宽松模式**：`AFL_LLM_VALIDATION_PERMISSIVE=1`
8. ✅ **更新 Makefile**：添加 llm-validator.o

### 未采纳的修改

- 5 层消融：用户选择 3 层消融
- dry-run 验证：推迟到后续阶段
- 通用 key-value store：使用 tagged union 更清晰

---

## 实现顺序

### 第一阶段：框架和 RTSP validator（Task 1-6）
- 创建 validator 框架
- 实现 RTSP validator
- 添加单元测试

### 第二阶段：扩展协议（Task 4-5）
- 实现 FTP validator
- 实现 HTTP validator

### 第三阶段：集成和测试（Task 7-14）
- 更新 Makefile
- 集成到三条 LLM 路径
- 添加集成测试
- 端到端测试

---

## 消融实验配置

### Baseline
```bash
AFL_LLM_VALIDATION=0 ./afl-fuzz -i seeds -o out_baseline -N rtsp://...
```

### +FORMAT
```bash
AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=0 ./afl-fuzz -i seeds -o out_format -N rtsp://...
```

### +FULL
```bash
AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=1 ./afl-fuzz -i seeds -o out_full -N rtsp://...
```

---

**计划状态**：已完成，等待执行
