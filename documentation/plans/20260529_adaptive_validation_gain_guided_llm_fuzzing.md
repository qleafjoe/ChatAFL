# Adaptive Validation and Gain-Guided LLM Fuzzing for ChatAFL

本文档给出一个面向当前仓库的改进方向。目标不是继续把工作描述成“加一个验证层”，而是把现有 ChatAFL 的三条 LLM 路径改造成一个动态系统：

```text
LLM candidate
  -> deterministic repair
  -> probabilistic adaptive validation
  -> accept / repair / trial execution
  -> coverage/state/transition gain attribution
  -> update runtime feedback memory
  -> update validation probability and prompt temperature
  -> state-targeted next LLM query
```

核心主张：

> 静态验证会提高输入合法性，但可能降低 fuzzing 探索性。本文通过概率自适应验证、隔离试执行队列和状态目标反馈，在合法性与探索性之间动态平衡，从理论上同时保留 v0 的探索能力和完整验证的语料质量控制能力。

## 1. 当前代码实际路径

### 1.1 Grammar extraction

入口：

- `ChatAFL/afl-fuzz.c:768` `setup_llm_grammars()`
- `ChatAFL/afl-fuzz.c:781` 调用 `chat_with_llm()` 生成模板
- `ChatAFL/afl-fuzz.c:877` `extract_message_pattern()`
- `ChatAFL/afl-fuzz.c:901` `validate_grammar_pattern()`
- `ChatAFL/afl-fuzz.c:909` `validate_grammar_required_fields()`

现状问题：

1. grammar 验证是静态的，依赖 `get_llm_validation_mode()`。
2. 验证失败后直接拒绝，没有记录该 grammar 对真实 seed 的匹配率。
3. 没有把“哪些 grammar 导致后续覆盖提升”反馈给 LLM。
4. 语法模板来自 LLM 自身知识，缺少对当前目标实现、已有 seed 和 dictionary 的运行时约束。

改进方向：

- grammar 路径应该最严格，但不是只看 method 是否在白名单里。
- 增加 `seed_match_rate`：新 grammar 在初始 seed / 高收益 seed 上的命中率。
- 增加 `grammar_gain_score`：该 grammar 参与 parse 后产生的新 path/state/transition 数。
- 对低命中且低收益 grammar 降权，而不是只做二值接收。

### 1.2 Seed enrichment

入口：

- `ChatAFL/afl-fuzz.c:3271` `enrich_testcases()`
- `ChatAFL/chat-llm.c:1173` `enrich_sequence_with_prompt()`
- `ChatAFL/afl-fuzz.c:3184` `format_request_message()`
- `ChatAFL/afl-fuzz.c:3199` `validate_llm_sequence_with_mode()`
- `ChatAFL/afl-fuzz.c:3230` 写入 `enriched_*` seed

现状问题：

1. enrichment 主要目标是补缺失 message type，但不知道补出来的序列是否带来状态收益。
2. 验证失败时要么 retry，要么丢弃，缺少“可发送但不完全合法”的隔离试执行。
3. `write_new_seeds` / enriched seed 写入前缺少执行收益确认，可能污染输入目录。
4. 当前只记录 validation record，没有维护跨调用的错误画像。

改进方向：

- enrichment 采用中等严格验证。
- 对格式可切分但上下文不确定的候选进入 `trial` 路径。
- trial 候选先执行，只有产生新 coverage/state/transition 才写入 corpus。
- prompt 加入最近高收益 seed 的结构摘要，而不是只列缺失 method。

### 1.3 Stall-breaking

入口：

- `ChatAFL/afl-fuzz.c:7365` 检查 `uninteresting_times >= UNINTERESTING_THRESHOLD`
- `ChatAFL/afl-fuzz.c:7466` `construct_prompt_stall()`
- `ChatAFL/afl-fuzz.c:7468` `chat_with_llm(..., temperature=1.5)`
- `ChatAFL/afl-fuzz.c:7512` `validate_llm_message_with_mode()`
- `ChatAFL/afl-fuzz.c:7552` `common_fuzz_stuff()`
- `ChatAFL/afl-fuzz.c:7554` `fill_post_execution_record()`

现状问题：

1. stall prompt 只有通信历史和例子，没有明确目标状态或目标转移。
2. temperature 固定为 1.5，不随 invalid/no-gain/state-stall 动态变化。
3. strict validation 会把一些状态违背但有探索价值的消息过滤掉。
4. `NO_GAIN` 已经记录，但没有反过来影响下一次 LLM prompt 和验证概率。

改进方向：

- stall 是最应该探索的路径，应使用较高 trial 概率。
- prompt 应包含目标，例如“尝试从 201 推进到 202/203/204 或低频 transition”。
- 对 `NO_GAIN` 的模式做摘要，避免 LLM 反复生成 400/404 循环消息。
- 对高收益 stall message 的 method/URI/header pattern 做正反馈。

## 2. 核心方法：概率自适应验证

### 2.1 验证动作

新增运行时动作：

```c
typedef enum {
  LLM_ACTION_ACCEPT_STRICT,
  LLM_ACTION_ACCEPT_FORMAT,
  LLM_ACTION_REPAIR,
  LLM_ACTION_TRIAL,
  LLM_ACTION_REJECT
} llm_adaptive_action_t;
```

含义：

- `ACCEPT_STRICT`：完整验证，适合 grammar 和高污染风险 seed。
- `ACCEPT_FORMAT`：只做可发送格式验证。
- `REPAIR`：对可确定修复的错误本地修复或一次 LLM repair。
- `TRIAL`：隔离试执行，不立即进入 seed corpus。
- `REJECT`：不可切分、不可发送、明显无效。

### 2.2 初始概率

按 LLM 阶段设置初始概率：

| 阶段 | strict | format | trial | repair |
|------|--------|--------|-------|--------|
| Grammar | 0.80 | 0.20 | 0.00 | 0.00 |
| Enrichment | 0.45 | 0.30 | 0.20 | 0.05 |
| Stall | 0.25 | 0.30 | 0.40 | 0.05 |

原因：

- grammar 一旦污染，会影响结构感知变异，应偏严格。
- enrichment 会新增 seed，但也承担扩展状态空间，应保留一定 trial。
- stall 的语义是突破停滞，应允许更多半合法探索。

### 2.3 动态调整

维护最近窗口，例如最近 64 次 LLM candidate：

```c
typedef struct {
  u32 total;
  u32 format_fail;
  u32 grammar_fail;
  u32 context_fail;
  u32 no_gain;
  u32 new_cov;
  u32 new_state;
  u32 new_transition;
  u32 fault;
  u32 hang;
} llm_window_stats_t;
```

动态调整规则：

```text
invalid_rate = (format_fail + grammar_fail + context_fail) / total
gain_rate = (new_cov + new_state + new_transition) / total
no_gain_rate = no_gain / total

if invalid_rate > 0.45:
  increase strict probability
  decrease temperature

if no_gain_rate > 0.60 and invalid_rate < 0.30:
  increase trial probability
  increase temperature
  switch to state-targeted prompt

if new_transition increases:
  keep current policy and boost similar candidates

if hang/fault rate too high:
  decrease trial probability
  increase format validation
```

一个可实现的公式：

```text
p_strict = clamp(base_strict + 0.35 * invalid_rate - 0.20 * gain_rate, 0.10, 0.85)
p_trial  = clamp(base_trial  + 0.30 * no_gain_rate + 0.20 * stall_rate - 0.30 * invalid_rate, 0.05, 0.60)
p_format = 1 - p_strict - p_trial - p_repair
```

这不是简单切换，而是对候选输入进行概率采样。论文里可以称为 probabilistic adaptive validation。

## 3. Trial Queue：保留 v0 的探索性，避免污染 corpus

当前 v0 覆盖较高，说明“无验证的野输入”确实能探索更多路径。但它也带来 hang 和语料污染。解决方法不是回到 v0，而是引入 trial queue。

### 3.1 Trial 候选准入

最低要求：

1. 字节可打印或可归一化。
2. 能被 `extract_requests_*()` 切出至少一个 region。
3. 不超过最大消息长度。
4. 网络发送后不会导致连续严重超时。

不要求：

- 必须有合法 Session。
- 必须符合完整状态机。
- 必须所有 header 都完美。

### 3.2 Trial 执行后准入

Trial candidate 先通过 `common_fuzz_stuff()` 执行，但不立即写入正式 seed corpus。只有满足以下任一条件才保留：

```text
has_new_cov == 1
has_new_state == 1
has_new_transition == 1
response_code_seq contains rare state
```

否则丢弃或降权。

这能在理论上提升覆盖率：半合法输入仍能触发 parser/error-handling branch，但只有产生收益才进入后续 fuzzing。

## 4. 多维运行时反馈，不只是 validation error

用户提出“增加信息维度”是合理的。当前项目可直接提取这些变量：

### 4.1 本项目已有变量

| 信息 | 来源 | 用途 |
|------|------|------|
| validation result | `llm-validation/*.csv` | 错误分类 |
| response code sequence | `responses-ipsm` / `fill_post_execution_record()` | 状态反馈 |
| IPSM nodes/edges | `ipsm` / `plot_data` | 状态覆盖与转移覆盖 |
| `chat_times` | `plot_data` | LLM 调用强度 |
| `unique_hangs` | `fuzzer_stats` | 异常率 |
| `execs_per_sec` | `fuzzer_stats` | 性能 |
| seed depth / favored | queue entry | seed 调度 |
| message regions | `extract_requests_*()` | 协议结构质量 |

### 4.2 动态反馈摘要

每 N 次 LLM 调用生成一个小型摘要：

```text
Recent runtime feedback:
- 42% generated messages missed CSeq or malformed request line.
- Valid PLAY/PAUSE messages often returned 454 because Session was stale.
- The fuzzer repeatedly explores 400->404 with no new coverage.
- Recent useful transitions: 201->202, 202->203.
- Under-explored states: 204, 205, 210.
- High-gain patterns: SETUP with alternative track ID; DESCRIBE unseen media path.

Generation objective:
- Prefer messages likely to reach under-explored states.
- Avoid repeating requests that produce only 400/404.
- Reuse live Session value if available in history.
```

这个摘要可以插入：

- `construct_prompt_stall()`
- `enrich_sequence_with_prompt()`
- feedback retry prompt

## 5. 动态 temperature

当前项目中：

- grammar/enrichment 多为 `0.5`
- stall 固定 `1.5`
- feedback retry 固定 `0.7`

建议改为：

| 场景 | temperature |
|------|-------------|
| grammar 初始 | 0.2-0.5 |
| grammar 多次重复 | 0.6 |
| enrichment 正常 | 0.5-0.9 |
| stall 正常 | 0.8-1.2 |
| 连续 no-gain | 1.2-1.6 |
| invalid_rate 高 | 降到 0.3-0.7 |
| state stagnation 高 | 升高并增加 trial |

核心原则：

```text
格式错多 -> 降温
合法但无收益 -> 升温
状态停滞 -> 升温 + state-target prompt + trial
hang 多 -> 降温 + format validation
```

## 6. 轻量 RAG / Runtime Feedback Retrieval

不要做过重的 multi-agent。当前仓库可以做轻量检索，增强复杂性但保持可实现。

候选知识源：

1. `rtsp.dict`
2. 初始 seed 目录 `in-rtsp`
3. 高收益 queue seed
4. `responses-ipsm`
5. `protocol-grammars`
6. `llm-validation/*.csv`
7. `ipsm.dot`

检索策略：

```text
给定当前 stage + 当前 response seq + target state
  -> 找最近高收益 seed
  -> 找包含目标 response code 的历史 trace
  -> 找与当前 method 相同但结果不同的请求
  -> 压缩成 prompt snippets
```

这比泛泛 RAG 更贴合项目，也能借鉴 MultiFuzz 的思想而不过度工程化。

## 7. 状态目标驱动生成

当前 stall prompt 的目标太泛。应增加 state target：

```text
Current response sequence: 0-400-200-201
Current rare states: 203, 204, 205, 210
Low-frequency transitions: 201->202, 202->203
Goal: generate one RTSP request likely to reach a rare state or low-frequency transition.
```

状态目标选择：

1. 优先未见过的 response code。
2. 其次低频 transition。
3. 再其次能离开 400/404 循环的 transition。
4. 若已有 Session，则优先 PLAY/PAUSE/TEARDOWN 变体。
5. 若无 Session，则优先 DESCRIBE/SETUP 变体。

这能直接服务状态转移覆盖率。

## 8. 代码修改建议

### 8.1 新增文件

```text
ChatAFL/llm-adaptive.h
ChatAFL/llm-adaptive.c
```

负责：

- 维护窗口统计。
- 计算概率。
- 选择 validation action。
- 计算动态 temperature。
- 生成 runtime feedback summary。

### 8.2 修改 `afl-fuzz.c`

替换固定逻辑：

```c
llm_validation_mode_t validation_mode = get_llm_validation_mode();
```

改为：

```c
llm_adaptive_decision_t decision =
    llm_select_adaptive_decision(LLM_STAGE_STALL, &runtime_stats);
```

在三个位置接入：

- `setup_llm_grammars()`
- `enrich_testcases()`
- stall-breaking block

### 8.3 修改 `chat-llm.c`

增加带上下文的 prompt 构造函数：

```c
construct_prompt_stall_with_feedback(protocol, examples, history, feedback_summary, target_state)
construct_enrichment_prompt_with_feedback(sequence, missing_types, feedback_summary)
```

### 8.4 修改日志

扩展 `llm_validation_record_t`：

```c
u32 source_state;
u32 target_state;
u8 action;          // strict / format / trial / repair / reject
u8 accepted_to_corpus;
float temperature;
char target_transition[32];
char candidate_class[32]; // valid / near-valid / state-violating
```

## 9. 消融实验设计

建议实验组：

| 组别 | 含义 |
------|------|
| AFLNet | 原始 baseline |
| ChatAFL | 原版 |
| v0 | 无验证 |
| Static-Full | 当前 full validation |
| Adaptive-Val | 只启用概率验证 |
| Adaptive-Val + Trial | 加 trial queue |
| Adaptive-Val + Trial + StateTarget | 完整方法 |
| Full + FeedbackSummary | 验证动态摘要是否有效 |

核心指标：

```text
line coverage
branch coverage
IPSM nodes
IPSM edges
new transitions per LLM call
new coverage per LLM call
NO_GAIN rate
trial accept rate
invalid rate
hang rate
exec/sec
LLM startup time vs fuzzing time
```

预期结论：

1. v0 可能覆盖高但 hang/污染高。
2. Static-Full 稳定但探索不足。
3. Adaptive-Val + Trial 应接近或超过 v0 的覆盖，同时降低污染。
4. StateTarget 应主要提升 IPSM edges，而不是只提升 line coverage。

## 10. 论文定位

不建议继续使用：

> Validation-driven LLM fuzzing

建议改成：

> Probabilistic Adaptive Validation and Runtime Feedback Retrieval for LLM-Guided Stateful Protocol Fuzzing

贡献点：

1. 提出概率自适应验证，动态平衡 LLM 输入合法性与 fuzzing 探索性。
2. 提出 trial execution queue，保留半合法输入的覆盖潜力并避免语料污染。
3. 提出运行时反馈摘要，将 validation error、网络响应、状态转移和执行收益共同反馈给 LLM。
4. 提出状态目标驱动的 stall-breaking，使 LLM 生成直接面向低频/未探索状态转移。

这个方向比“验证层 + 反馈重试”更复杂，也更贴合当前项目实验中暴露的问题。
