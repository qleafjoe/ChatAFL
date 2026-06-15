# ChatAFL-TR5：Transition-Oriented Soft Validation

## 定位

最复杂的变体。不使用概率软接受，而是尝试从运行时上下文**修复**验证失败的候选消息。

## 消融配置

| 标志 | 值 | 说明 |
|------|----|------|
| Hard Validation | **ON** | 严格验证（格式+语法+上下文） |
| Feedback Retry | **ON (3次)** | 验证失败时反馈错误信息并重试 |
| Soft Accept | OFF（概率） | 不使用概率软接受 |
| Contextual Recovery | **ON** | 运行时上下文提取 + 候选修复 |
| 状态转换图 | 保留 | 协议一致性检查启用 |
| 上下文感知拒绝检测 | 保留 | 协议指标预检查 |
| LF→CRLF 转换 | 保留 | 协议格式修正 |
| 指数退避重试 | 保留 | 指数退避（上限 30s） |
| 语法解析鲁棒性 | 保留 | JSON 防御性检查 |

## 编译时常量

```c
TR5_ENABLE_STALL_SOFT_VALIDATION       = 1  // 停滞场景启用上下文修复
TR5_ENABLE_ENRICHMENT_SOFT_VALIDATION  = 0  // 种子丰富场景不启用
TR5_REQUIRE_REVALIDATION_AFTER_CONTEXTUALIZE = 1  // 修复后需重新验证
```

## 核心创新：上下文修复机制

### 新增数据结构

```c
typedef struct {
    char *last_cseq;      // 最近的 CSeq 值
    char *last_session;   // 最近的 Session ID
    char *last_transport; // 最近的 Transport 头
    char *last_uri;       // 最近的请求 URI
    char *last_method;    // 最近的 RTSP 方法
} protocol_runtime_context_t;
```

### 新增函数

| 函数 | 说明 |
|------|------|
| `extract_runtime_context_from_history()` | 从交互历史解析 Session、CSeq、Transport、URI |
| `is_transition_critical_candidate()` | 判断是否为转换关键方法（SETUP/PLAY/PAUSE/TEARDOWN） |
| `contextualize_candidate()` | 注入缺失头字段修复候选消息 |
| `can_soft_accept_candidate()` | 判断是否可软接受（仅 CONTEXT_FAIL + 转换关键方法） |
| `has_sufficient_context_for_method()` | 检查上下文是否包含必要字段 |
| `find_header()` | 大小写不敏感的头字段查找 |

### 修复流程

```
LLM 生成候选
    ↓
严格验证 ──通过──→ 接受
    ↓ 失败
判断：CONTEXT_FAIL 且是转换关键方法？
    ↓ 是
从交互历史提取运行时上下文
    ↓
检查上下文是否充分（SETUP 需要 CSeq+Transport，PLAY/PAUSE/TEARDOWN 需要 CSeq+Session）
    ↓ 充分
contextualize_candidate() 注入缺失头字段
    ↓
重新验证 ──通过──→ 软接受
    ↓ 仍失败
反馈重试（最多3次）──通过──→ 接受
    ↓ 仍失败
拒绝
```

### 验证分类修改

修改了 `llm-validator.c` 中的失败类型分类：
- SETUP 缺少 Transport：从 `GRAMMAR_FAIL` 重分类为 `CONTEXT_FAIL`（可恢复）
- PLAY/PAUSE/TEARDOWN 缺少 Session：从 `GRAMMAR_FAIL` 重分类为 `CONTEXT_FAIL`（可恢复）

### 分析字段

`llm-validation_record_t` 新增 TR5 专用字段：
- `original_validation_result` — 原始验证结果
- `post_contextualize_validation_result` — 修复后验证结果
- `is_transition_critical` — 是否为转换关键方法
- `runtime_ctx_available` — 运行时上下文是否可用
- `recovered_field_count` — 恢复的字段数量
- `soft_accept_reason[64]` — 软接受原因
- `recovered_fields[128]` — 恢复的字段列表

CSV 日志新增 7 列 TR5 分析字段。

## 与 TR3 的关键差异

### afl-fuzz.c（11962 行 vs 基线 11594 行，+368 行）
- 唯一比基线更大的变体
- 新增 `protocol_runtime_context_t` 结构体和全部上下文修复函数
- 重写了 `setup_llm_grammars()` 使用一致性表方法
- 使用 `write_llm_text_artifact()` 记录停滞交互日志

### llm-validator.c / llm-validator.h
- 验证分类规则修改（GRAMMAR_FAIL → CONTEXT_FAIL 重分类）
- 新增 TR5 专用分析字段

## 行为总结

TR5 用**上下文修复**替代 TR4 的**概率软接受**。当 LLM 生成的转换关键方法缺少必要头字段时，从历史交互中提取运行时状态并注入修复。这是一种确定性的、基于协议语义的修复策略，而非随机接受。消融实验中用于测量"上下文感知修复"相比"概率接受"的有效性。
