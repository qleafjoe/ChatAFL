# Soft Validation + PostGain Scheduler 方案框架与代码级设计

> 适用范围：本文基于当前仓库中的 `ChatAFL/`、`ChatAFL-V0/V1/V2/`、`benchmark/` 和现有验证层实现，描述一个计划加入系统的实验方案：Soft Validation、PostGain Scheduler 以及二者组合的消融框架。本文不是基准论文 ChatAFL 的原始机制说明，而是面向当前工程版本的可实现设计文档。

## 目录

1. [设计动机与实验目标](#1-设计动机与实验目标)
2. [五组实验变体定义](#2-五组实验变体定义)
3. [新增环境变量与运行时开关](#3-新增环境变量与运行时开关)
4. [Soft Validation 软准入机制](#4-soft-validation-软准入机制)
5. [PostGain Scheduler 收益感知调度](#5-postgain-scheduler-收益感知调度)
6. [系统运行逻辑与数据流](#6-系统运行逻辑与数据流)
7. [实现接入点与代码级修改范围](#7-实现接入点与代码级修改范围)
8. [示例：RTSP Stall 消息的完整处理过程](#8-示例rtsp-stall-消息的完整处理过程)
9. [日志、指标与实验分析方法](#9-日志指标与实验分析方法)
10. [预期收益、风险与消融解释边界](#10-预期收益风险与消融解释边界)

---

## 1. 设计动机与实验目标

### 1.1 当前问题

当前工程版本已经具备以下能力：

- `AFL_LLM_VALIDATION`：开启 LLM 输出验证。
- `AFL_LLM_VALIDATION_STRICT`：开启格式、语法、上下文的完整验证。
- `AFL_LLM_FEEDBACK`：验证失败后让 LLM 根据错误原因重试。
- `AFL_LLM_POST_GAIN`：在 stall 消息执行后记录是否产生新覆盖、新状态或新状态转移。

这些机制能提高 LLM 输出的可控性，但当前实验暴露出一个核心矛盾：

```
执行前合法性 != 执行后 fuzzing 收益
```

也就是说，一个 LLM 生成的协议消息可以通过格式、语法和上下文验证，但执行后仍然不产生新覆盖、新状态或新状态转移。相反，某些不完全符合上下文规则的半合法输入，可能仍然有探索价值。

### 1.2 方案核心思想

本方案不继续把研究重点放在“更严格地过滤 LLM 输出”，而是将验证结果和执行后收益结果用于调度：

1. **Soft Validation**：验证失败不一定直接丢弃。对于 `GRAMMAR_FAIL`、`CONTEXT_FAIL` 等非格式污染类失败，按小概率进入 fuzzing 执行路径，保留协议 fuzzing 对半合法/非法状态的探索能力。
2. **PostGain Scheduler**：根据 LLM 生成消息执行后的收益结果动态调整后续 LLM 调用频率和 LLM seed 优先级，减少大量 `NO_GAIN` 调用对执行时间和 API 成本的浪费。
3. **组合策略**：用 Soft Validation 保留探索空间，用 PostGain Scheduler 控制探索成本。

### 1.3 实验目标

该方案的实验目标不是单纯证明“验证越严格覆盖率越高”，而是证明：

- 相比基线 ChatAFL，Soft Validation + PostGain Scheduler 能减少无收益 LLM 调用。
- 在不显著增加 hang 或垃圾种子污染的前提下，提高状态、状态转移或分支覆盖。
- 提高单位 LLM 调用带来的收益，例如 `LLM calls / new_state`、`LLM calls / new_transition`。

---

## 2. 五组实验变体定义

### 2.1 变体总览

| 变体 | 目的 | Validation | Soft Validation | PostGain | Scheduler |
|------|------|------------|-----------------|----------|-----------|
| `ChatAFL-base` | 原始 LLM-guided 基线 | 关闭或使用原始 `V0` | 关闭 | 关闭 | 关闭 |
| `+Hard Validation` | 当前严格验证方案 | FULL | 关闭 | 开启 | 关闭 |
| `+Soft Validation` | 验证失败分级准入 | FULL | 开启 | 开启 | 关闭 |
| `+PostGain Scheduler` | 收益感知 LLM 调度 | FULL | 关闭 | 开启 | 开启 |
| `+Soft Validation + PostGain Scheduler` | 主推组合方案 | FULL | 开启 | 开启 | 开启 |

### 2.2 ChatAFL-base

`ChatAFL-base` 用于回答：

> 不加入验证治理和收益调度时，LLM 引导的协议 fuzzing 表现如何？

实现建议：

- 保留 `ChatAFL-V0/` 作为论文原始代码或近似原始代码基线。
- 如果需要统一二进制，也可在 `ChatAFL/` 主目录中通过环境变量模拟：

```bash
AFL_LLM_VALIDATION=0
AFL_LLM_VALIDATION_STRICT=0
AFL_LLM_POST_GAIN=0
AFL_LLM_FEEDBACK=0
AFL_LLM_SOFT_VALIDATION=0
AFL_LLM_POST_GAIN_SCHEDULER=0
```

注意：如果 `ChatAFL-V0/` 与当前 `ChatAFL/` 主目录代码差异较大，论文中应明确区分：

- `V0-original-code`：原始或接近原始 ChatAFL 代码。
- `base-config`：当前主代码关闭验证后的配置基线。

### 2.3 Hard Validation

`+Hard Validation` 对应当前严格策略：

```bash
AFL_LLM_VALIDATION=1
AFL_LLM_VALIDATION_STRICT=1
AFL_LLM_POST_GAIN=1
AFL_LLM_FEEDBACK=1
AFL_LLM_SOFT_VALIDATION=0
AFL_LLM_POST_GAIN_SCHEDULER=0
```

行为：

- LLM 输出必须通过验证。
- 验证失败时优先触发 feedback retry。
- feedback 仍失败则丢弃。
- stall 阶段执行后记录 `ok` 或 `no_gain`。

该变体用于衡量当前验证层的效果和代价。

### 2.4 Soft Validation

`+Soft Validation` 配置：

```bash
AFL_LLM_VALIDATION=1
AFL_LLM_VALIDATION_STRICT=1
AFL_LLM_POST_GAIN=1
AFL_LLM_FEEDBACK=1
AFL_LLM_SOFT_VALIDATION=1
AFL_LLM_POST_GAIN_SCHEDULER=0
```

行为：

- `LLM_VALID_OK`：正常接受。
- `LLM_VALID_FORMAT_FAIL`：默认拒绝。
- `LLM_VALID_GRAMMAR_FAIL`：按低概率接受。
- `LLM_VALID_CONTEXT_FAIL`：按低概率接受。

该变体用于验证：

> 半合法输入是否能带来更多状态探索，而不会像完全无验证那样污染队列。

### 2.5 PostGain Scheduler

`+PostGain Scheduler` 配置：

```bash
AFL_LLM_VALIDATION=1
AFL_LLM_VALIDATION_STRICT=1
AFL_LLM_POST_GAIN=1
AFL_LLM_FEEDBACK=1
AFL_LLM_SOFT_VALIDATION=0
AFL_LLM_POST_GAIN_SCHEDULER=1
```

行为：

- 仍采用 hard validation。
- 但根据 stall 消息执行后收益调整下一阶段 LLM 调用频率。
- 连续 `NO_GAIN` 较多时，推迟下一次 LLM stall 调用。
- 出现 `OK` 收益时，恢复默认调用频率。

该变体用于验证：

> LLM 调用是否可以通过执行后收益反馈变得更经济。

### 2.6 Soft Validation + PostGain Scheduler

主推组合变体：

```bash
AFL_LLM_VALIDATION=1
AFL_LLM_VALIDATION_STRICT=1
AFL_LLM_POST_GAIN=1
AFL_LLM_FEEDBACK=1
AFL_LLM_SOFT_VALIDATION=1
AFL_LLM_POST_GAIN_SCHEDULER=1
```

预期效果：

- Soft Validation 保留 V0 式探索性。
- PostGain Scheduler 控制无收益 LLM 调用。
- 相比 Hard Validation，减少过度过滤。
- 相比 V0，减少明显垃圾输入污染。

---

## 3. 新增环境变量与运行时开关

### 3.1 Soft Validation 开关

| 环境变量 | 默认值 | 说明 |
|----------|--------|------|
| `AFL_LLM_SOFT_VALIDATION` | `0` | 是否启用软准入 |
| `AFL_LLM_SOFT_ACCEPT_RATE_FORMAT` | `0` | `FORMAT_FAIL` 接受概率，默认 0% |
| `AFL_LLM_SOFT_ACCEPT_RATE_GRAMMAR` | `5` | `GRAMMAR_FAIL` 接受概率，默认 5% |
| `AFL_LLM_SOFT_ACCEPT_RATE_CONTEXT` | `10` | `CONTEXT_FAIL` 接受概率，默认 10% |

概率单位为百分比整数，范围为 `0..100`。

推荐默认值：

```bash
AFL_LLM_SOFT_ACCEPT_RATE_FORMAT=0
AFL_LLM_SOFT_ACCEPT_RATE_GRAMMAR=5
AFL_LLM_SOFT_ACCEPT_RATE_CONTEXT=10
```

设计理由：

- `FORMAT_FAIL` 往往包含空输出、Markdown、拒绝话术、乱码或 CRLF 错误，污染风险最高。
- `GRAMMAR_FAIL` 可能是方法名或字段不完全符合验证器规则，但仍可能触发目标实现中的错误处理路径。
- `CONTEXT_FAIL` 可能违反理想协议状态顺序，但协议实现对非法状态的处理也可能产生新路径。

### 3.2 PostGain Scheduler 开关

| 环境变量 | 默认值 | 说明 |
|----------|--------|------|
| `AFL_LLM_POST_GAIN_SCHEDULER` | `0` | 是否启用收益感知调度 |
| `AFL_LLM_NOGAIN_BACKOFF_WINDOW` | `10` | 最近多少次 stall LLM 执行作为观察窗口 |
| `AFL_LLM_NOGAIN_BACKOFF_THRESHOLD` | `8` | 窗口内 no_gain 达到多少次触发降频 |
| `AFL_LLM_NOGAIN_BACKOFF_FACTOR` | `4` | 触发降频后 stall 阈值乘数 |
| `AFL_LLM_SEED_PRIORITY_OK` | `10` | 有收益 LLM seed 优先级 |
| `AFL_LLM_SEED_PRIORITY_NOGAIN` | `1` | 无收益 LLM seed 优先级 |
| `AFL_LLM_SEED_PRIORITY_SOFT` | `3` | soft accepted LLM seed 优先级 |

当前代码中 `UNINTERESTING_THRESHOLD=512`。启用 scheduler 后：

```c
dynamic_threshold = UNINTERESTING_THRESHOLD;

if (recent_no_gain >= AFL_LLM_NOGAIN_BACKOFF_THRESHOLD) {
  dynamic_threshold = UNINTERESTING_THRESHOLD * AFL_LLM_NOGAIN_BACKOFF_FACTOR;
}
```

例如默认配置下，连续窗口内 `NO_GAIN` 过多时，下一次 LLM stall 触发条件从：

```c
uninteresting_times >= 512
```

变为：

```c
uninteresting_times >= 2048
```

---

## 4. Soft Validation 软准入机制

### 4.1 当前 Hard Validation 逻辑

当前 enrichment 阶段的大致逻辑为：

```c
record.result = validate_llm_sequence_with_mode(...);
log_llm_validation_record(&record);

if (record.result != LLM_VALID_OK && !afl_llm_validation_permissive) {
  if (afl_llm_feedback) {
    recovered = llm_feedback_retry_enrichment(...);
    if (!recovered) discard;
  } else {
    discard;
  }
}
```

当前 stall 阶段的大致逻辑为：

```c
record.result = validate_llm_message_with_mode(...);

if (record.result != LLM_VALID_OK) {
  log_llm_validation_record(&record);
  if (!afl_llm_validation_permissive) {
    if (afl_llm_feedback) retry;
    else discard;
  }
}
```

Hard Validation 的问题是：

- 它能减少明显无效输入；
- 但也可能过滤掉对 fuzzing 有价值的半合法输入；
- 它只考虑执行前合法性，不考虑执行后收益。

### 4.2 Soft Validation 决策函数

建议新增内部函数：

```c
static u8 should_soft_accept_llm_result(llm_validation_result_t result) {
  if (!afl_llm_soft_validation) return 0;

  switch (result) {
    case LLM_VALID_FORMAT_FAIL:
      return UR(100) < afl_llm_soft_accept_rate_format;
    case LLM_VALID_GRAMMAR_FAIL:
      return UR(100) < afl_llm_soft_accept_rate_grammar;
    case LLM_VALID_CONTEXT_FAIL:
      return UR(100) < afl_llm_soft_accept_rate_context;
    default:
      return 0;
  }
}
```

其中：

- `UR(100)` 是 AFL 中常见的随机数函数。
- rate 为 0 时永不接受。
- rate 为 100 时总是接受。

### 4.3 Soft Validation 与 feedback retry 的顺序

推荐顺序：

```
原始 LLM 输出
  │
  ├─ 执行 validation
  │
  ├─ OK → 接受
  │
  └─ FAIL
       │
       ├─ feedback retry 开启 → 先尝试修复
       │     │
       │     ├─ 修复后 OK → 接受
       │     └─ 修复失败 / 仍 FAIL
       │
       └─ soft validation 开启 → 按失败类型概率准入
             │
             ├─ soft accept → 低优先级执行
             └─ soft reject → 丢弃
```

设计理由：

- 先 feedback retry，可以优先得到合法输入。
- retry 失败后再 soft accept，可以保留一小部分半合法探索机会。
- `FORMAT_FAIL` 默认不 soft accept，降低垃圾输入进入队列的概率。

### 4.4 Enrichment 阶段软准入

Enrichment 生成的是完整请求序列，会写入 `enriched_*` 种子文件。

建议策略：

| 验证结果 | 行为 |
|----------|------|
| `OK` | 写入 enriched seed |
| `FORMAT_FAIL` | 默认丢弃 |
| `GRAMMAR_FAIL` | 小概率写入，文件名前缀标记 `soft_grammar_` |
| `CONTEXT_FAIL` | 小概率写入，文件名前缀标记 `soft_context_` |

示例文件名：

```text
enriched_12_seed.raw
soft_grammar_enriched_13_seed.raw
soft_context_enriched_14_seed.raw
```

如果不希望改动文件命名，也必须在 `llm-validation/enrichment.csv` 中记录：

```text
reason=soft_accept:grammar_fail
reason=soft_accept:context_fail
```

### 4.5 Stall 阶段软准入

Stall 生成的是下一条客户端请求，直接进入 `common_fuzz_stuff()` 执行。

建议策略：

| 验证结果 | 行为 |
|----------|------|
| `OK` | 执行，优先级正常 |
| `FORMAT_FAIL` | 默认丢弃 |
| `GRAMMAR_FAIL` | 小概率执行，标记低优先级 LLM seed |
| `CONTEXT_FAIL` | 小概率执行，标记低优先级 LLM seed |

软准入执行前：

```c
mark_next_seed_as_llm = 1;
next_seed_llm_priority = afl_llm_seed_priority_soft;  // 默认 3
```

这样即使 soft accepted 输入进入队列，也不会像高收益 LLM seed 一样被过度优先选择。

---

## 5. PostGain Scheduler 收益感知调度

### 5.1 当前 post-gain 逻辑

当前 stall 执行后会调用：

```c
fill_post_execution_record(&record, queued_before, state_before, edge_before);
```

该函数记录：

- `has_new_cov`
- `has_new_state`
- `has_new_transition`
- `fault`
- `exec_us`

然后调用：

```c
classify_llm_execution_gain(
  record->has_new_cov,
  record->has_new_state,
  record->has_new_transition
)
```

分类结果：

| 结果 | 含义 |
|------|------|
| `LLM_VALID_OK` | 执行后产生新覆盖、新状态或新状态转移 |
| `LLM_VALID_NO_GAIN` | 执行后没有产生上述收益 |

当前 post-gain 主要用于日志归因，还没有反向影响后续调度。

### 5.2 Scheduler 的目标

PostGain Scheduler 的目标是将 post-gain 从“观察指标”变成“控制信号”：

- 如果近期 LLM stall 多次无收益，则降低 LLM stall 调用频率。
- 如果某次 LLM stall 产生收益，则恢复调用频率。
- 对有收益 LLM seed 提高优先级。
- 对无收益 LLM seed 降低优先级。

### 5.3 动态 stall 触发阈值

当前 stall 触发条件：

```c
if (uninteresting_times >= UNINTERESTING_THRESHOLD &&
    chat_times < CHATTING_THRESHOLD) {
  // call LLM
}
```

建议改为：

```c
if (uninteresting_times >= afl_llm_dynamic_uninteresting_threshold &&
    chat_times < CHATTING_THRESHOLD) {
  // call LLM
}
```

新增变量：

```c
static u32 afl_llm_dynamic_uninteresting_threshold = UNINTERESTING_THRESHOLD;
```

调度规则：

```c
if (post_gain_result == LLM_VALID_NO_GAIN) {
  recent_no_gain++;
}

if (recent_no_gain >= AFL_LLM_NOGAIN_BACKOFF_THRESHOLD) {
  afl_llm_dynamic_uninteresting_threshold =
      UNINTERESTING_THRESHOLD * AFL_LLM_NOGAIN_BACKOFF_FACTOR;
}

if (post_gain_result == LLM_VALID_OK) {
  afl_llm_dynamic_uninteresting_threshold = UNINTERESTING_THRESHOLD;
  reset_recent_window();
}
```

### 5.4 滑动窗口统计

建议用固定长度环形缓冲记录最近 N 次 stall LLM 执行结果：

```c
#define LLM_GAIN_WINDOW_MAX 128
static u8 llm_gain_window[LLM_GAIN_WINDOW_MAX];
static u32 llm_gain_window_size;
static u32 llm_gain_window_pos;
static u32 llm_gain_window_count;
static u32 llm_gain_window_no_gain;
```

记录规则：

```c
// 1 表示 gain，0 表示 no_gain
update_llm_gain_window(record.result == LLM_VALID_OK);
```

这样可以避免全局累计 no_gain 导致调度器永久降频。

### 5.5 LLM seed 优先级调整

当前 stall 执行前固定设置：

```c
mark_next_seed_as_llm = 1;
next_seed_llm_priority = 8;
```

建议改为：

```c
mark_next_seed_as_llm = 1;

if (soft_accepted) {
  next_seed_llm_priority = afl_llm_seed_priority_soft;      // 默认 3
} else if (post_gain_result == LLM_VALID_OK) {
  next_seed_llm_priority = afl_llm_seed_priority_ok;        // 默认 10
} else if (post_gain_result == LLM_VALID_NO_GAIN) {
  next_seed_llm_priority = afl_llm_seed_priority_no_gain;   // 默认 1
} else {
  next_seed_llm_priority = 8;
}
```

注意：由于 `save_if_interesting()` 只有在输入被保存进队列时才会调用 `add_to_queue()`，post-gain no_gain 通常不会新增队列项。因此 no_gain priority 主要用于防御性处理和未来扩展；真正关键的是对 gain seed 提高优先级。

---

## 6. 系统运行逻辑与数据流

### 6.1 启动阶段

```
./run.sh
  │
  ├─ 解析 TARGET / FUZZER / TIMEOUT
  │
  ├─ 透传 AFL_LLM_* 环境变量
  │
  └─ profuzzbench_exec_all.sh
       │
       └─ profuzzbench_exec_common.sh
            │
            └─ docker run ...
                 │
                 └─ source ${FUZZER}/env.sh
                      │
                      └─ afl-fuzz main()
                           │
                           ├─ 读取原有 validation / feedback / post-gain 开关
                           ├─ 读取新增 soft validation 开关
                           ├─ 读取新增 post-gain scheduler 开关
                           ├─ setup_llm_grammars()
                           ├─ enrich_testcases()
                           └─ fuzz loop
```

### 6.2 Enrichment 阶段数据流

```
LLM 生成 enriched sequence
  │
  ├─ clean / format
  │
  ├─ validate_llm_sequence_with_mode()
  │
  ├─ OK
  │    └─ 写入 enriched seed
  │
  └─ FAIL
       │
       ├─ feedback retry
       │    ├─ recovered OK → 写入 enriched seed
       │    └─ recovered FAIL / NULL
       │
       └─ Soft Validation
            ├─ FORMAT_FAIL → 默认丢弃
            ├─ GRAMMAR_FAIL → 低概率写入 soft seed
            └─ CONTEXT_FAIL → 低概率写入 soft seed
```

### 6.3 Stall 阶段数据流

```
uninteresting_times 达到动态阈值
  │
  ├─ 构造 history + examples prompt
  │
  ├─ LLM 生成下一条请求
  │
  ├─ validate_llm_message_with_mode()
  │
  ├─ OK
  │    └─ common_fuzz_stuff()
  │         └─ fill_post_execution_record()
  │              ├─ OK → 更新 scheduler，提升 seed priority
  │              └─ NO_GAIN → 更新 scheduler，可能 backoff
  │
  └─ FAIL
       │
       ├─ feedback retry
       │
       └─ Soft Validation
            ├─ soft accept → common_fuzz_stuff()
            └─ soft reject → skip
```

### 6.4 Scheduler 与 fuzz loop 的关系

```
fuzz_one()
  │
  ├─ 常规 AFL/AFLNet 变异
  │
  ├─ common_fuzz_stuff()
  │    ├─ save_if_interesting()
  │    ├─ interesting → uninteresting_times = 0
  │    └─ not interesting → uninteresting_times++
  │
  └─ if uninteresting_times >= dynamic_threshold
       └─ call LLM stall handler
```

Scheduler 不改变 AFLNet 的基础状态选择逻辑，只改变：

- 何时调用 LLM stall handler；
- LLM seed 被加入队列后具有什么优先级；
- soft accepted 输入是否以低概率进入执行路径。

---

## 7. 实现接入点与代码级修改范围

### 7.1 `afl-fuzz.c` 全局变量

新增运行时开关：

```c
u8 afl_llm_soft_validation = 0;
u32 afl_llm_soft_accept_rate_format = 0;
u32 afl_llm_soft_accept_rate_grammar = 5;
u32 afl_llm_soft_accept_rate_context = 10;

u8 afl_llm_post_gain_scheduler = 0;
u32 afl_llm_nogain_backoff_window = 10;
u32 afl_llm_nogain_backoff_threshold = 8;
u32 afl_llm_nogain_backoff_factor = 4;
u32 afl_llm_dynamic_uninteresting_threshold = UNINTERESTING_THRESHOLD;

u32 afl_llm_seed_priority_ok = 10;
u32 afl_llm_seed_priority_nogain = 1;
u32 afl_llm_seed_priority_soft = 3;
```

### 7.2 `main()` 环境变量读取

在当前读取以下变量的位置附近扩展：

```c
afl_llm_validation = env_flag_enabled("AFL_LLM_VALIDATION");
afl_llm_validation_strict = env_flag_enabled("AFL_LLM_VALIDATION_STRICT");
afl_llm_post_gain = env_flag_enabled("AFL_LLM_POST_GAIN");
```

新增读取：

```c
afl_llm_soft_validation = env_flag_enabled("AFL_LLM_SOFT_VALIDATION");
afl_llm_post_gain_scheduler = env_flag_enabled("AFL_LLM_POST_GAIN_SCHEDULER");

afl_llm_soft_accept_rate_format =
    env_u32_or_default("AFL_LLM_SOFT_ACCEPT_RATE_FORMAT", 0, 0, 100);
afl_llm_soft_accept_rate_grammar =
    env_u32_or_default("AFL_LLM_SOFT_ACCEPT_RATE_GRAMMAR", 5, 0, 100);
afl_llm_soft_accept_rate_context =
    env_u32_or_default("AFL_LLM_SOFT_ACCEPT_RATE_CONTEXT", 10, 0, 100);
```

建议新增工具函数：

```c
static u32 env_u32_or_default(const char *name, u32 def, u32 min, u32 max);
```

### 7.3 Enrichment 接入点

当前接入点：

```c
record.result = validate_llm_sequence_with_mode(...);
fill_validation_reason(&record, record.result, "enrichment_validation");
log_llm_validation_record(&record);
```

建议改造为：

```c
if (record.result != LLM_VALID_OK && !afl_llm_validation_permissive) {
  recovered = try_feedback_if_enabled(...);

  if (!recovered) {
    if (should_soft_accept_llm_result(record.result)) {
      fill_validation_reason(&record, record.result, "soft_accept");
      log_llm_validation_record(&record);
      accept_as_soft_seed = 1;
    } else {
      fill_validation_reason(&record, record.result, "soft_reject");
      log_llm_validation_record(&record);
      discard;
    }
  }
}
```

### 7.4 Stall 接入点

当前接入点：

```c
if (uninteresting_times >= UNINTERESTING_THRESHOLD &&
    chat_times < CHATTING_THRESHOLD)
```

建议改为：

```c
if (uninteresting_times >= afl_llm_dynamic_uninteresting_threshold &&
    chat_times < CHATTING_THRESHOLD)
```

验证失败时：

```c
if (record.result != LLM_VALID_OK) {
  if (feedback_recovered) {
    // use recovered
  } else if (should_soft_accept_llm_result(record.result)) {
    soft_accepted = 1;
    fill_validation_reason(&record, record.result, "soft_accept");
  } else {
    fill_validation_reason(&record, record.result, "soft_reject");
    discard;
  }
}
```

执行后：

```c
fill_post_execution_record(&record, queued_before, state_before, edge_before);
log_llm_validation_record(&record);

if (afl_llm_post_gain_scheduler) {
  update_post_gain_scheduler(record.result);
}
```

### 7.5 `llm-validation/*.csv` 日志扩展

当前日志字段已经包含：

```text
time,stage,protocol,seed_id,llm_call_id,result,reason,input_bytes,
normalized_bytes,region_count,state_count,response_code_seq,
new_cov,new_state,new_transition,fault,exec_us
```

为避免破坏已有分析脚本，建议不改变字段结构，只扩展 `reason` 值：

| reason | 含义 |
--------|------|
| `soft_accept:grammar_fail` | 语法失败但被软准入 |
| `soft_accept:context_fail` | 上下文失败但被软准入 |
| `soft_reject:format_fail` | 格式失败且被拒绝 |
| `soft_reject:grammar_fail` | 语法失败且未通过软准入 |
| `soft_reject:context_fail` | 上下文失败且未通过软准入 |
| `scheduler_backoff:no_gain` | 近期 no_gain 过多，调高 stall 触发阈值 |
| `scheduler_recover:ok` | 出现收益，恢复默认 stall 触发阈值 |

---

## 8. 示例：RTSP Stall 消息的完整处理过程

### 8.1 场景设定

当前 fuzzing 已经连续 512 次没有发现 interesting 输入：

```c
uninteresting_times = 512;
afl_llm_dynamic_uninteresting_threshold = 512;
```

触发 stall breaking。

已有通信历史：

```text
OPTIONS rtsp://127.0.0.1:8554/test.mpg RTSP/1.0
CSeq: 1

RTSP/1.0 200 OK
CSeq: 1
Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN
```

LLM 生成：

```text
PLAY rtsp://127.0.0.1:8554/test.mpg RTSP/1.0
CSeq: 2

```

### 8.2 Hard Validation 下的处理

RTSP 的 `PLAY` 通常需要 `Session` header。当前上下文没有经过 `SETUP` 获得 session，因此完整验证返回：

```text
LLM_VALID_CONTEXT_FAIL
```

Hard Validation 行为：

1. 记录 `stall_validation:context_fail`。
2. 调用 feedback retry。
3. 如果 LLM 修复为带 `Session` 的消息且通过验证，则执行。
4. 如果修复失败，则丢弃。

风险：

- 丢弃该输入可能错过服务端对非法 `PLAY` 的错误处理路径。

### 8.3 Soft Validation 下的处理

Soft Validation 配置：

```bash
AFL_LLM_SOFT_ACCEPT_RATE_CONTEXT=10
```

处理流程：

1. `PLAY` 缺少 `Session`，验证结果为 `CONTEXT_FAIL`。
2. feedback retry 失败或未产生合法修复。
3. 进入 soft validation。
4. 以 10% 概率执行该消息。
5. 如果被接受，记录：

```text
reason=soft_accept:context_fail
```

6. 设置低优先级：

```c
mark_next_seed_as_llm = 1;
next_seed_llm_priority = 3;
```

7. 执行 `common_fuzz_stuff()`。

可能结果：

| 执行结果 | post-gain 分类 | 后续调度 |
----------|----------------|----------|
| 触发新错误处理路径 | `OK` | 提高 LLM seed 优先级，恢复默认阈值 |
| 没有新路径 | `NO_GAIN` | 更新 no_gain 窗口，可能 backoff |
| 导致 hang | fault 记录为 hang | 作为风险指标计入实验 |

### 8.4 PostGain Scheduler 下的处理

假设最近 10 次 LLM stall 中有 8 次都是 `NO_GAIN`：

```text
recent_no_gain = 8
window = 10
threshold = 8
```

Scheduler 触发 backoff：

```c
afl_llm_dynamic_uninteresting_threshold = 512 * 4;
```

下一次 LLM stall 只有在连续 2048 次无趣执行后才触发。

这样可以减少在当前状态附近反复询问 LLM 却没有收益的情况。

---

## 9. 日志、指标与实验分析方法

### 9.1 必须保留的基础指标

来自 `fuzzer_stats`：

- `execs_done`
- `execs_per_sec`
- `paths_total`
- `unique_crashes`
- `unique_hangs`
- `bitmap_cvg`

来自 `plot_data`：

- `n_nodes`
- `n_edges`
- `chat_times`
- 覆盖率随时间变化

来自 `llm-validation/*.csv`：

- `result`
- `reason`
- `new_cov`
- `new_state`
- `new_transition`
- `fault`
- `exec_us`

### 9.2 新方案核心指标

| 指标 | 计算方式 | 解释 |
------|----------|------|
| `NO_GAIN ratio` | `stall_post_exec:no_gain / stall_post_exec total` | LLM stall 无收益比例 |
| `LLM calls per new_cov` | `chat_times / sum(new_cov)` | 每个新覆盖需要多少次 LLM 调用 |
| `LLM calls per new_state` | `chat_times / sum(new_state)` | 每个新状态需要多少次 LLM 调用 |
| `LLM calls per new_transition` | `chat_times / sum(new_transition)` | 每个新状态转移需要多少次 LLM 调用 |
| `soft_accept gain rate` | `soft_accept 且 post-gain OK / soft_accept 总数` | 软准入输入的真实收益率 |
| `backoff trigger count` | `scheduler_backoff:no_gain` 次数 | Scheduler 是否有效触发 |
| `recover count` | `scheduler_recover:ok` 次数 | Scheduler 是否能恢复 |

### 9.3 实验对比方式

推荐正式实验：

```bash
./run.sh 5 800 live555 chatafl-base,chatafl-hard,chatafl-soft,chatafl-pg,chatafl-soft-pg paper_soft_pg
./run.sh 5 800 pure-ftpd chatafl-base,chatafl-hard,chatafl-soft,chatafl-pg,chatafl-soft-pg paper_soft_pg
```

其中：

- `5` 表示每组 5 次重复。
- `800` 表示 800 分钟，不是 800 小时。
- 每组结果报告均值、标准差、最好值和最差值。

### 9.4 结果解释标准

如果 `soft-pg` 相比 hard validation：

- `NO_GAIN ratio` 下降；
- `execs_per_sec` 上升或基本不下降；
- `n_edges`、`n_nodes`、branch coverage 至少一个提升；

则说明收益调度有效。

如果 `soft` 相比 hard validation：

- hang 不显著增加；
- `n_edges` 或 paths 增加；

则说明半合法输入保留了探索价值。

如果 `pg` 相比 hard validation：

- 覆盖率变化不大；
- 但 `LLM calls per new_state` 降低；

仍可作为成本收益改进。

---

## 10. 预期收益、风险与消融解释边界

### 10.1 预期收益排序

| 方案 | 预期收益 | 原因 |
------|----------|------|
| `Soft + PostGain` | 最高 | 同时保留探索性和控制 LLM 成本 |
| `PostGain Scheduler` | 高 | 直接针对大量 `NO_GAIN` 调用 |
| `Soft Validation` | 中高 | 缓解 hard validation 过度过滤 |
| `Hard Validation` | 中 | 提高输入质量，但可能降低探索性 |
| `Base / V0` | 不稳定 | 探索激进，但污染和 hang 风险高 |

### 10.2 主要风险

#### 风险一：Soft Validation 增加 hang

缓解：

- `FORMAT_FAIL` 默认不接受。
- `CONTEXT_FAIL` 接受率保持 10%。
- 如果 hang 明显增加，将 `AFL_LLM_SOFT_ACCEPT_RATE_CONTEXT` 降至 5%。

#### 风险二：Scheduler 过度降频，错过 LLM 有效时机

缓解：

- 使用滑动窗口，不使用永久累计。
- 一旦出现 `post-gain OK`，立即恢复默认阈值。
- backoff factor 默认 4，不建议超过 8。

#### 风险三：覆盖率提升不明显

缓解：

- 报告单位 LLM 调用收益。
- 报告状态节点和状态转移，而不是只报告 line coverage。
- 报告 no-gain ratio 和 soft accept gain rate。

### 10.3 不能声称的结论

以下结论不能直接声称：

- Soft Validation 一定提高代码覆盖率。
- Hard Validation 一定优于无验证。
- `LLM_VALID_OK` 表示该输入对 fuzzing 有收益。
- `LLM_VALID_NO_GAIN` 表示该输入非法。
- PostGain Scheduler 本身发现了新漏洞。

更合理的论文表述是：

> 本方案将 LLM 输出验证从二元过滤扩展为分级准入，并将执行后收益用于调节 LLM 调用频率和 seed 优先级，从而降低无收益 LLM 交互比例，提高 LLM-guided protocol fuzzing 的有效探索效率。

### 10.4 推荐落地顺序

1. 增加环境变量读取和默认配置。
2. 实现 `should_soft_accept_llm_result()`。
3. 在 enrichment 和 stall 两处接入 soft validation。
4. 实现 post-gain 滑动窗口。
5. 将 stall 触发阈值替换为动态阈值。
6. 根据 post-gain 结果调整 LLM seed priority。
7. 扩展 `env.sh` 或新增 fuzzer 包装目录。
8. 跑 30-60 分钟 smoke test。
9. 跑 Live555 和 Pure-FTPD 的 5 次重复正式实验。

---

## 附录：关键代码索引

| 位置 | 作用 |
------|------|
| `ChatAFL/afl-fuzz.c` | 主 fuzzing 流程、enrichment、stall、post-gain 接入 |
| `ChatAFL/llm-validator.c` | LLM 输出验证、验证日志、post-gain 分类 |
| `ChatAFL/llm-validator.h` | 验证结果枚举和日志结构 |
| `ChatAFL/config.h` | `UNINTERESTING_THRESHOLD`、`CHATTING_THRESHOLD` |
| `ChatAFL/env.sh` | 完整版环境变量默认配置 |
| `ChatAFL-V0/env.sh` | 无验证基线配置 |
| `ChatAFL-V1/env.sh` | 格式验证配置 |
| `ChatAFL-V2/env.sh` | 完整验证配置 |
| `benchmark/scripts/execution/profuzzbench_exec_all.sh` | fuzzer 变体分发 |
| `benchmark/scripts/execution/profuzzbench_exec_common.sh` | Docker 环境变量透传 |

