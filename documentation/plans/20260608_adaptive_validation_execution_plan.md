# 自适应验证与收益引导 LLM Fuzzing：执行计划、技术分析与预期结果

## 1. 实验变体定义与代码映射

### 1.1 变体总览

| 变体代号 | 基于 | 目录 | 核心机制 | 验证模式 | Post-Gain | Feedback | Repair | Shadow |
|---------|------|------|---------|---------|-----------|----------|--------|--------|
| **B0** | 独立 | `ChatAFL-TR1/` | 纯LLM，无验证 | 禁用 | 关 | 关 | 关 | 关 |
| **B1** | 独立 | `ChatAFL-TR2/` | PromptClean | 禁用 | 关 | 关 | 关 | 关 |
| **V1** | ChatAFL | `ChatAFL-V1/` | Hard Reject | 严格 | 关 | 开 | 关 | 关 |
| **V2** | ChatAFL | `ChatAFL-V2/` | Shadow Validation | 严格 | 开 | 开 | 关 | 开 |
| **V3** | ChatAFL | `ChatAFL-V3/` | Repair Validation | 严格 | 关 | 开 | 开 | 关 |
| **V4** | ChatAFL | `ChatAFL-V4/` | Repair + Post-Gain | 严格 | 开 | 开 | 开 | 关 |
| **V5** | ChatAFL | `ChatAFL-V5/` | V4 + Target-Transition | 严格 | 开 | 开 | 开 | 关 |
| **TR4** | 独立 | `ChatAFL-TR4/` | Soft Validation（负对照） | 混合 | 关 | 开 | 关 | 关 |

### 1.2 变体间消融关系

```
B0 (纯LLM)
│
├─+PromptClean→ B1
│
│  ChatAFL (完整栈基线)
│  │
│  ├─ 去掉Repair/PostGain → V1 (Hard Reject)
│  │   │
│  │   └─+Shadow→ V2 (Shadow Validation)
│  │
│  ├─ 去掉PostGain → V3 (Repair Validation)
│  │   │
│  │   └─+PostGain→ V4 (Repair + Post-Gain)
│  │       │
│  │       └─+TargetTransition→ V5
│  │
│  └─ TR4 (Soft Validation 负对照)
```

## 2. 详细执行计划

### 2.1 Phase 1：数据对齐与基础设施（预计 2 天）

#### 2.1.1 已有数据分析

当前实验数据（两次运行，800分钟/次）：

| 变体 | n_nodes (avg) | n_edges (avg) | 行覆盖率 | 分支覆盖率 |
|------|:------------:|:------------:|:--------:|:----------:|
| TR1 (B0) | 13.0 | 128.5 | 24.2% | 15.5% |
| TR2 (B1) | 14.0 | 150.5 | 24.4% | 15.6% |
| TR3 | 13.5 | 135.5 | 24.4% | 15.6% |
| TR4 | 11.5 | 89.5 | 24.3% | 15.5% |

论文基线（24小时，10次重复）：

| 方法 | n_nodes | n_edges |
|------|:-------:|:-------:|
| CHATAFL | 14.20 | 160.00 |
| AFLNET | 10.00 | 83.80 |

#### 2.1.2 补充实验

对 V1-V5 和 TR4 各运行 2-3 次（初步筛选），每次 800 分钟。正式实验 5-10 次，1440 分钟。

### 2.2 Phase 2：Shadow Validation（V2）实现（预计 1 天）

**目标**：验证"严格拒绝是否真的丢弃了有收益的候选"。

**技术方案**：在现有 `afl-fuzz.c` 验证路径中，将 `REJECT` 替换为 `LOG + ACCEPT`。

**代码修改点**（以 `ChatAFL/afl-fuzz.c` 为基础）：

#### 2.2.1 Stall-breaking 路径（~line 7502-7558）

```c
// 原始逻辑：
if (record.result != LLM_VALID_OK) {
    // log + reject (hard mode) 或 feedback retry
}

// V2 Shadow 逻辑：
if (record.result != LLM_VALID_OK) {
    fill_validation_reason(&record, record.result, "stall_shadow");
    log_llm_validation_record(&record);
    // 不 reject，继续执行，但标记为 shadow
    record.shadow = 1;
}
```

#### 2.2.2 Enrichment 路径（~line 3160-3230）

同理，在 enrichment 验证失败处添加 shadow 路径。

#### 2.2.3 关键指标采集

在 `llm_validation_record_t` 中扩展字段：

```c
u8 shadow;              // 是否为 shadow 通过
u8 shadow_had_new_cov;  // shadow 执行后是否产生新覆盖
u8 shadow_had_new_state;
u8 shadow_had_new_transition;
```

**预期观测**：shadow 候选中约 10-25% 会产生新覆盖或新状态转移，证明严格验证存在 false reject。

### 2.3 Phase 3：Repair Validation（V3）实现（预计 2 天）

**目标**：对格式可修复的失败候选自动修复，而非直接拒绝。

#### 2.3.1 Repair Gate 设计

新增函数 `repair_rtsp_message()`，在 `llm-validator.c` 中实现：

```c
typedef enum {
  REPAIR_NONE = 0,      // 无需修复
  REPAIR_SUCCESS,       // 修复成功
  REPAIR_FAILED         // 无法修复
} repair_result_t;

repair_result_t repair_rtsp_message(char *msg, size_t buf_size,
                                     llm_validation_result_t fail_type);
```

**可修复的格式错误（按频率排序）**：

| 错误类型 | 检测方式 | 修复策略 |
|---------|---------|---------|
| 缺少 `\r\n\r\n` 结尾 | `!strstr(msg, "\r\n\r\n")` | 追加 `\r\n\r\n` |
| `\n` 替代 `\r\n` | regex `(?<!\r)\n` | 替换为 `\r\n` |
| CSeq 缺失 | `!find_header(msg, "cseq:")` | 插入 `CSeq: 1\r\n` |
| Session 缺失（非 SETUP） | 方法不是 SETUP 且无 Session | 从上下文获取最近 Session 值 |
| Transport 缺失（SETUP） | 方法是 SETUP 且无 Transport | 插入默认 `Transport: RTP/AVP;unicast;client_port=8000-8001\r\n` |
| RTSP 版本错误 | `version != "RTSP/1.0"` | 替换为 `RTSP/1.0` |
| URI 格式错误 | `!strstr(uri, "rtsp://")` | 使用默认 URI `rtsp://localhost:8554/stream\r\n` |

#### 2.3.2 Repair 流程集成

在 `afl-fuzz.c` stall-validation 路径中：

```c
if (record.result != LLM_VALID_OK) {
    // 先尝试 repair
    repair_result_t rep = REPAIR_NONE;
    if (record.result == LLM_VALID_FORMAT_FAIL ||
        record.result == LLM_VALID_GRAMMAR_FAIL) {
        rep = repair_rtsp_message(stall_message, MAX_MSG_SIZE, record.result);
    }

    if (rep == REPAIR_SUCCESS) {
        // 重新验证
        record.result = validate_llm_message_with_mode(...);
        record.repaired = 1;
    }

    if (record.result != LLM_VALID_OK) {
        // 原有 feedback retry 或 reject 逻辑
    }
}
```

#### 2.3.3 Repair 成功率统计

在 validation log 中记录：

```csv
stage,result,repair_attempted,repair_success,repaired_valid_pass
stall,FORMAT_FAIL,1,1,1
enrichment,GRAMMAR_FAIL,1,0,0
```

**预期**：Repair 成功率约 30-60%（FORMAT_FAIL 类型），GRAMMAR_FAIL 约 10-20%。

### 2.4 Phase 4：Post-Gain 增强（V4）实现（预计 1.5 天）

**目标**：将验证从"合法性过滤器"转变为"状态探索反馈机制"。

#### 2.4.1 修改 `classify_llm_execution_gain`

当前实现（二值判定）：

```c
llm_validation_result_t classify_llm_execution_gain(
    u8 has_new_cov, u8 has_new_state, u8 has_new_transition) {
  if (has_new_cov || has_new_state || has_new_transition) return LLM_VALID_OK;
  return LLM_VALID_NO_GAIN;
}
```

V4 增强为加权收益评分：

```c
typedef struct {
  llm_validation_result_t result;
  s32 gain_score;  // 加权收益分
  u8 gain_class;   // 0=NO_GAIN, 1=COV_ONLY, 2=STATE, 3=TRANSITION
} llm_gain_result_t;

llm_gain_result_t classify_llm_execution_gain_weighted(
    u8 has_new_cov, u8 has_new_state, u8 has_new_transition) {
  llm_gain_result_t r;
  s32 score = 0;

  if (has_new_transition) {
    score += 10;
    r.gain_class = 3;
  } else if (has_new_state) {
    score += 6;
    r.gain_class = 2;
  } else if (has_new_cov) {
    score += 2;
    r.gain_class = 1;
  } else {
    score -= 1;
    r.gain_class = 0;
  }

  if (score > 0) {
    r.result = LLM_VALID_OK;
  } else {
    r.result = LLM_VALID_NO_GAIN;
  }
  r.gain_score = score;
  return r;
}
```

#### 2.4.2 Gain 反馈到 Seed 优先级

在 `afl-fuzz.c` 中，LLM 生成的 seed 标记 gain_score：

```c
// mark_next_seed_as_llm 时同时设置 gain_score
if (gain_result.gain_class >= 3) {
    next_seed_llm_priority = 10;  // 最高优先级
} else if (gain_result.gain_class >= 2) {
    next_seed_llm_priority = 7;
} else if (gain_result.gain_class >= 1) {
    next_seed_llm_priority = 4;
} else {
    next_seed_llm_priority = 1;   // 最低优先级
}
```

#### 2.4.3 Gain 统计日志

扩展 validation log 输出：

```csv
stage,result,gain_score,gain_class,new_cov,new_state,new_transition,exec_us
stall,OK,10,3,1,1,1,2345
stall,NO_GAIN,-1,0,0,0,0,1200
```

**预期**：V4 相比 V3 在 n_edges 上提升 5-15%，因为高收益 seed 会被更频繁选择。

### 2.5 Phase 5：Target-Transition Prompt（V5）实现（预计 2 天）

**目标**：让 LLM 生成针对缺失/低频状态转移的请求。

#### 2.5.1 IPSM 状态图解析

从 `ipsm.dot` 文件中提取状态图信息：

```c
typedef struct {
  u32 from_state;
  u32 to_state;
  u32 count;       // 该转移被执行的次数
  char label[64];  // 触发该转移的请求类型
} transition_info_t;

typedef struct {
  u32 state_id;
  u32 visit_count;
} state_info_t;

// 解析 ipsm.dot 文件
int parse_ipsm_graph(const char *dot_path,
                     transition_info_t **transitions, u32 *trans_count,
                     state_info_t **states, u32 *state_count);
```

#### 2.5.2 目标选择算法

```c
typedef struct {
  u32 target_state;        // 目标状态
  u32 source_state;        // 当前状态
  char suggested_method[64]; // 建议的请求方法
  u8 priority;             // 优先级（未探索=高，低频=中）
} target_transition_t;

int select_target_transition(
    const transition_info_t *transitions, u32 trans_count,
    const state_info_t *states, u32 state_count,
    u32 current_state,
    target_transition_t *target);
```

选择优先级：
1. 未见过的 response code（priority=10）
2. 低频 transition（count < 3，priority=7）
3. 能离开 400/404 循环的 transition（priority=5）
4. 有 Session 时优先 PLAY/PAUSE/TEARDOWN 变体
5. 无 Session 时优先 DESCRIBE/SETUP 变体

#### 2.5.3 Prompt 构造增强

在 `construct_prompt_stall()` 基础上，新增 `construct_prompt_stall_targeted()`：

```c
char *construct_prompt_stall_targeted(
    char *protocol_name,
    char *examples,
    char *history,
    const target_transition_t *target,
    const char *feedback_summary);
```

Prompt 结构：

```
You are a protocol fuzzing assistant for RTSP.

Recent communication history:
[history]

Example requests:
[examples]

Target state analysis:
- Current response sequence: 0-400-200-201
- Under-explored states: 204, 205, 210
- Low-frequency transitions: 201->202 (1 time), 202->203 (0 times)
- Goal: Generate one RTSP request likely to reach state 204 or trigger transition 202->203.

Recent feedback:
- 42% of recent messages had CSeq errors.
- PLAY requests often returned 454 (Session expired).
- High-gain patterns: SETUP with alternative track ID.

Generate a single RTSP request that:
1. Is syntactically correct (proper CRLF, CSeq, Session if needed).
2. Is likely to reach the target state 204.
3. Avoids repeating patterns that only produce 400/404.
```

**预期**：V5 相比 V4 在 n_edges 上再提升 5-10%，在低频/未探索转移上提升更显著。

## 3. 技术分析

### 3.1 各组件技术风险评估

| 组件 | 复杂度 | 风险 | 缓解措施 |
|------|:------:|:----:|---------|
| Shadow Validation | 低 | 低 | 仅修改条件判断，不改核心逻辑 |
| Repair Gate | 中 | 中 | 需要正确处理 RTSP 消息格式边界情况 |
| Post-Gain 权重 | 低 | 低 | 仅扩展已有 `classify_llm_execution_gain` |
| Target-Transition | 高 | 中 | 需要解析 ipsm.dot，可能引入解析错误 |
| 动态 Temperature | 中 | 中 | 需要合理的调整公式和边界保护 |
| Gain-based Scheduling | 高 | 高 | 涉及 seed 选择核心路径，需充分测试 |

### 3.2 关键代码路径分析

#### 3.2.1 Stall-breaking 验证路径

```
afl-fuzz.c:7365  uninteresting_times >= THRESHOLD
    ↓
afl-fuzz.c:7466  construct_prompt_stall()
    ↓
afl-fuzz.c:7468  chat_with_llm(..., temperature=1.5)
    ↓
afl-fuzz.c:7500  format_request_message()
    ↓
afl-fuzz.c:7512  validate_llm_message_with_mode()  ← 修改点1: Shadow/Repair
    ↓
afl-fuzz.c:7518  if FAIL:
    ↓
afl-fuzz.c:7522  feedback retry  ← 修改点2: Repair 优先于 feedback
    ↓
afl-fuzz.c:7552  common_fuzz_stuff()
    ↓
afl-fuzz.c:7554  fill_post_execution_record()  ← 修改点3: 加权收益
```

#### 3.2.2 Enrichment 验证路径

```
afl-fuzz.c:3184  format_request_message()
    ↓
afl-fuzz.c:3199  validate_llm_sequence_with_mode()  ← 修改点1
    ↓
afl-fuzz.c:3214  if FAIL:
    ↓
afl-fuzz.c:3220  feedback retry  ← 修改点2
    ↓
afl-fuzz.c:3230  write_new_seeds()
```

### 3.3 `llm-validator.c` 修改清单

| 修改 | 文件 | 行范围 | 说明 |
|------|------|--------|------|
| 新增 `repair_rtsp_message()` | `llm-validator.c` | 新增 ~100 行 | RTSP 消息格式修复 |
| 新增 `repair_ftp_message()` | `llm-validator.c` | 新增 ~50 行 | FTP 消息格式修复 |
| 修改 `classify_llm_execution_gain()` | `llm-validator.c:501-507` | 修改 ~20 行 | 加权收益判定 |
| 新增 `classify_llm_execution_gain_weighted()` | `llm-validator.c` | 新增 ~30 行 | 带权重的收益分类 |
| 修改 `log_llm_validation_record()` | `llm-validator.c` | 修改 ~10 行 | 扩展日志字段 |

### 3.4 `afl-fuzz.c` 修改清单

| 修改 | 文件 | 行范围 | 说明 |
|------|------|--------|------|
| Shadow validation 逻辑 | `afl-fuzz.c:7502-7558` | 修改 ~20 行 | 失败时 log 但不 reject |
| Repair gate 集成 | `afl-fuzz.c:7518-7538` | 修改 ~30 行 | 失败时先 repair 再 feedback |
| Post-Gain 加权 | `afl-fuzz.c:7554-7558` | 修改 ~10 行 | 使用加权收益分类 |
| Target prompt 集成 | `afl-fuzz.c:7466` | 修改 ~20 行 | 使用 targeted prompt |
| Seed 优先级调整 | `afl-fuzz.c:7550-7551` | 修改 ~10 行 | 基于 gain_class 设置优先级 |

## 4. 双重指标系统

### 4.1 论文主指标（Paper Main Metrics）

| 指标 | 来源 | 含义 | 论文对标 |
|------|------|------|---------|
| `n_nodes` | `plot_data` 最后一行 | 状态图节点数（唯一状态数） | CHATAFL: 14.20 |
| `n_edges` | `plot_data` 最后一行 | 状态图边数（唯一转移数） | CHATAFL: 160.00 |
| 行覆盖率 | `fuzzer_stats` | 指令级覆盖 | — |
| 分支覆盖率 | `fuzzer_stats` | 分支级覆盖 | — |

### 4.2 机制指标（Mechanism Metrics）

| 指标 | 计算方式 | 含义 | 预期范围 |
|------|---------|------|---------|
| `valid_rate` | `ok_count / total_llm_calls` | LLM 输出合法率 | B0:0%, B1:0%, V1:40-60%, V2:同V1(但不reject) |
| `repair_success_rate` | `repair_success / repair_attempted` | 修复成功率 | V3/V4/V5: 30-60% |
| `false_reject_gain_rate` | `shadow_gain / shadow_total` | 被错误拒绝的有收益候选占比 | V2: 10-25% |
| `new_state_per_llm_call` | `total_new_states / total_llm_calls` | 每次LLM调用产生的新状态数 | 0.01-0.1 |
| `new_transition_per_llm_call` | `total_new_transitions / total_llm_calls` | 每次LLM调用产生的新转移数 | 0.05-0.5 |
| `gain_score_distribution` | histogram of gain_score | 收益分分布 | — |
| `feedback_retry_rate` | `retry_count / fail_count` | 反馈重试触发率 | 30-70% |
| `feedback_recovery_rate` | `retry_success / retry_count` | 反馈重试恢复率 | 20-50% |
| `avg_exec_us` | `sum(exec_us) / llm_calls` | LLM 种子平均执行时间 | 1000-5000 μs |

### 4.3 指标采集实现

在 `plot_data` 和 `llm-validation/*.csv` 中扩展：

```c
// afl-fuzz.c: plot_data 输出时增加
fprintf(f, "llm_valid_rate:%.4f,", valid_rate);
fprintf(f, "llm_repair_rate:%.4f,", repair_rate);
fprintf(f, "llm_gain_avg:%.2f,", avg_gain_score);
fprintf(f, "llm_new_state_per_call:%.4f,", new_state_per_call);
fprintf(f, "llm_new_transition_per_call:%.4f,", new_trans_per_call);
```

## 5. 预期结果分析

### 5.1 协议状态覆盖预期

| 变体 | n_nodes 预期 | n_edges 预期 | 与 CHATAFL 基线对比 |
|------|:------------:|:------------:|:------------------:|
| **B0** | 13-14 | 125-135 | -8% / -18% |
| **B1** | 14 | 148-155 | -1% / -5% |
| **V1** | 13-14 | 130-140 | -5% / -15% |
| **V2** | 14-15 | 155-165 | +3% / +0% |
| **V3** | 14 | 150-160 | -1% / -3% |
| **V4** | 14-15 | 160-175 | +3% / +6% |
| **V5** | 15 | 170-190 | +6% / +15% |
| **TR4** | 11-12 | 85-95 | -20% / -43% |

**分析**：

1. **V2（Shadow）** 可能意外接近甚至超过 CHATAFL 基线，因为被严格拒绝的"半合法"候选实际上有状态探索价值。
2. **V4（Repair+Post-Gain）** 预期最接近或超过 CHATAFL 基线，因为 Repair 保留了格式可修复的候选，Post-Gain 确保高收益 seed 被优先使用。
3. **V5（+Target-Transition）** 预期在 n_edges 上有最大提升，因为显式引导 LLM 面向未探索转移生成请求。
4. **V1（Hard Reject）** 可能略低于 B1（TR2），因为严格拒绝了一些有探索价值的候选。
5. **TR4** 继续作为负对照，预期 n_edges 最低。

### 5.2 代码覆盖率预期

| 变体 | 行覆盖率预期 | 分支覆盖率预期 |
|------|:------------:|:------------:|
| **B0** | 24.0-24.5% | 15.3-15.7% |
| **B1** | 24.2-24.6% | 15.5-15.8% |
| **V1** | 24.1-24.5% | 15.4-15.7% |
| **V2** | 24.3-24.7% | 15.5-15.8% |
| **V3** | 24.2-24.6% | 15.5-15.8% |
| **V4** | 24.3-24.7% | 15.6-15.9% |
| **V5** | 24.4-24.8% | 15.7-16.0% |

**分析**：代码覆盖率在各变体间差异较小（<1pp），因为 Live555 的 RTSP 实现规模有限，行覆盖率已接近饱和。**分支覆盖率**的差异更能反映协议状态探索的深度。

### 5.3 机制指标预期

| 指标 | B0 | B1 | V1 | V2 | V3 | V4 | V5 |
|------|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| valid_rate | 0% | 0% | 45% | 45% | 55% | 55% | 55% |
| repair_rate | — | — | 0% | 0% | 40% | 40% | 40% |
| false_reject_rate | — | — | — | 15% | — | — | — |
| new_state/call | 0.02 | 0.03 | 0.02 | 0.04 | 0.03 | 0.05 | 0.07 |
| new_transition/call | 0.15 | 0.20 | 0.15 | 0.25 | 0.22 | 0.30 | 0.40 |
| gain_score_avg | 1.5 | 2.0 | 1.8 | 2.5 | 2.2 | 3.0 | 3.5 |
| feedback_retry_rate | — | — | 50% | 50% | 35% | 35% | 35% |
| feedback_recovery_rate | — | — | 25% | 25% | 35% | 35% | 35% |

### 5.4 关键对比分析

#### 对比组1：V1 vs V2（Shadow Validation 的价值）

- **假设**：严格验证存在 false reject，shadow 能发现被错误丢弃的有收益候选
- **预期**：V2 的 n_edges 比 V1 高 15-25 个（+10-20%）
- **机制解释**：shadow 通过的候选中，约 10-25% 产生新状态转移，证明验证过于保守

#### 对比组2：V1 vs V3（Repair 的价值）

- **假设**：格式修复能挽救部分被拒绝的候选
- **预期**：V3 的 n_edges 比 V1 高 10-20 个（+8-15%）
- **机制解释**：FORMAT_FAIL 占失败的 60-70%，其中 30-60% 可修复

#### 对比组3：V3 vs V4（Post-Gain 的价值）

- **假设**：加权收益判定能提升高收益 seed 的复用率
- **预期**：V4 的 n_edges 比 V3 高 10-15 个（+7-10%）
- **机制解释**：gain_class=3 的 seed 优先级是 gain_class=0 的 10 倍

#### 对比组4：V4 vs V5（Target-Transition 的价值）

- **假设**：状态目标引导能直接提升低频/未探索转移
- **预期**：V5 的 n_edges 比 V4 高 10-20 个（+6-12%）
- **机制解释**：targeted prompt 将 LLM 的注意力从"生成合法消息"转向"探索目标转移"

#### 对比组5：V2 vs B0（验证是否有害）

- **假设**：shadow 验证（不 reject）不应降低探索性
- **预期**：V2 的 n_edges 应 ≥ B0
- **如果 V2 < B0**：说明验证的 log 开销或分支影响了 fuzzing 效率

### 5.5 统计显著性检验

每次实验运行 5-10 次（1440 分钟/次），使用以下检验：

| 指标 | 检验方法 | 显著性阈值 |
|------|---------|-----------|
| n_nodes | Wilcoxon rank-sum | p < 0.05 |
| n_edges | Wilcoxon rank-sum | p < 0.05 |
| 行覆盖率 | Welch's t-test | p < 0.05 |
| 分支覆盖率 | Welch's t-test | p < 0.05 |
| gain_score | Mann-Whitney U | p < 0.05 |

由于 n_nodes 取值范围有限（10-15），建议使用 effect size（Cliff's delta）补充：

- |d| < 0.147：negligible
- 0.147 ≤ |d| < 0.33：small
- 0.33 ≤ |d| < 0.474：medium
- |d| ≥ 0.474：large

## 6. 完整实验时间表

| 阶段 | 任务 | 预计时间 | 依赖 |
|------|------|---------|------|
| Phase 1 | 数据对齐 + 基础设施 | 2 天 | 无 |
| Phase 2 | Shadow Validation (V2) | 1 天 | Phase 1 |
| Phase 3 | Repair Validation (V3) | 2 天 | Phase 2 |
| Phase 4 | Post-Gain 增强 (V4) | 1.5 天 | Phase 3 |
| Phase 5 | Target-Transition (V5) | 2 天 | Phase 4 |
| Phase 6 | 初步筛选实验（2-3次/变体） | 3 天 | Phase 2-5 |
| Phase 7 | 正式实验（5-10次/变体） | 7 天 | Phase 6 |
| Phase 8 | 数据分析 + 论文撰写 | 5 天 | Phase 7 |
| **总计** | | **~23 天** | |

### 6.1 关键里程碑

1. **M1（Phase 2 完成）**：V2 运行成功，shadow 数据可分析
2. **M2（Phase 5 完成）**：所有变体代码就绪
3. **M3（Phase 6 完成）**：初步筛选结果，确定正式实验的变体范围
4. **M4（Phase 7 完成）**：正式实验数据，可进行统计分析

## 7. 风险与应对

### 7.1 技术风险

| 风险 | 概率 | 影响 | 应对 |
|------|:----:|:----:|------|
| Repair 修复后消息仍导致 hang | 中 | 中 | 修复后增加 timeout 检查 |
| Target-Transition 解析 ipsm.dot 失败 | 低 | 低 | 使用 graphviz C API 直接解析 |
| Post-Gain 权重设置不当导致 seed 偏差 | 中 | 中 | 通过敏感性分析确定最优权重 |
| 实验运行时间不足 | 低 | 高 | 延长至 1440 分钟/次 |

### 7.2 实验风险

| 风险 | 概率 | 影响 | 应对 |
|------|:----:|:----:|------|
| LLM API 不稳定 | 中 | 高 | 增加重试次数，记录失败率 |
| 实验结果不显著 | 中 | 高 | 增加重复次数至 10 次 |
| V5 效果不明显 | 中 | 中 | 调整 target 选择策略 |

## 8. 总结

本计划通过 5 个阶段的渐进式消融实验，系统性验证以下核心假设：

1. **Shadow 假设**：严格验证存在 false reject（V2 > V1）
2. **Repair 假设**：格式修复能挽救被拒绝的候选（V3 > V1）
3. **Post-Gain 假设**：加权收益判定能提升 seed 质量（V4 > V3）
4. **Target-Transition 假设**：状态目标引导能提升转移覆盖率（V5 > V4）

最终目标：**V5 的 n_edges 达到 170-190，超过 CHATAFL 论文基线（160）**，同时代码覆盖率保持在 24.5% 以上。
