# 基准 ChatAFL 与扩展版对比分析

> 本文档对比 `baseline/ChatAFL/`（论文原始代码）与 `ChatAFL/`（当前仓库扩展版）的差异，为消融实验设计和论文写作提供准确的代码事实依据。

---

## 一、代码规模对比

| 文件 | 基准版 | 扩展版 | 增量 |
|------|--------|--------|------|
| `afl-fuzz.c` | 10,933 行 | 11,594 行 | +661 行 |
| `chat-llm.c` | 1,182 行 | 1,647 行 | +465 行 |
| `chat-llm.h` | 94 行 | 119 行 | +25 行 |
| `llm-validator.c` | **不存在** | 613 行 | +613 行 |
| `llm-validator.h` | **不存在** | 145 行 | +145 行 |
| `config.h` | 369 行 | 369 行 | 仅改常量 |
| **总计** | ~12,578 行 | ~14,487 行 | +1,909 行 |

扩展版新增约 **1,909 行**代码，其中验证框架（`llm-validator.c/h`）占 758 行。

---

## 二、核心差异总览

| 维度 | 基准版 | 扩展版 |
|------|--------|--------|
| LLM 端点 | 硬编码 OpenAI API | 环境变量配置（`LLM_URL/TOKEN/MODEL`） |
| 输出清洗 | **无**（直接使用 LLM 原始输出） | 有（`clean_llm_response()`） |
| 验证框架 | **无** | 三级验证（DISABLED/FORMAT_ONLY/FULL） |
| 反馈重试 | **无** | 有（最多 `AFL_LLM_FEEDBACK_MAX_RETRIES` 次） |
| Post-gain 归因 | **无** | 有（`AFL_LLM_POST_GAIN`） |
| 消融变体 | 仅 CL1/CL2（注释代码） | V0/V1/V2/ChatAFL（环境变量控制） |
| 验证日志 | **无** | 有（`llm-validation/*.csv`） |
| Token 预算 | 较小（2048/400/1300） | 较大（8192/2000/4000） |
| Stall 阈值 | `CHATTING_THRESHOLD=64` | `CHATTING_THRESHOLD=512` |

---

## 三、常量差异详解

### 3.1 Token 预算常量

| 常量 | 基准版 | 扩展版 | 变化 |
|------|--------|--------|------|
| `MAX_TOKENS` | 2048 | 4096 | 2x |
| `MAX_PROMPT_LENGTH` | 2048 | 8192 | 4x |
| `EXAMPLES_PROMPT_LENGTH` | 400 | 2000 | 5x |
| `HISTORY_PROMPT_LENGTH` | 1300 | 4000 | 3x |
| `EXAMPLE_SEQUENCE_PROMPT_LENGTH` | 1700 | 4000 | 2.4x |

**影响**：扩展版的 LLM prompt 可以包含更长的通信历史和更多示例，但也会增加 API 调用成本和延迟。

### 3.2 Fuzzing 阈值常量

| 常量 | 基准版 | 扩展版 | 影响 |
|------|--------|--------|------|
| `CHATTING_THRESHOLD` | **64** | **512** | 扩展版允许 8 倍更多的 LLM stall 调用 |
| `UNINTERESTING_THRESHOLD` | 512 | 512 | 不变 |
| `EPSILON_CHOICE` | 0.5 | 0.5 | 不变 |

**关键发现**：基准版的 `CHATTING_THRESHOLD=64` 意味着整个 fuzzing 过程中**最多只调用 64 次 stall breaking LLM**。扩展版将其提高到 512，大幅增加了 LLM 参与 fuzzing 的程度。

### 3.3 LLM 重试常量

| 常量 | 基准版 | 扩展版 |
|------|--------|--------|
| `STALL_RETRIES` | 2 | 2 |
| `GRAMMAR_RETRIES` | 5 | 5 |
| `ENRICHMENT_RETRIES` | 5 | 5 |
| `LLM_FEEDBACK_MAX_RETRIES_DEFAULT` | **不存在** | 3 |

---

## 四、LLM 通信机制差异

### 4.1 API 端点

**基准版**（硬编码 OpenAI）：
```c
// chat-llm.c:51-58
if (strcmp(model, "instruct") == 0)
    url = "https://api.openai.com/v1/completions";
else
    url = "https://api.openai.com/v1/chat/completions";

char *auth_header = "Authorization: Bearer " OPENAI_TOKEN;
```

- 两个模型：`gpt-3.5-turbo-instruct`（completions API）和 `gpt-3.5-turbo`（chat completions API）
- Token 通过编译时宏 `OPENAI_TOKEN` 注入

**扩展版**（环境变量配置）：
```c
// chat-llm.c:105-140
char *url = getenv("LLM_URL");
char *token = getenv("LLM_TOKEN");
char *model_name = getenv("LLM_MODEL");
// 默认使用 MiniMax-compatible endpoint
```

- 统一 chat endpoint，不再区分 completions/chat completions
- 通过 `parse_or_create_messages()` 自动处理 prompt 格式
- 支持任意 OpenAI-compatible / MiniMax-compatible 服务

### 4.2 输出清洗

**基准版**：无清洗，LLM 输出直接使用。

```c
// 直接从 API 响应中提取 content
data = json_object_get_string(jobj5);
if (data[0] == '\n') data++;  // 仅跳过开头换行
answer = strdup(data);
```

**扩展版**：增加 `clean_llm_response()` 函数（chat-llm.c:258-359）。

```c
char *clean_llm_response(const char *raw_response) {
    // 1. 拒答关键词过滤（sorry, As an AI, policy, ...）
    // 2. JSON 提取（优先截取 {...} 或 [...]）
    // 3. Markdown 剥离（去掉 ``` 包裹）
    // 4. LF → CRLF 转换
    // 5. 首尾空白清理
}
```

**影响**：`clean_llm_response()` 是扩展版最重要的增量之一。它能在早期过滤掉大量明显无效的 LLM 输出，避免这些输出进入后续流程浪费时间。

---

## 五、处理链差异

### 5.1 Stall Breaking 处理链

**基准版**：
```
LLM 原始输出
    ↓
extract_stalled_message()  ← 跳过第一行（空行或说明文字）
    ↓
format_request_message()   ← 修复 CRLF + 追加 \r\n\r\n
    ↓
common_fuzz_stuff()        ← 直接执行
```

**扩展版**：
```
LLM 原始输出
    ↓
clean_llm_response()       ← 拒答过滤 + JSON提取 + Markdown剥离
    ↓
extract_stalled_message()  ← 跳过第一行
    ↓
format_request_message()   ← 修复 CRLF + 追加 \r\n\r\n
    ↓
validate_llm_message_with_mode()  ← 格式/语法/上下文验证
    ↓
    ├─ 通过 → common_fuzz_stuff() 执行
    └─ 失败 → llm_feedback_retry_stall() × 最多3次
                ↓
            ├─ 修复成功 → 执行
            └─ 修复失败 → 丢弃
```

### 5.2 Seed Enrichment 处理链

**基准版**：
```
LLM 原始输出
    ↓
unescape_string()          ← 反转义（\n → 真实换行）
    ↓
与原始种子比较             ← 相同则跳过
    ↓
format_request_message()   ← 修复 CRLF + 追加 \r\n\r\n
    ↓
write_new_seeds()          ← 写入 enriched_* 文件
```

**扩展版**：
```
LLM 原始输出
    ↓
clean_llm_response()       ← 拒答过滤 + JSON提取 + Markdown剥离
    ↓
enrich_sequence_with_prompt()  ← 构造 prompt（含格式约束）
    ↓
unescape_string() + format_request_message()
    ↓
与原始种子比较             ← 相同则跳过
    ↓
validate_llm_sequence_with_mode()  ← 格式/语法/上下文验证
    ↓
    ├─ 通过 → write_new_seeds()
    └─ 失败 → llm_feedback_retry_enrichment() × 最多3次
                ↓
            ├─ 修复成功 → 写入
            └─ 修复失败 → 丢弃（或 permissive 模式下记录但仍写入）
```

---

## 六、验证框架（扩展版独有）

### 6.1 三级验证模式

```c
// llm-validator.h:31-35
typedef enum {
    LLM_VALIDATE_DISABLED = 0,    // 跳过所有校验
    LLM_VALIDATE_FORMAT_ONLY,     // 仅检查结构格式
    LLM_VALIDATE_FULL             // 格式 + 语法 + 上下文
} llm_validation_mode_t;
```

### 6.2 四种验证结果

```c
// llm-validator.h:15-21
typedef enum {
    LLM_VALID_OK = 0,           // 通过
    LLM_VALID_FORMAT_FAIL,      // 缺少 CRLF、非打印字符、请求行格式错误
    LLM_VALID_GRAMMAR_FAIL,     // 无效方法/命令、缺少必需头字段
    LLM_VALID_CONTEXT_FAIL,     // 协议状态机违规（如 FTP 的 USER→PASS 顺序）
    LLM_VALID_NO_GAIN           // 有效但无新覆盖率（仅用于执行结果分类）
} llm_validation_result_t;
```

### 6.3 协议特定验证规则

| 协议 | FORMAT 检查 | GRAMMAR 检查 | CONTEXT 检查 |
|------|------------|-------------|-------------|
| RTSP | CRLF 终止符、可打印字符、请求行格式 | 方法有效、版本 RTSP/1.0、CSeq 必需 | PLAY/PAUSE/TEARDOWN 需要 Session |
| FTP | CRLF 行尾、可打印字符 | 命令有效 | PASS 需要 USER、RETR/STOR/LIST 需要认证 |
| HTTP | CRLF 终止符、可打印字符、请求行格式 | 方法有效、版本 HTTP/1.x、Content-Length 匹配 | Host 头检查 |

### 6.4 运行时控制

```c
// afl-fuzz.c:11137-11156
AFL_LLM_VALIDATION       → afl_llm_validation
AFL_LLM_VALIDATION_STRICT → afl_llm_validation_strict
AFL_LLM_POST_GAIN        → afl_llm_post_gain
AFL_LLM_FEEDBACK         → afl_llm_feedback
AFL_LLM_FEEDBACK_MAX_RETRIES → afl_llm_feedback_max_retries
AFL_LLM_SKIP_STARTUP     → afl_llm_skip_startup
```

---

## 七、反馈重试机制（扩展版独有）

### 7.1 Stall 反馈重试

```c
// chat-llm.c:1331-1384
char *llm_feedback_retry_stall(protocol, failed_message, error, mode, max_retries=3) {
    for (attempt = 0; attempt < max_retries; attempt++) {
        // 1. 构造反馈 prompt（包含失败消息 + 错误详情）
        // 2. 调用 LLM（temperature=0.7，单次尝试）
        // 3. 提取 + 格式化 + 验证
        // 4. 通过则返回修复后的消息
    }
    return NULL;  // 所有重试失败
}
```

### 7.2 Enrichment 反馈重试

```c
// chat-llm.c:1386-1440
char *llm_feedback_retry_enrichment(protocol, failed_message, error, mode, max_retries=3) {
    // 与 stall 反馈类似，但处理的是消息序列而非单条消息
}
```

### 7.3 反馈 Prompt 模板

```
Stall 反馈:
"The following <PROTOCOL> client request message was generated but FAILED validation:
 --- BEGIN FAILED MESSAGE --- <failed_message> --- END FAILED MESSAGE ---
 Validation error: <error_detail>
 Please generate a CORRECTED <PROTOCOL> client request..."

Enrichment 反馈:
"The following <PROTOCOL> message sequence was generated but FAILED validation:
 --- BEGIN FAILED SEQUENCE --- <failed_sequence> --- END FAILED SEQUENCE ---
 Validation error: <error_detail>
 Please generate a CORRECTED <PROTOCOL> message sequence..."
```

---

## 八、消融变体设计差异

### 8.1 基准版的变体

基准版有三个变体目录：`ChatAFL/`、`ChatAFL-CL1/`、`ChatAFL-CL2/`，差异通过**注释代码**实现：

| 变体 | 差异方式 |
|------|---------|
| ChatAFL | 完整版 |
| ChatAFL-CL1 | `enrich_testcases()` 被注释掉，`CHATTING_THRESHOLD=0` |
| ChatAFL-CL2 | `CHATTING_THRESHOLD=0` |

### 8.2 扩展版的变体

扩展版通过**环境变量**控制行为，代码相同：

| 变体 | `VALIDATION` | `STRICT` | `POST_GAIN` | `FEEDBACK` | 含义 |
|------|-------------|----------|-------------|------------|------|
| V0 | 0 | 0 | 0 | 0 | 无验证基线 |
| V1 | 1 | 0 | 0 | 自动启用 | 格式验证 + 反馈 |
| V2 | 1 | 1 | 0 | 自动启用 | 完整验证 + 反馈 |
| ChatAFL | 1 | 1 | 1 | 1 | 完整系统 |

**注意**：V1/V2 的 `env.sh` 没有显式设置 `AFL_LLM_FEEDBACK`，但 `afl-fuzz.c` 中有自动启用逻辑：

```c
if (getenv("AFL_LLM_FEEDBACK")) {
    afl_llm_feedback = env_flag_enabled("AFL_LLM_FEEDBACK");
} else if (afl_llm_validation) {
    afl_llm_feedback = 1;  // validation 开启时自动启用 feedback
}
```

---

## 九、Prompt 设计差异

### 9.1 Stall Prompt

**基准版**：
```c
// chat-llm.c:150-166
char *template = "In the %s protocol, the communication history between the %s client "
                 "and the %s server is as follows."
                 "The next proper client request that can affect the server's state are:"
                 "\\n\\nDesired format of real client requests:\\n%s"
                 "Communication History:\\n\\\"\\\"\\\"\\n%s\\\"\\\"\\\"";
// system: "You are a helpful assistant."
```

**扩展版**：
```c
// chat-llm.c:361-389
// system: "You are a network protocol expert assistant. Output ONLY the raw required protocol command."
// 增加了格式约束：
// "Output exactly ONE complete client request message.
//  MUST include headers and MUST end with \r\n\r\n.
//  NO markdown, NO formatting, NO explanations."
```

### 9.2 Grammar Prompt

**基准版**：
```c
// chat-llm.c:168-195
// system: "You are a helpful assistant."
// 无格式约束
```

**扩展版**：
```c
// chat-llm.c:391-432
// system: "You are a helpful assistant."
// 增加格式约束："Output ONLY strictly valid JSON. NO markdown, NO code blocks, NO explanations."
```

### 9.3 Enrichment Prompt

**基准版**：
```c
// chat-llm.c:916-973
// 纯文本 prompt，无格式约束
"The following is one sequence of client requests:\n%s\n
 Please add the %s client requests in the proper locations,
 and the modified sequence of client requests is:"
```

**扩展版**：
```c
// chat-llm.c:1173-1245
// 增加格式约束
"(System constraint: Output ONLY the raw protocol commands.
 NO markdown code blocks, NO explanations, NO intro text.
 ONLY output the raw TCP sequence.)"
```

---

## 十、Post-gain 归因（扩展版独有）

### 10.1 归因记录结构

```c
// llm-validator.h:70-87
typedef struct {
    llm_generation_stage_t stage;      // GRAMMAR / ENRICHMENT / STALL
    llm_validation_result_t result;    // OK / FORMAT_FAIL / GRAMMAR_FAIL / CONTEXT_FAIL
    char reason[128];                  // 失败原因描述
    u32 protocol_type;
    u32 seed_id;
    u32 llm_call_id;
    u32 input_bytes;
    u32 normalized_bytes;
    u32 region_count;
    u32 state_count;
    char response_code_seq[128];
    u8 has_new_cov;                    // 是否有新覆盖率
    u8 has_new_state;                  // 是否有新状态
    u8 has_new_transition;             // 是否有新转换
    u8 fault;
    u64 exec_us;                       // 执行耗时
} llm_validation_record_t;
```

### 10.2 收益分类

```c
llm_validation_result_t classify_llm_execution_gain(
    u8 has_new_cov,
    u8 has_new_state,
    u8 has_new_transition
) {
    if (!has_new_cov && !has_new_state && !has_new_transition)
        return LLM_VALID_NO_GAIN;  // 无收益
    return LLM_VALID_OK;           // 有收益
}
```

### 10.3 日志输出

扩展版在 `out_dir/llm-validation/` 目录下生成三个 CSV 文件：
- `grammar.csv`：语法提取阶段的验证记录
- `enrichment.csv`：种子丰富阶段的验证记录
- `stall.csv`：停滞突破阶段的验证记录

---

## 十一、对消融实验设计的影响

### 11.1 基准版 vs 扩展版 V0

扩展版 V0（`AFL_LLM_VALIDATION=0`）与基准版的行为**接近但不完全相同**，因为：

| 差异 | 基准版 | 扩展版 V0 |
|------|--------|----------|
| `clean_llm_response()` | 无 | **有**（即使 VALIDATION=0 也会执行） |
| `CHATTING_THRESHOLD` | 64 | 512 |
| Token 预算 | 较小 | 较大 |
| LLM 端点 | OpenAI | 可配置 |
| Stall prompt 格式约束 | 无 | 有 |

**结论**：扩展版 V0 不等于基准版。如果要与基准版对比，需要特别说明这些差异。

### 11.2 消融实验的变量分离

当前四变体（V0/V1/V2/ChatAFL）的消融目标：

| 对比 | 实际差异 | 可证明的结论 |
|------|---------|-------------|
| V0 vs V1 | `clean_llm_response()` + 格式验证 + 自动反馈 | 输出清洗 + 格式验证 + 反馈的组合效果 |
| V1 vs V2 | 语法+上下文验证 | 完整验证在格式验证基础上的增量效果 |
| V2 vs ChatAFL | Post-gain 归因 | 归因日志的价值（不直接影响覆盖率） |

**无法单独证明的结论**：
- `clean_llm_response()` 的独立贡献（V0 已包含）
- 反馈重试的独立贡献（V1/V2 自动启用 feedback）

### 11.3 建议的额外消融

```bash
# 分离 clean_llm_response 的贡献
# 需要一个"无 clean_llm_response 但有验证"的变体

# 分离 feedback 的贡献
AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=0 AFL_LLM_FEEDBACK=0  # V1-nofeedback
AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=1 AFL_LLM_FEEDBACK=0  # V2-nofeedback
```

---

## 十二、总结

### 12.1 扩展版的核心增量贡献

1. **`clean_llm_response()`**：输出清洗，过滤拒答、Markdown、说明文字
2. **验证框架**：三级验证模式，覆盖 RTSP/FTP/HTTP
3. **反馈重试**：验证失败后构造错误反馈让 LLM 重新生成
4. **Post-gain 归因**：记录每次 LLM 调用的执行收益
5. **环境变量控制**：支持任意 OpenAI-compatible LLM 服务
6. **Token 预算扩展**：更长的上下文窗口

### 12.2 与基准版的关键差异

| 差异点 | 影响程度 | 对实验的影响 |
|--------|---------|-------------|
| `CHATTING_THRESHOLD` 64→512 | **高** | 扩展版 LLM 参与度高 8 倍 |
| `clean_llm_response()` | **高** | 扩展版有输出清洗，基准版无 |
| Token 预算 2048→4096 | **中** | 扩展版 prompt 更长，但成本更高 |
| 验证框架 | **中** | 扩展版可过滤无效输出 |
| 反馈重试 | **中** | 扩展版可修复部分失败输出 |

### 12.3 论文写作注意事项

1. **明确区分基准贡献与扩展贡献**：验证框架、反馈重试、Post-gain 归因是扩展版独有，不应表述为论文原始机制
2. **注意常量差异**：`CHATTING_THRESHOLD` 的差异（64 vs 512）会显著影响实验结果，必须在实验部分说明
3. **V0 ≠ 基准版**：扩展版 V0 仍然包含 `clean_llm_response()` 和更大的 Token 预算，与基准版行为不同
