# ChatAFL 前四组实验计划：真实基线、PromptClean、Hard Validation、Soft Validation

> 本文基于当前仓库代码状态制定实验方案。目标不是完整铺开所有 Soft/PostGain 组合，而是先完成 4 个必要组：真实 `ChatAFL-base`、当前 `V0 / PromptClean`、已有 `Hard Validation`、以及最有论文价值的 `Soft Validation`。

## 1. 核心判断

当前仓库中存在三个容易混淆的层次：

| 组别 | 当前代码位置 | 当前是否可直接跑 | 关键说明 |
|---|---|---:|---|
| 真实 `ChatAFL-base` | `baseline/ChatAFL/ChatAFL/` | 否 | 这是论文原始 ChatAFL 风格代码，但还没有作为 `chatafl-base` 接入 ProfuzzBench 脚本和 Dockerfile。 |
| `V0 / PromptClean` | `ChatAFL-V0/`，Docker 内为 `chatafl-v0` | 是 | 不是严格原始 ChatAFL。它关闭 validation/feedback/post-gain，但 `chat-llm.c` 已有 endpoint、response clean、prompt 兼容等改动。 |
| `Hard Validation` | `ChatAFL-V2/`，Docker 内为 `chatafl-v2` | 是 | `AFL_LLM_VALIDATION=1`、`AFL_LLM_VALIDATION_STRICT=1`、`AFL_LLM_POST_GAIN=0`；注意 feedback 默认会随 validation 自动开启。 |
| `Soft Validation` | 需新增变体，例如 `ChatAFL-Soft/` 或 `chatafl-soft` | 否 | 不能直接用 `AFL_LLM_VALIDATION_PERMISSIVE=1` 代替，因为 permissive 是失败全放行，不是按类别低概率接收。 |

因此，实验主线应是：

```text
真实 ChatAFL-base
  -> 当前 V0 / PromptClean
  -> Hard Validation
  -> Soft Validation
```

这条主线可以回答三个问题：

1. Prompt/IO 清洗相对原始 ChatAFL 是否已有收益。
2. 严格验证是否提升输入质量，还是牺牲探索性。
3. Soft Validation 是否能找回 Hard Validation 丢弃的边界输入价值。

## 2. 实验组定义

### 2.1 组 A：真实 ChatAFL-base

**目的**：建立真正的原始 ChatAFL 对照，避免把当前 V0 误称为基准论文原始实现。

**代码来源**：

```text
baseline/ChatAFL/ChatAFL/
```

该目录中的 `afl-fuzz.c` 和 `chat-llm.c` 没有当前验证层、feedback retry、post-gain 归因、统一 response clean 等扩展。

**需要的工程接入**：

1. 在 `benchmark/subjects/RTSP/Live555/` 下新增 `chatafl-base/`，内容来自 `baseline/ChatAFL/ChatAFL/`。
2. 在 `benchmark/subjects/FTP/PureFTPD/` 下新增 `chatafl-base/`，内容同上。
3. 给 `chatafl-base/env.sh` 设置最小环境，原则上不启用当前扩展：

```bash
#!/bin/bash
# Original ChatAFL-style baseline.
export AFL_LLM_VALIDATION=0
export AFL_LLM_VALIDATION_STRICT=0
export AFL_LLM_POST_GAIN=0
export AFL_LLM_FEEDBACK=0
```

4. 修改 `benchmark/subjects/RTSP/Live555/Dockerfile` 和 `benchmark/subjects/FTP/PureFTPD/Dockerfile`：

```dockerfile
COPY --chown=ubuntu:ubuntu chatafl-base chatafl-base
RUN cd chatafl-base && \
    make clean all $MAKE_OPT && \
    cd llvm_mode && make $MAKE_OPT
```

并把 env 复制循环从：

```bash
for f in chatafl chatafl-v0 chatafl-v1 chatafl-v2; do
```

扩展为：

```bash
for f in chatafl-base chatafl chatafl-v0 chatafl-v1 chatafl-v2; do
```

5. 修改 `benchmark/scripts/execution/profuzzbench_exec_all.sh`，为 `pure-ftpd` 和 `live555` 增加 `chatafl-base` 分支：

```bash
if [[ $FUZZER == "chatafl-base" ]] || [[ $FUZZER == "all" ]]
then
  profuzzbench_exec_common.sh live555 $NUM_CONTAINERS results-live555${RESULT_SUFFIX} \
    chatafl-base out-live555-chatafl_base \
    "-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none" $TIMEOUT $SKIPCOUNT &
fi
```

FTP 同理，输出目录建议为：

```text
out-pure-ftpd-chatafl_base
```

**实验命令示例**：

```bash
./run.sh 10 720 live555,pure-ftpd chatafl-base exp_base_01
```

**主要指标**：

| 指标 | 作用 |
|---|---|
| line / branch coverage | 原始 ChatAFL 的最终覆盖率基准 |
| paths_total | AFL 队列规模 |
| execs_done / execs_per_sec | 原始执行效率 |
| unique_hangs / unique_crashes | 原始异常行为 |
| chat_times | 原始 LLM 触发频率 |

## 3. 组 B：当前 V0 / PromptClean

**目的**：隔离 prompt、LLM endpoint 适配、response clean 等工程改动的收益。

**代码来源**：

```text
ChatAFL-V0/
benchmark/subjects/RTSP/Live555/chatafl-v0/
benchmark/subjects/FTP/PureFTPD/chatafl-v0/
```

**当前配置**：

`ChatAFL-V0/env.sh`：

```bash
AFL_LLM_VALIDATION=0
AFL_LLM_VALIDATION_STRICT=0
AFL_LLM_POST_GAIN=0
AFL_LLM_FEEDBACK=0
```

**注意**：V0 不是严格原始 ChatAFL。和真实 baseline 相比，V0 的 `chat-llm.c` 已经包含：

- `LLM_URL`、`LLM_TOKEN`、`LLM_MODEL` 环境变量适配；
- 统一 chat endpoint 逻辑；
- `clean_llm_response()`；
- JSON/markdown/CRLF 清理；
- prompt 输出约束增强。

所以论文中应命名为：

```text
ChatAFL-PromptClean
```

而不是直接称为：

```text
Original ChatAFL
```

**实验命令示例**：

```bash
./run.sh 10 720 live555,pure-ftpd chatafl-v0 exp_promptclean_01
```

**对比关系**：

```text
V0 / PromptClean vs ChatAFL-base
```

回答问题：

```text
当前 prompt/IO 清洗是否已经带来覆盖率、稳定性或执行效率变化？
```

**判定方式**：

| 现象 | 解释 |
|---|---|
| V0 覆盖率高于 base | prompt/IO 清洗本身有收益，后续验证层收益必须扣除这部分。 |
| V0 与 base 持平 | prompt/IO 清洗主要是工程兼容，不是主要收益来源。 |
| V0 低于 base | 当前模型适配或清洗可能改变了原始 ChatAFL 的生成分布，需要谨慎。 |

## 4. 组 C：Hard Validation

**目的**：评估严格验证的代价与收益。

**代码来源**：

```text
ChatAFL-V2/
benchmark/subjects/RTSP/Live555/chatafl-v2/
benchmark/subjects/FTP/PureFTPD/chatafl-v2/
```

**当前配置**：

`ChatAFL-V2/env.sh`：

```bash
AFL_LLM_VALIDATION=1
AFL_LLM_VALIDATION_STRICT=1
AFL_LLM_POST_GAIN=0
```

`afl-fuzz.c` 中存在自动开启逻辑：

```c
if (getenv("AFL_LLM_FEEDBACK")) {
  afl_llm_feedback = env_flag_enabled("AFL_LLM_FEEDBACK");
} else if (afl_llm_validation) {
  afl_llm_feedback = 1;
}
```

因此，当前 `chatafl-v2` 实际是：

```text
FULL validation + auto feedback retry + no post-gain
```

不是纯粹的：

```text
FULL validation only
```

**建议使用方式**：

如果已有 V2 实验结果，可以作为 Hard Validation 主结果使用，但论文中必须写清楚它包含自动 feedback。

如果要严格拆分“硬验证本身”的贡献，应额外跑：

```bash
AFL_LLM_FEEDBACK=0 ./run.sh 10 720 live555,pure-ftpd chatafl-v2 exp_hard_nofeedback_01
```

不过这不是前四组的必要项，可以作为补充实验。

**实验命令示例**：

```bash
./run.sh 10 720 live555,pure-ftpd chatafl-v2 exp_hard_01
```

**对比关系**：

```text
Hard Validation vs V0 / PromptClean
```

回答问题：

```text
严格验证是否减少无效 LLM 输出、hang 和 no_gain，同时是否牺牲覆盖率探索？
```

**重点指标**：

| 指标 | 解释 |
|---|---|
| validation pass rate | LLM 输出通过率 |
| format_fail / grammar_fail / context_fail | 被拦截的失败类型分布 |
| feedback recovery rate | feedback 修复失败候选的能力 |
| unique_hangs | 严格验证是否减少 hang |
| line/branch coverage | 严格验证是否牺牲覆盖 |
| paths_total | 队列是否被过度压缩 |

**预期结论边界**：

Hard Validation 如果覆盖率没有提升，也不代表验证层无价值。它可能仍然提升了输入合法性、降低 hang、提升 LLM 输出可解释性。论文表述应避免只用 coverage 评价。

## 5. 组 D：Soft Validation

**目的**：验证被 Hard Validation 丢弃的候选中，是否存在对状态机探索有价值的边界输入。

### 5.1 为什么不能直接用 permissive

当前代码已有：

```c
u8 afl_llm_validation_permissive = 0;
```

并在关键分支中使用：

```c
if (record.result != LLM_VALID_OK && !afl_llm_validation_permissive) {
  ...
}
```

这意味着 `AFL_LLM_VALIDATION_PERMISSIVE=1` 会让验证失败候选全部继续进入后续路径。它不是 Soft Validation，而是：

```text
log-only permissive validation
```

风险是噪声过大，尤其 `FORMAT_FAIL` 大概率会被服务端直接拒绝。

### 5.2 Soft Validation 的正确实验定义

Soft Validation 应满足三点：

1. 验证失败不立即全部丢弃；
2. 按失败类型设置不同 admission probability；
3. 被 soft 接收的候选必须低能量、可标记、可统计。

建议新增环境变量：

```bash
AFL_LLM_SOFT_VALIDATION=1
AFL_LLM_SOFT_FORMAT_PROB=0
AFL_LLM_SOFT_GRAMMAR_PROB=5
AFL_LLM_SOFT_CONTEXT_PROB=30
AFL_LLM_SOFT_SEED_PRIORITY=2
```

概率用整数百分比，避免浮点依赖。

推荐默认策略：

| 失败类型 | admission probability | 原因 |
---|---:|---|
| `LLM_VALID_FORMAT_FAIL` | 0% 到 3% | 格式错误通常不能被目标解析，优先丢弃。 |
| `LLM_VALID_GRAMMAR_FAIL` | 5% 到 10% | 非法方法/字段可能触发错误处理路径，但噪声较大。 |
| `LLM_VALID_CONTEXT_FAIL` | 25% 到 35% | 语法合法但状态上下文不满足，最可能探索状态机错误处理。 |

### 5.3 代码接入点

Soft Validation 需要修改当前 `ChatAFL/afl-fuzz.c` 或派生 `ChatAFL-Soft/afl-fuzz.c`。关键位置有三处。

**Grammar 阶段**：

```text
ChatAFL/afl-fuzz.c
record.result != LLM_VALID_OK && !afl_llm_validation_permissive
```

当前在 grammar pattern 失败时会跳过该 pattern。Soft 方案建议：

- grammar pattern 失败仍默认丢弃；
- 不建议对 grammar pattern 启用 soft admission，因为错误 pattern 会污染后续区域解析和 havoc exploit。

**Enrichment 阶段**：

```text
validate_llm_sequence_with_mode(...)
if (record.result != LLM_VALID_OK && !afl_llm_validation_permissive) { ... }
```

建议改为：

```c
if (record.result != LLM_VALID_OK) {
  if (afl_llm_feedback) {
    recovered = llm_feedback_retry_enrichment(...);
  }

  if (!recovered && !should_soft_accept(record.result, LLM_STAGE_ENRICHMENT)) {
    continue;
  }
}
```

并在 soft 接收时记录：

```text
reason = enrichment_soft_accept:context_fail
llm_priority = AFL_LLM_SOFT_SEED_PRIORITY
```

**Stall 阶段**：

```text
validate_llm_message_with_mode(...)
if (record.result != LLM_VALID_OK) { ... }
```

建议逻辑：

1. 先尝试 feedback retry；
2. feedback 失败后按类别 soft admission；
3. soft 接收的消息允许执行一次；
4. 如果执行后无收益，不提升优先级，不扩大能量。

### 5.4 soft seed 的能量控制

当前代码已有 LLM seed 标记：

```c
q->is_llm_seed = mark_next_seed_as_llm;
q->llm_priority = mark_next_seed_as_llm ? next_seed_llm_priority : 0;
```

以及 enriched seed 标记：

```c
if (strstr(nl[i]->d_name, "enriched_") == nl[i]->d_name) {
  queue_top->is_llm_seed = 1;
  queue_top->llm_priority = 10;
}
```

Soft 方案不要沿用高优先级 8 或 10。建议：

```text
hard-ok enriched seed: llm_priority = 10
hard-ok stall seed:    llm_priority = 8
soft-accepted seed:    llm_priority = 1 或 2
```

否则 soft 噪声可能挤占正常种子。

如果实现成本允许，建议新增字段：

```c
u8 is_soft_llm_seed;
llm_validation_result_t soft_origin_result;
```

这样后续可以统计：

```text
soft_context_fail -> new_cov?
soft_grammar_fail -> new_state?
soft_format_fail -> no_gain?
```

### 5.5 Soft Validation 实验命名

建议新增变体：

```text
ChatAFL-Soft/
benchmark/subjects/RTSP/Live555/chatafl-soft/
benchmark/subjects/FTP/PureFTPD/chatafl-soft/
```

`env.sh`：

```bash
#!/bin/bash
# ChatAFL-Soft: full validation + feedback + low-probability admission for selected validation failures.
: "${AFL_LLM_VALIDATION:=1}"
: "${AFL_LLM_VALIDATION_STRICT:=1}"
: "${AFL_LLM_POST_GAIN:=0}"
: "${AFL_LLM_FEEDBACK:=1}"
: "${AFL_LLM_FEEDBACK_MAX_RETRIES:=3}"
: "${AFL_LLM_SOFT_VALIDATION:=1}"
: "${AFL_LLM_SOFT_FORMAT_PROB:=0}"
: "${AFL_LLM_SOFT_GRAMMAR_PROB:=5}"
: "${AFL_LLM_SOFT_CONTEXT_PROB:=30}"
: "${AFL_LLM_SOFT_SEED_PRIORITY:=2}"
export AFL_LLM_VALIDATION AFL_LLM_VALIDATION_STRICT AFL_LLM_POST_GAIN
export AFL_LLM_FEEDBACK AFL_LLM_FEEDBACK_MAX_RETRIES
export AFL_LLM_SOFT_VALIDATION AFL_LLM_SOFT_FORMAT_PROB
export AFL_LLM_SOFT_GRAMMAR_PROB AFL_LLM_SOFT_CONTEXT_PROB
export AFL_LLM_SOFT_SEED_PRIORITY
```

实验命令：

```bash
./run.sh 10 720 live555,pure-ftpd chatafl-soft exp_soft_01
```

## 6. 推荐运行顺序

不要一开始跑长时间完整实验。推荐三步推进。

### 阶段 1：工程接入与 smoke test

目标：确认四组都能启动、产生日志、输出目录一致。

建议：

```bash
./run.sh 1 10 live555 chatafl-base smoke_base
./run.sh 1 10 live555 chatafl-v0 smoke_v0
./run.sh 1 10 live555 chatafl-v2 smoke_hard
./run.sh 1 10 live555 chatafl-soft smoke_soft
```

通过标准：

| 检查项 | 通过条件 |
---|---|
| Docker build | 四个 fuzzer 都能编译 |
| fuzzer_stats | 每组都生成 `fuzzer_stats` |
| LLM artifacts | ChatAFL 相关组生成 `protocol-grammars/` 和 `stall-interactions/` |
| validation logs | Hard/Soft 生成 `llm-validation/*.csv` |
| soft logs | Soft 中出现 `soft_accept` 记录 |

### 阶段 2：短跑参数筛选

目标：先判断 Soft 是否有信号，避免直接消耗长跑资源。

建议：

```bash
./run.sh 3 120 live555,pure-ftpd chatafl-base,chatafl-v0,chatafl-v2,chatafl-soft short_soft_screen
```

重点观察：

| 指标 | 判断 |
---|---|
| Soft 的 `unique_hangs` 是否明显增加 | 如果大幅增加，format/grammar admission 太高。 |
| Soft 的 `paths_total` 是否接近或高于 Hard | 如果远低于 Hard，soft 逻辑可能仍在过度过滤或能量过低。 |
| Soft 的 `new_cov/new_state/new_transition` 是否来自 soft_accept | 如果完全没有，Soft 价值弱。 |
| `context_fail` soft 接收是否比 `grammar_fail` 更有效 | 决定最终参数。 |

### 阶段 3：正式重复实验

目标：给论文提供统计上更稳的结果。

建议：

```bash
./run.sh 10 720 live555,pure-ftpd chatafl-base,chatafl-v0,chatafl-v2,chatafl-soft final_four_groups
```

如果资源允许，正式版使用 24 小时：

```bash
./run.sh 10 1440 live555,pure-ftpd chatafl-base,chatafl-v0,chatafl-v2,chatafl-soft final_four_groups_24h
```

## 7. 指标体系

### 7.1 主指标

| 指标 | 来源 | 用途 |
---|---|---|
| line coverage | gcovr HTML/CSV | 代码覆盖率主结果 |
| branch coverage | gcovr HTML/CSV | 分支探索能力 |
| paths_total | `fuzzer_stats` | AFL 队列规模 |
| state count | `plot_data` / IPSM | 协议状态覆盖 |
| transition count | `ipsm.dot` edge count | 状态转移覆盖 |

### 7.2 验证层指标

| 指标 | 来源 | 用途 |
---|---|---|
| validation pass rate | `llm-validation/*.csv` | LLM 输出合法性 |
| fail type distribution | `reason` 字段 | 格式/语法/上下文失败分布 |
| feedback recovery rate | feedback 前后记录 | 验证驱动修复价值 |
| no_gain ratio | post execution 记录或 replay | LLM 候选无收益比例 |
| soft_accept gain rate | Soft 日志 | Soft Validation 是否真的有效 |

### 7.3 成本指标

| 指标 | 来源 | 用途 |
---|---|---|
| execs_done | `fuzzer_stats` | 总执行量 |
| execs_per_sec | `fuzzer_stats` | 执行效率 |
| chat_times | `plot_data` | LLM 调用成本 |
| coverage per LLM call | coverage / chat_times | LLM 性价比 |
| new_state per LLM call | new states / chat_times | 状态探索性价比 |

## 8. 预期结论模板

### 8.1 如果 V0 优于真实 base

说明 prompt/IO clean 本身已有明显贡献。论文必须把验证层贡献建立在：

```text
Hard/Soft vs V0
```

而不是：

```text
Hard/Soft vs original ChatAFL
```

### 8.2 如果 Hard 低于 V0

这不是失败，而是说明严格验证有探索性损失。重点转向：

```text
Hard Validation reduces invalid/hanging executions but may discard boundary inputs.
```

这正好引出 Soft Validation。

### 8.3 如果 Soft 高于 Hard

这是最理想的主结论：

```text
Soft Validation recovers part of the exploration value lost by strict validation while keeping validation-driven observability.
```

需要进一步证明收益主要来自：

```text
CONTEXT_FAIL soft admission
```

而不是随机噪声。

### 8.4 如果 Soft 与 Hard 持平

仍可成立的结论：

```text
Validation-driven logging and feedback make LLM fuzzing measurable, but low-probability admission does not consistently improve coverage on these targets.
```

此时论文应弱化覆盖率提升，强调可解释性、风险边界和负结果。

### 8.5 如果 Soft 低于 Hard

说明风险总结中的噪声稀释成立。应收缩 Soft 参数：

```text
FORMAT_FAIL = 0
GRAMMAR_FAIL = 0
CONTEXT_FAIL = 10~20
```

并把 Soft 作为消融负例，不作为最终主方案。

## 9. 最小完成标准

前四组实验计划完成的最低标准是：

1. `chatafl-base` 能作为真实原始基线进入 ProfuzzBench。
2. `chatafl-v0` 在论文中被重命名或解释为 `PromptClean`，不再冒充真实原始基线。
3. `chatafl-v2` 作为 Hard Validation 组使用时，明确包含 auto feedback；如需纯 hard，额外设置 `AFL_LLM_FEEDBACK=0`。
4. `chatafl-soft` 使用按失败类型低概率 admission，而不是 `AFL_LLM_VALIDATION_PERMISSIVE=1` 全放行。
5. 每组至少覆盖 `live555` 与 `pure-ftpd` 两个目标。
6. 每组至少 3 次短跑筛选，正式实验建议 10 次重复。
7. 最终报告同时给出覆盖率指标、状态指标、验证层指标和成本指标。

## 10. 当前最推荐的下一步

优先做工程接入，而不是直接长跑：

```text
1. 新增 chatafl-base 目录并接入 Dockerfile / profuzzbench_exec_all.sh。
2. 新增 chatafl-soft 目录，先从 ChatAFL-V2 或 ChatAFL 派生，但关闭 post-gain。
3. 在 soft 变体中实现 should_soft_accept(result, stage)。
4. 跑 1 容器 10 分钟 smoke test。
5. 确认日志完整后，再跑 3 容器 120 分钟短跑。
```

只有当短跑中 Soft 相对 Hard 至少表现出以下任一信号，才值得进入正式长跑：

```text
soft_accept 中有 new_cov/new_state/new_transition；
Soft 的 coverage 或 state count 稳定不低于 Hard；
Soft 的 hang 增量可控；
Soft 在 PureFTPD 上比 Live555 更能触发错误处理路径。
```

