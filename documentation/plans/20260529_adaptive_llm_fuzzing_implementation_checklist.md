# Adaptive LLM Fuzzing Implementation Checklist

本文档把 `20260529_adaptive_validation_gain_guided_llm_fuzzing.md` 拆成代码级改造清单。目标是让后续实现不只是“再加一个验证层”，而是在当前 ChatAFL 代码上形成一个可消融、可解释、可实验验证的系统。

## A. 新增模块

### A1. `ChatAFL/llm-adaptive.h`

建议新增类型：

```c
typedef enum {
  LLM_ADAPT_STRICT = 0,
  LLM_ADAPT_FORMAT,
  LLM_ADAPT_REPAIR,
  LLM_ADAPT_TRIAL,
  LLM_ADAPT_REJECT
} llm_adaptive_action_t;

typedef struct {
  double p_strict;
  double p_format;
  double p_repair;
  double p_trial;
  double temperature;
  llm_validation_mode_t validation_mode;
  llm_adaptive_action_t action;
  char target_transition[32];
  char feedback_summary[1024];
} llm_adaptive_decision_t;

typedef struct {
  u32 total;
  u32 format_fail;
  u32 grammar_fail;
  u32 context_fail;
  u32 no_gain;
  u32 new_cov;
  u32 new_state;
  u32 new_transition;
  u32 hang_or_fault;
} llm_adaptive_window_t;
```

### A2. `ChatAFL/llm-adaptive.c`

建议实现接口：

```c
void llm_adaptive_init(const char *out_dir);

llm_adaptive_decision_t llm_adaptive_decide(
    llm_generation_stage_t stage,
    const llm_adaptive_window_t *window,
    u32 state_count,
    u32 edge_count);

void llm_adaptive_update_from_record(
    const llm_validation_record_t *record);

void llm_adaptive_build_feedback_summary(
    llm_generation_stage_t stage,
    char *buf,
    size_t buf_size);

int llm_adaptive_should_keep_trial(
    const llm_validation_record_t *record);
```

核心逻辑：

```text
Grammar:
  high p_strict, low temperature

Enrichment:
  moderate p_strict, moderate p_trial

Stall:
  lower p_strict, higher p_trial, dynamic temperature

invalid_rate high:
  increase p_strict, decrease temperature

no_gain_rate high:
  increase p_trial, increase temperature, add state target

new_transition high:
  keep policy and store useful pattern
```

## B. 修改 `ChatAFL/afl-fuzz.c`

### B1. 全局 flag

在当前 LLM flag 附近增加：

```c
u8 afl_llm_adaptive = 0;       // AFL_LLM_ADAPTIVE=1
u8 afl_llm_trial_queue = 0;    // AFL_LLM_TRIAL_QUEUE=1
u32 afl_llm_feedback_window = 64;
```

位置参考：

- `afl_llm_validation`
- `afl_llm_validation_strict`
- `afl_llm_post_gain`
- `afl_llm_feedback`

### B2. 环境变量读取

在读取 `AFL_LLM_VALIDATION` 的位置增加：

```c
afl_llm_adaptive = env_flag_enabled("AFL_LLM_ADAPTIVE");
afl_llm_trial_queue = env_flag_enabled("AFL_LLM_TRIAL_QUEUE");

const char *win = getenv("AFL_LLM_FEEDBACK_WINDOW");
if (win && win[0]) {
  int v = atoi(win);
  if (v > 0) afl_llm_feedback_window = v;
}
```

### B3. 初始化

在 `init_validation_log(out_dir)` 附近：

```c
if (afl_llm_adaptive) {
  llm_adaptive_init(out_dir);
}
```

### B4. Grammar 路径改造

当前流程：

```text
extract_message_pattern
-> validate_grammar_pattern
-> validate_grammar_required_fields
-> fail then continue
```

建议改造：

```text
decision = llm_adaptive_decide(LLM_STAGE_GRAMMAR, ...)
if decision.action == STRICT:
  current full validation
else if FORMAT:
  accept only known method and basic fields
else:
  reject trial for grammar stage

record grammar_accept / grammar_reject / grammar_low_match
update adaptive window
```

新增指标：

```text
grammar_seed_match_rate
grammar_message_type
grammar_required_field_count
```

原因：

grammar 污染会影响 `parse_buffer()` 和结构感知变异，不能像 stall 一样大胆 trial。

### B5. Enrichment 路径改造

当前验证位置：

```c
record.result = validate_llm_sequence_with_mode(...)
if (record.result != LLM_VALID_OK && !afl_llm_validation_permissive) {
  feedback retry or continue;
}
```

建议改造：

```text
decision = llm_adaptive_decide(LLM_STAGE_ENRICHMENT, ...)

if STRICT/FORMAT:
  run selected validation

if validation OK:
  write enriched seed

if validation fail and deterministic repair possible:
  repair locally, then revalidate

if validation fail but decision.action == TRIAL:
  execute once in isolation
  keep only if new cov/state/transition

else:
  feedback retry or reject
```

Trial enrichment 不建议直接写入 `in_dir`，应先通过 `common_fuzz_stuff()` 或专门的 replay path 试执行。若实现成本较高，第一版可先把 trial seed 写入 `out_dir/llm-trial/`，后续由独立脚本 replay 并筛选。

### B6. Stall-breaking 路径改造

当前流程：

```text
history/examples
-> construct_prompt_stall()
-> chat_with_llm(..., 1.5)
-> extract_stalled_message
-> validate
-> common_fuzz_stuff
-> optional post-gain
```

建议改造：

```text
decision = llm_adaptive_decide(LLM_STAGE_STALL, ...)
feedback_summary = llm_adaptive_build_feedback_summary(...)
target_transition = select_target_transition(ipsm, recent responses)
temperature = decision.temperature

construct_prompt_stall_with_feedback(
  protocol_name,
  examples,
  history,
  feedback_summary,
  target_transition)

execute candidate according to decision.action
always fill post-execution record
update adaptive window
```

重要改变：

- stall 阶段不应只生成“proper request”。
- 应生成“可能突破低频状态转移的 request”。
- trial 失败不污染 corpus，trial 成功可提升 LLM seed priority。

## C. 修改 `ChatAFL/chat-llm.c`

### C1. Stall prompt

新增：

```c
char *construct_prompt_stall_with_feedback(
    char *protocol_name,
    char *examples,
    char *history,
    const char *feedback_summary,
    const char *target_transition);
```

Prompt 结构建议：

```text
You are assisting stateful protocol fuzzing.

Recent runtime feedback:
{feedback_summary}

Target:
Try to generate a request likely to reach {target_transition}
or any under-explored server response state.

Communication history:
{history}

Output exactly one raw client request.
```

### C2. Enrichment prompt

新增：

```c
char *enrich_sequence_with_feedback_prompt(
    char *sequence,
    khash_t(strSet) *missing_message_types,
    const char *feedback_summary,
    char **prompt_out,
    double temperature);
```

与当前 `enrich_sequence_with_prompt()` 的区别：

- 不只补缺失 method。
- 增加最近高收益 pattern。
- 避免最近 no-gain pattern。
- temperature 由 adaptive decision 给出。

### C3. 本地 deterministic repair

可先放在 `llm-validator.c` 或新文件中：

```c
int llm_repair_candidate(
    const char *protocol,
    const char *input,
    char **repaired,
    protocol_context_t *ctx);
```

第一版只做确定性修复：

- 补 `\r\n\r\n`
- CRLF 归一化
- RTSP 补 `CSeq`
- 从 history/session cache 补 `Session`
- `SETUP` 补常见 `Transport`
- HTTP 重算 `Content-Length`

## D. 修改 `ChatAFL/llm-validator.h/c`

### D1. 记录 action 和 temperature

扩展 `llm_validation_record_t`：

```c
u8 adaptive_action;
float temperature;
u8 accepted_to_corpus;
u8 trial_executed;
char target_transition[32];
char candidate_class[32];
```

CSV header 同步扩展。

### D2. 分类粒度

当前 result 只有：

```text
FORMAT_FAIL
GRAMMAR_FAIL
CONTEXT_FAIL
NO_GAIN
OK
```

建议在 `reason` 中保留细粒度：

```text
format_missing_crlf
format_bad_request_line
grammar_unknown_method
grammar_missing_cseq
grammar_missing_transport
context_missing_session
context_stale_session
gain_new_cov
gain_new_state
gain_new_transition
nogain_repeated_response_seq
nogain_400_404_loop
```

不一定扩展 enum，先扩展 `reason` 更低风险。

## E. 数据结构和日志

新增目录：

```text
out_dir/llm-adaptive/
  policy.csv
  feedback-summary.log
  trial.csv
  useful-patterns.log
  rejected-patterns.log
```

`policy.csv`：

```text
time,stage,p_strict,p_format,p_repair,p_trial,temperature,invalid_rate,no_gain_rate,gain_rate,state_count,edge_count
```

`trial.csv`：

```text
time,stage,result,reason,new_cov,new_state,new_transition,response_code_seq,kept
```

这些数据可以直接支撑论文的过程指标。

## F. 推荐实现顺序

### Phase 1: 不改 LLM prompt，只做 adaptive logging

目标：

- 新增 `llm-adaptive.c/h`
- 记录窗口统计
- 记录 policy.csv
- 不改变 fuzzing 行为

价值：

- 低风险。
- 能复盘当前 full/v0/v1/v2 为什么失败。

### Phase 2: 动态 temperature + feedback summary

目标：

- stall 和 enrichment 使用动态 temperature。
- prompt 加入 feedback summary。

价值：

- 改动小。
- 能快速验证 LLM 输出是否更少重复 no-gain。

### Phase 3: 概率验证

目标：

- 用 `llm_adaptive_decide()` 替代固定 `get_llm_validation_mode()`。
- 按 stage 采样 strict/format/trial。

价值：

- 形成论文核心。

### Phase 4: Trial queue

目标：

- trial 候选只在产生收益时保留。
- 避免 v0 式污染。

价值：

- 理论上最可能提升 coverage 和 state transition。

### Phase 5: State-targeted stall prompt

目标：

- 从 IPSM 选择低频/未探索 transition。
- LLM prompt 明确状态目标。

价值：

- 直接服务状态转移覆盖率。

## G. 预期论文实验结论

理想结果不是“所有指标都碾压”，而是形成合理 trade-off：

```text
v0:
  high exploration, high pollution/hang

static full validation:
  low pollution, low exploration

adaptive + trial:
  comparable or better coverage than v0
  lower hang/pollution than v0
  higher IPSM edges than static full

adaptive + state target:
  best IPSM transition coverage
```

只要实验能证明：

1. adaptive 不是简单验证层；
2. trial 能保留半合法输入收益；
3. state-target feedback 提升 IPSM edges；
4. dynamic summary 降低 repeated no-gain；

论文主线就比“验证驱动 LLM”更稳。
