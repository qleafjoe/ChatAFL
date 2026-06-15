# ChatAFL 验证失败反馈重试机制 — 完整上下文

## 1. 项目概况

**项目**: ChatAFL — 基于 LLM 的协议模糊测试工具
**分支**: ggqiao
**目标**: 在 Enrichment 和 Stall 阶段，当 LLM 输出验证失败时，构造反馈 prompt 让 LLM 重新生成，提升有效种子数量和代码覆盖率。

## 2. 现有系统分析

### 2.1 四层验证体系

| 层级 | 验证类型 | 控制变量 |
|------|---------|---------|
| 1. 格式校验 | FORMAT_FAIL | AFL_LLM_VALIDATION=1 |
| 2. 语法校验 | GRAMMAR_FAIL | AFL_LLM_VALIDATION_STRICT=1 |
| 3. 上下文校验 | CONTEXT_FAIL | AFL_LLM_VALIDATION_STRICT=1 |
| 4. 成本分析 | NO_GAIN | AFL_LLM_POST_GAIN=1 |

### 2.2 消融变体

| 变体 | VALIDATION | STRICT | POST_GAIN |
|------|:---:|:---:|:---:|
| V0 | 0 | 0 | 0 |
| V1 | 1 | 0 | 0 |
| V2 | 1 | 1 | 0 |
| ChatAFL | 1 | 1 | 1 |

### 2.3 三个验证触发点

| 阶段 | 阶段标识 | 验证函数 | 适合反馈 |
|------|---------|---------|:--------:|
| 语法提取 | LLM_STAGE_GRAMMAR | validate_grammar_pattern() | 否 |
| 种子增强 | LLM_STAGE_ENRICHMENT | validate_llm_sequence_with_mode() | **是** |
| 突破停滞 | LLM_STAGE_STALL | validate_llm_message_with_mode() | **是** |

### 2.4 现有重试机制

- `chat_with_llm(prompt, model, tries, temperature)` 内部重试
- STALL_RETRIES=2, ENRICHMENT_RETRIES=5, GRAMMAR_RETRIES=5
- 重试的是**同一个 prompt**，处理 API 调用失败
- 与反馈重试**不冲突**（不同层级）

## 3. 内存管理关键发现

| 函数 | 分配方式 | 释放方式 | 是否 free 输入指针 |
|------|---------|---------|:-----------------:|
| extract_stalled_message() | strdup (malloc) | free() | 否 |
| format_request_message() | ck_alloc | ck_free() | **是 (line 476)** |
| unescape_string() | malloc | free() | 否 |
| construct_prompt_stall() | strdup (malloc) | free() | 否 |
| chat_with_llm() | malloc | free() | 否 |

**关键**: `format_request_message()` 在 line 476 `free(message)` 释放传入指针，返回新的 ck_alloc 指针。

### 3.1 内存生命周期追踪

**Stall 路径**:
```
stall_message = extract_stalled_message(...)     // malloc (strdup)
stall_message = format_request_message(stall_message)  // free旧, 返回ck_alloc
// 此时 stall_message 是 ck_alloc → ck_free 正确
```

**反馈循环**:
```
fb_message = extract_stalled_message(...)        // malloc (strdup)
fb_message = format_request_message(fb_message)  // free旧, 返回ck_alloc
// 验证失败: ck_free(fb_message)
// 验证成功: 作为 recovered 返回，调用方 ck_free
```

**Enrichment 路径**:
```
unescaped = unescape_string(...)                 // malloc
unescaped = format_request_message(unescaped)    // free旧, 返回ck_alloc
// 注意: formatted_request_base 在此之后是悬空指针！
```

## 4. 实现方案

### 4.1 架构：两个独立函数

```
chat-llm.c 新增:
  + construct_feedback_prompt_stall()        // Stall 专用反馈 prompt
  + construct_feedback_prompt_enrichment()   // Enrichment 专用反馈 prompt
  + llm_feedback_retry_stall()               // Stall 反馈重试
  + llm_feedback_retry_enrichment()          // Enrichment 反馈重试

chat-llm.h 新增:
  + 两个函数声明
  + LLM_FEEDBACK_MAX_RETRIES_DEFAULT 宏

llm-validator.c 新增:
  + get_validation_error_detail()            // static char[256]

llm-validator.h 新增:
  + get_validation_error_detail 声明

afl-fuzz.c 修改:
  + 2 个全局变量 (afl_llm_feedback, afl_llm_feedback_max_retries)
  + env 读取（+6行）
  + Stall 调用 llm_feedback_retry_stall()（+10行）
  + Enrichment 调用 llm_feedback_retry_enrichment()（+10行）
```

### 4.2 函数签名

```c
// llm-validator.h
const char *get_validation_error_detail(
    const char *protocol,
    llm_validation_result_t result,
    const char *failed_message);
// 返回: static char[256]，不需要释放

// chat-llm.h
#define LLM_FEEDBACK_MAX_RETRIES_DEFAULT 3

char *llm_feedback_retry_stall(
    const char *protocol_name,
    const char *failed_message,
    llm_validation_result_t error,
    llm_validation_mode_t mode,
    int max_retries);
// 返回: ck_alloc 指针，调用方 ck_free()；失败返回 NULL

char *llm_feedback_retry_enrichment(
    const char *protocol_name,
    const char *failed_message,
    llm_validation_result_t error,
    llm_validation_mode_t mode,
    int max_retries);
// 返回: ck_alloc 指针，调用方 ck_free()；失败返回 NULL
```

### 4.3 afl-fuzz.c 调用方式

**Stall 阶段**（约 7292 行）:
```c
if (record.result != LLM_VALID_OK && !afl_llm_validation_permissive) {
    if (afl_llm_feedback) {
        char *recovered = llm_feedback_retry_stall(
            protocol_name, stall_message, record.result,
            validation_mode, afl_llm_feedback_max_retries);
        if (recovered) {
            ck_free(stall_message);
            stall_message = recovered;
        } else {
            ck_free(stall_message);
            goto free_stall;
        }
    } else {
        ck_free(stall_message);
        goto free_stall;
    }
}
```

**Enrichment 阶段**（约 3021 行）:
```c
if (record.result != LLM_VALID_OK && !afl_llm_validation_permissive) {
    if (afl_llm_feedback) {
        char *recovered = llm_feedback_retry_enrichment(
            protocol_name, unescaped_client_requests, record.result,
            validation_mode, afl_llm_feedback_max_retries);
        if (recovered) {
            ck_free(unescaped_client_requests);
            unescaped_client_requests = recovered;
        } else {
            free(client_request_answer);
            continue;
        }
    } else {
        ck_free(unescaped_client_requests);
        free(client_request_answer);
        continue;
    }
}
```

## 5. 环境变量设计

### 5.1 新增变量

```bash
AFL_LLM_FEEDBACK=1              # 启用反馈重试
AFL_LLM_FEEDBACK_MAX_RETRIES=3  # 最大重试次数
```

### 5.2 各变体 env.sh

| 变体 | VALIDATION | STRICT | POST_GAIN | FEEDBACK |
|------|:---:|:---:|:---:|:---:|
| V0 | 0 | 0 | 0 | 0 |
| V1 | 1 | 0 | 0 | 0 |
| V2 | 1 | 1 | 0 | 0 |
| ChatAFL | 1 | 1 | 1 | 1 |

### 5.3 传递链

```
run.sh → profuzzbench_exec_common.sh → docker run -e → env.sh → afl-fuzz
```

Dockerfile line 83-86 已正确复制 env.sh 到容器，无需额外修改。

## 6. 风险审查

| # | 风险 | 严重度 | 结论 |
|---|------|--------|------|
| 1 | 内存双重释放 | 低 | format_request_message 返回新指针，旧指针丢失 |
| 2 | 内存泄漏 | 低 | ck_alloc 失败会 FATAL |
| 3 | **悬空指针 formatted_request_base** | **中** | line 3001 后指向已释放内存，反馈代码绝不能碰 |
| 4 | stall_response 泄漏 | - | 已有 bug，非本次引入 |
| 5 | 两个函数代码重复 | 低 | 用户选择接受 |
| 6 | env var 未设置 | 无 | 反馈代码跳过，行为不变 |
| 7 | LLM 返回同样无效消息 | 低 | 受 max_retries 限制 |

## 7. 论文定位

### 7.1 核心贡献
- 四层验证框架（系统设计）
- 闭环反馈机制（系统设计）
- 验证失败反馈重试（算法）

### 7.2 覆盖率提升逻辑
```
当前 ChatAFL: 1000 次 LLM 调用 → 600 有效种子 → 基线覆盖率 C₀
+ 反馈重试:   1000 次 LLM 调用 → 600 + 250 修复 = 850 有效种子 → C₀ + Δ
```

### 7.3 消融实验
- ChatAFL vs V2 唯一差异: FEEDBACK=1 vs FEEDBACK=0
- 论文中需说明同时包含 POST_GAIN 变量

## 8. 实现步骤

1. llm-validator.h/c — get_validation_error_detail()
2. chat-llm.h/c — 4 个新函数
3. afl-fuzz.c — 全局变量 + env 读取 + 两处调用
4. env.sh × 4 — 新增 FEEDBACK 变量
5. run.sh + profuzzbench_exec_common.sh — 传递新 env vars
6. 编译测试
7. Docker 构建

---
*生成时间: 2026-05-14*
