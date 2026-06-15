# Validation-Driven LLM Fuzzing 设计文档

> **创建日期**: 2026-05-06
> **状态**: 已审计，已修改
> **审计意见来源**: superpowers:code-reviewer

---

## 1. 概述

### 1.1 目标

将 ChatAFL 从"prompt-driven"LLM fuzzing 改造为"validation-driven"LLM fuzzing，通过统一的验证框架控制 LLM 输出质量。

### 1.2 核心贡献

1. **统一验证框架**：贯穿 grammar/enrichment/stall 三条路径的统一 Validator
2. **分层失败归因**：FORMAT_FAIL → GRAMMAR_FAIL → CONTEXT_FAIL
3. **收益验证**：有效接受率（effective acceptance rate）而非简单接受率
4. **消融实验**：3 层消融证明每层验证的价值

---

## 2. 模块结构

### 2.1 文件结构

```
ChatAFL/
├── llm-validator.h      # 新增：Validator 框架头文件
├── llm-validator.c      # 新增：Validator 框架实现
├── chat-llm.h           # 修改：添加 validator 集成声明
├── chat-llm.c           # 修改：集成 validator 调用
├── afl-fuzz.c           # 修改：添加环境变量开关、集成 validator
├── aflnet.c             # 不修改：复用现有 extract_requests_*
├── Makefile             # 修改：添加 llm-validator.o
└── test/
    ├── test_llm_validator.c  # 新增：单元测试
    └── test_llm_integration.c # 新增：集成测试
```

### 2.2 代码同步策略

**决定**：从 benchmark 移植 RTSP validator 作为基础，然后扩展 FTP/HTTP。

**原因**：
- benchmark 中已有经过测试的 RTSP validator（`benchmark/subjects/RTSP/Live555/chatafl/chat-llm.c:491-599`
- 减少重复工作，降低风险
- 保持代码一致性

**移植内容**：
- `validate_rtsp_request_message()` 函数
- `sanitize_prompt_context()` 函数（可选）
- 相关的常量定义（RTSP 方法集合等）

---

## 3. 核心数据结构

### 3.1 验证结果枚举

```c
// llm-validator.h

typedef enum {
  LLM_VALID_OK = 0,
  LLM_VALID_FORMAT_FAIL,    // 格式错误（预执行）
  LLM_VALID_GRAMMAR_FAIL,   // 语法错误（预执行）
  LLM_VALID_CONTEXT_FAIL,   // 上下文错误（预执行）
  LLM_VALID_NO_GAIN         // 无收益（执行后分类）
} llm_validation_result_t;
```

**审计修改**：移除 `LLM_VALID_STATE_FAIL`，因为它是执行后分类，不是预执行验证。

### 3.2 验证阶段枚举

```c
typedef enum {
  LLM_STAGE_GRAMMAR = 0,
  LLM_STAGE_ENRICHMENT,
  LLM_STAGE_STALL
} llm_generation_stage_t;
```

### 3.3 验证记录结构

```c
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
```

### 3.4 协议上下文结构（审计修改）

**原设计问题**：`protocol_context_t` 混合了 FTP/RTSP 字段。

**修改方案**：使用 tagged union 设计。

```c
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
```

**优点**：
- 类型安全，不会混用字段
- 内存高效，只占用所需协议的空间
- 易于扩展新协议

---

## 4. 接口设计

### 4.1 核心验证接口（审计修改）

```c
// 核心验证接口（添加 protocol_context_t *ctx 参数）
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
```

**审计修改**：明确 `llm_normalize_candidate()` 的定位。

**定位**：`llm_normalize_candidate()` 是对现有函数的封装，提供统一的规范化流程：
1. 调用 `clean_llm_response()` 进行基础清洗
2. 调用 `extract_stalled_message()` 提取消息
3. 调用 `format_request_message()` 格式化

**不替换**现有函数，而是提供统一入口。

### 4.2 协议级 Validator

```c
int validate_rtsp_request_message(const char *message, protocol_context_t *ctx);
int validate_ftp_request_message(const char *message, protocol_context_t *ctx);
int validate_http_request_message(const char *message, protocol_context_t *ctx);
```

### 4.3 日志接口

```c
void log_llm_validation_record(const llm_validation_record_t *record);
void init_validation_log(const char *out_dir);
void close_validation_log();
```

### 4.4 环境变量开关

```c
// afl-fuzz.c 中读取
u8 afl_llm_validation = 0;          // AFL_LLM_VALIDATION=0/1
u8 afl_llm_validation_strict = 0;   // AFL_LLM_VALIDATION_STRICT=0/1
u8 afl_llm_validation_permissive = 0; // AFL_LLM_VALIDATION_PERMISSIVE=0/1
```

**审计修改**：添加宽松模式 `AFL_LLM_VALIDATION_PERMISSIVE=1`，用于测量误报率。

---

## 5. 协议 Validator 实现

### 5.1 RTSP Validator（从 benchmark 移植）

**来源**：`benchmark/subjects/RTSP/Live555/chatafl/chat-llm.c:491-584`

```c
// 合法 RTSP 方法集合
static const char *rtsp_methods[] = {
    "OPTIONS", "DESCRIBE", "SETUP", "PLAY", "PAUSE",
    "TEARDOWN", "ANNOUNCE", "RECORD", "GET_PARAMETER",
    "SET_PARAMETER", "REDIRECT", NULL
};

int validate_rtsp_request_message(const char *message, protocol_context_t *ctx) {
    // 1. 必须以 \r\n\r\n 结束
    // 2. 解析请求行：METHOD URI RTSP/1.0
    // 3. 检查方法是否在合法集合中
    // 4. 检查必需头字段：
    //    - 所有请求必须有 CSeq
    //    - SETUP 必须有 Transport
    //    - PLAY/PAUSE/TEARDOWN 必须有 Session
    // 5. 更新上下文（CSeq、Session、Transport）
    return 1; // 合法
}
```

### 5.2 FTP Validator（新增）

```c
// 合法 FTP 命令集合
static const char *ftp_commands[] = {
    "USER", "PASS", "PWD", "CWD", "CDUP", "LIST", "NLST",
    "RETR", "STOR", "APPE", "DELE", "RNFR", "RNTO", "MKD",
    "RMD", "SITE", "SYST", "STAT", "HELP", "NOOP", "QUIT",
    "PASV", "PORT", "TYPE", "MODE", "STRU", "REST", NULL
};

int validate_ftp_request_message(const char *message, protocol_context_t *ctx) {
    // 1. 每行必须以 \r\n 结束
    // 2. 解析命令：COMMAND [args]
    // 3. 检查命令是否在合法集合中
    // 4. 检查会话依赖：
    //    - PASS 不能先于 USER
    //    - 未认证前不能 RETR/STOR/LIST
    // 5. 更新上下文（has_user、has_pass、is_authed）
    return 1; // 合法
}
```

### 5.3 HTTP Validator（新增）

```c
// 合法 HTTP 方法集合
static const char *http_methods[] = {
    "GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS",
    "PATCH", "TRACE", "CONNECT", NULL
};

int validate_http_request_message(const char *message, protocol_context_t *ctx) {
    // 1. 解析请求行：METHOD URI HTTP/1.x
    // 2. 检查方法是否在合法集合中
    // 3. 检查 header 格式：Key: Value
    // 4. 检查 Content-Length 与 body 一致性
    // 5. 检查 header/body 用空行分隔
    return 1; // 合法
}
```

---

## 6. 集成点设计

### 6.1 Stall Breaking 集成（afl-fuzz.c:6961-7005）

```c
// 当前代码（无验证）：
// stall_message = extract_stalled_message(response);
// format_request_message(stall_message);
// common_fuzz_stuff(argv, stall_message, len);

// 修改后：
stall_message = extract_stalled_message(response);
format_request_message(stall_message);

if (afl_llm_validation) {
    llm_validation_record_t record = {0};
    record.stage = LLM_STAGE_STALL;

    protocol_context_t ctx = {0};
    ctx.type = protocol_type; // 设置协议类型

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

common_fuzz_stuff(argv, stall_message, len);
```

**审计修改**：
- 添加宽松模式支持
- 明确重试机制限制（不添加额外重试，使用现有 `STALL_RETRIES=2`）

### 6.2 Seed Enrichment 集成（afl-fuzz.c:2736-2767）

```c
// 当前代码：
// enriched_seq = enrich_sequence(...);
// format_request_message(enriched_seq);
// write_new_seeds(enriched_seq);

// 修改后：
enriched_seq = enrich_sequence(...);
format_request_message(enriched_seq);

if (afl_llm_validation) {
    llm_validation_record_t record = {0};
    record.stage = LLM_STAGE_ENRICHMENT;

    protocol_context_t ctx = {0};
    ctx.type = protocol_type;

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

write_new_seeds(enriched_seq);
```

### 6.3 Grammar Extraction 集成（afl-fuzz.c:434-548）

```c
// 当前代码：
// pattern = extract_message_pattern(grammar);
// kl_push(rang, protocol_patterns, pattern);

// 修改后：
pattern = extract_message_pattern(grammar);

if (afl_llm_validation) {
    // 1. 检查消息类型是否合法
    // 2. 检查必需字段是否存在
    // 3. 检查 seed 命中率（AFL_LLM_VALIDATION_STRICT=1 时）
    if (!validate_grammar_pattern(pattern)) {
        log_grammar_validation_failure(pattern);

        // 宽松模式：记录但不拒绝
        if (!afl_llm_validation_permissive) {
            free(pattern);
            continue;
        }
    }
}

kl_push(rang, protocol_patterns, pattern);
```

**审计修改**：seed-match gate 作为严格模式的可选增强。

---

## 7. 消融实验设计（审计修改）

### 7.1 三层消融配置

| 配置 | 环境变量 | 说明 |
|------|----------|------|
| Baseline | `AFL_LLM_VALIDATION=0` | 原始 ChatAFL，无验证 |
| +FORMAT | `AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=0` | 只开启格式验证 |
| +FULL | `AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=1` | 开启所有验证 |

**审计修改**：保持 3 层消融（用户选择），但论文中可以展示 5 层消融的详细分析。

### 7.2 实验指标（审计修改）

**机制指标**（论文重点）：
- **有效接受率**（effective acceptance rate）：LLM 输出中导致新覆盖/状态/转移的比例
- format / grammar / context failure breakdown
- grammar seed-match rate（严格模式）
- enriched seed validity rate

**效率指标**：
- new coverage per LLM call
- new transitions per LLM call
- time to first new state

**最终指标**：
- branch / line coverage
- unique states
- unique transitions
- unique crashes

### 7.3 统计严谨性（审计修改）

**运行次数**：每个配置运行 10 次

**随机种子策略**：使用固定种子集合，确保可复现

**显著性检验**：Mann-Whitney U test（非参数检验）

**报告方式**：
- 中位数 + 四分位距
- p-value < 0.05 为显著

### 7.4 实验脚本设计

```bash
#!/bin/bash
# benchmark/scripts/execution/run_ablation.sh

# 定义种子集合
SEEDS=(12345 23456 34567 45678 56789 67890 78901 89012 90123 10234)

# Baseline
for seed in "${SEEDS[@]}"; do
    AFL_LLM_VALIDATION=0 AFL_RANDOM_SEED=$seed \
        ./afl-fuzz -i seeds -o out_baseline_$seed -N rtsp://...
done

# +FORMAT
for seed in "${SEEDS[@]}"; do
    AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=0 AFL_RANDOM_SEED=$seed \
        ./afl-fuzz -i seeds -o out_format_$seed -N rtsp://...
done

# +FULL
for seed in "${SEEDS[@]}"; do
    AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=1 AFL_RANDOM_SEED=$seed \
        ./afl-fuzz -i seeds -o out_full_$seed -N rtsp://...
done
```

---

## 8. 风险分析与缓解

### 8.1 技术风险

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| Validator 逻辑错误 | 误判合法/非法消息 | 充分的单元测试，参考 benchmark 实现 |
| 性能影响 | Fuzzing 速度下降 | 默认关闭，按需开启 |
| 协议理解偏差 | Validator 规则不准确 | 参考 RFC 和现有 extract_requests_* 实现 |
| 集成点位置错误 | 改动影响原有逻辑 | 仔细对比 benchmark 和主代码差异 |

### 8.2 误报风险（审计新增）

**问题**：Validator 过于严格可能拒绝有价值的 LLM 输出。

**缓解措施**：
- 添加宽松模式 `AFL_LLM_VALIDATION_PERMISSIVE=1`
- 在宽松模式下记录拒绝但不实际拒绝
- 对比宽松模式和严格模式的结果，量化误报影响

### 8.3 重试机制风险（审计新增）

**问题**：LLM 调用重试 + 验证重试可能导致总重试次数过多。

**缓解措施**：
- 不添加额外重试机制
- 使用现有 `STALL_RETRIES=2` 限制
- 验证失败直接跳过，不重试

### 8.4 进度风险

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| FTP/HTTP validator 复杂 | 工期延长 | 先实现 RTSP，验证框架后再扩展 |
| 集成测试困难 | 无法验证效果 | 使用现有 test_rtsp_validation 作为参考 |
| 消融实验时间长 | 论文进度延迟 | 并行运行实验，优先关键指标 |

### 8.5 论文风险

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 创新性不足 | 被拒稿 | 强调统一框架 + 分层验证 + 收益归因 |
| 实验结果不显著 | 论文说服力弱 | 重点关注状态转移效率，而非覆盖率 |
| 与现有工作重复 | 被认为是工程改进 | 突出 validation-driven 范式变化 |

---

## 9. 论文定位（审计修改）

### 9.1 创新点定位

**主创新点**：
1. 从 prompt-driven 到 validation-driven 的范式变化
2. 统一验证框架（common data structures, common logging, common failure taxonomy）
3. 分层失败归因机制
4. 有效接受率（effective acceptance rate）指标

**与 MultiFuzz/StatePre 的定位**：
- MultiFuzz：RAG 增强生成，但输出仍不验证
- StatePre：状态注释，但不控制 LLM 输出质量
- 本工作：验证 LLM 输出，即使生成不完美也能保证质量

**可以结合**：本工作可以与 RAG 增强生成结合，获得更好效果。

### 9.2 有效接受率定义（审计新增）

**定义**：有效接受率 = LLM 输出中导致新覆盖/状态/转移的比例

**公式**：
```
effective_acceptance_rate = (outputs_with_new_benefit) / (total_llm_outputs)
```

**优势**：
- baseline 的有效接受率不是 100%（因为 LLM 输出可能无收益）
- 可以公平比较不同配置的效果
- 更能体现验证的价值

### 9.3 负面结果叙事（审计新增）

**建议**：如果实验显示格式验证提供大部分收益，而上下文验证收益有限，应如实报告。

**叙事方式**：
- "我们尝试了完整验证栈，但大部分收益来自基础格式+语法验证"
- 这种负面结果仍有发表价值
- 为未来工作提供方向

---

## 10. 实现顺序

### 10.1 第一阶段：框架和 RTSP validator

1. 创建 `llm-validator.h/c` 框架
2. 从 benchmark 移植 RTSP validator
3. 添加单元测试
4. 集成到 stall breaking 路径
5. 添加集成测试

### 10.2 第二阶段：扩展协议

6. 实现 FTP validator
7. 实现 HTTP validator
8. 集成到 enrichment 路径
9. 集成到 grammar 路径

### 10.3 第三阶段：实验和论文

10. 实现消融实验开关
11. 运行消融实验
12. 编写论文

---

## 11. Makefile 修改

```makefile
# ChatAFL/Makefile

# 原有：
# afl-fuzz: afl-fuzz.c aflnet.o chat-llm.o | test-instr
#     $(CC) $(CFLAGS) ... aflnet.o chat-llm.o ... -lcurl -ljson-c -lpcre2-8

# 修改为：
afl-fuzz: afl-fuzz.c aflnet.o chat-llm.o llm-validator.o | test-instr
    $(CC) $(CFLAGS) ... aflnet.o chat-llm.o llm-validator.o ... -lcurl -ljson-c -lpcre2-8

llm-validator.o: llm-validator.c llm-validator.h
    $(CC) $(CFLAGS) -c llm-validator.c
```

---

## 12. 测试策略

### 12.1 单元测试

- `test_llm_validator.c`：测试各协议 validator 的正确性
- 覆盖：合法消息、非法消息、边界情况

### 12.2 集成测试

- `test_llm_integration.c`：测试 validator 与 afl-fuzz 的集成
- 覆盖：stall breaking、enrichment、grammar extraction

### 12.3 回归测试

- 确保原有功能不受影响
- 运行现有测试用例

---

## 13. 审计意见采纳总结

### 必须修复（已采纳）

1. ✅ 代码同步：从 benchmark 移植 RTSP validator
2. ✅ 接口参数：添加 `protocol_context_t *ctx` 参数
3. ✅ 规范化函数：明确 `llm_normalize_candidate()` 是封装而非替换

### 应该修复（已采纳）

4. ✅ `protocol_context_t` 重新设计：使用 tagged union
5. ✅ 重试机制限制：不添加额外重试
6. ✅ 区分 `NO_GAIN`：移除 `LLM_VALID_STATE_FAIL`，保留 `LLM_VALID_NO_GAIN` 作为执行后分类
7. ✅ 添加宽松模式：`AFL_LLM_VALIDATION_PERMISSIVE=1`
8. ✅ 更新 Makefile

### 论文相关（已采纳）

9. ✅ 统计严谨性：10 次运行，Mann-Whitney U test
10. ✅ 保持 3 层消融（用户选择）
11. ✅ 加强定位对比：与 MultiFuzz/StatePre 的差异
12. ✅ 定义有效接受率

### 未采纳

- 5 层消融：用户选择 3 层消融
- dry-run 验证：推迟到后续阶段
- 通用 key-value store：使用 tagged union 更清晰

---

**文档状态**：已审计，已修改，等待用户批准
