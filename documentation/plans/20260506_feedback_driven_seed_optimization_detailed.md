# 基于覆盖率反馈的 ChatAFL 种子优化：详细改进方案

本文档是对 `plan/20260429_ChatAFL_Improvement_Directions_Analysis.md` 中“基于覆盖率反馈的 ChatAFL 种子优化”方向的细化版。目标不是再给一个泛泛方向，而是明确：

1. 当前 ChatAFL 在代码里已经具备哪些基础；
2. 现有实现为什么还不能算“反馈驱动种子优化”；
3. 具体应该改哪些函数、增加哪些数据结构、如何接入主循环；
4. 修改后预期能提升什么；
5. 为什么这条路线具备论文价值。

---

## 1. 问题定义

原始 ChatAFL 里的 LLM 使用方式，本质上仍然是“开环”的：

- 启动前做一次 grammar 抽取；
- 启动前做一次 seed enrichment；
- 运行时在 coverage 长时间停滞后，临时让 LLM 生成一条“下一条消息”。

它**并没有形成“某条 seed 的执行反馈 -> 分析其失败原因 -> 定向重写该 seed -> 再验证收益”**的闭环。

因此，当前系统虽然已经有 LLM、有覆盖率反馈、有状态反馈，但还没有把三者真正绑定成一个优化器。

本方向的核心定义应当是：

> 将 ChatAFL 中原本一次性、开环的 LLM 生成机制，升级为一个基于覆盖率和状态收益的在线种子重写系统，让 LLM 不再“盲目补全”，而是“根据失败反馈重写高潜力低收益 seed”。

---

## 2. 基于代码的现状分析

### 2.1 现有系统已经有的能力

当前代码里，做这个方向最关键的基础能力已经存在。

#### 2.1.1 队列项已经记录了种子的核心属性

`ChatAFL/afl-fuzz.c` 的 `struct queue_entry` 已经保存了种子优化需要的大部分基本指标，包括：

- `len`
- `exec_us`
- `bitmap_size`
- `has_new_cov`
- `favored`
- `depth`
- `generating_state_id`
- `unique_state_count`

见：
- `ChatAFL/afl-fuzz.c:243`

这意味着我们**不需要重新发明 seed metadata 系统**，只需要扩展现有队列项。

#### 2.1.2 系统已经能精确判断某次执行是否有新覆盖

当前覆盖率收益判定入口是：

- `has_new_bits()`：`ChatAFL/afl-fuzz.c:2082`
- `save_if_interesting()`：`ChatAFL/afl-fuzz.c:4642`

其中：

- `has_new_bits(virgin_bits)` 返回这次执行是否带来了新的 coverage；
- `save_if_interesting()` 决定该输入是否进入 queue；
- `common_fuzz_stuff()` 在 `ChatAFL/afl-fuzz.c:6305` 用 `is_interesting` 更新 `uninteresting_times`。

这说明“覆盖率反馈”本身并不缺，缺的是**把反馈绑定回原始 seed 的重写过程**。

#### 2.1.3 系统已经有状态收益信号

ChatAFL 不是纯 AFL，它继承了 AFLNet 的状态反馈。

- `update_state_aware_variables()` 会从响应中提取状态序列：`ChatAFL/afl-fuzz.c:1032`
- 它会更新：
  - IPSM 图
  - state->paths
  - state->paths_discovered
  - seed 到 reachable state 的映射

这说明系统天然可以拿到两类收益：

1. coverage 收益；
2. state transition 收益。

#### 2.1.4 系统已经有 seed 调度和 favored 机制

- `choose_seed()`：`ChatAFL/afl-fuzz.c:927`
- `update_bitmap_score()`：`ChatAFL/afl-fuzz.c:2453`
- `cull_queue()`：`ChatAFL/afl-fuzz.c:2511`
- `calculate_score()`：`ChatAFL/afl-fuzz.c:6375`

当前 AFL/ChatAFL 已经会根据：

- 执行速度；
- bitmap 大小；
- depth；
- favored 状态；

给 seed 分配更多执行机会。

因此，反馈驱动优化不应该另起一套 scheduler，而应该**作为现有 scheduler 上方的“种子再生层”**。

#### 2.1.5 系统已经有 LLM 交互入口

- `chat_with_llm()`：`ChatAFL/chat-llm.c:46`
- `clean_llm_response()`：`ChatAFL/chat-llm.c:203`
- `enrich_sequence()`：`ChatAFL/chat-llm.c:1421`
- `construct_prompt_stall()`：`ChatAFL/chat-llm.c:601`

所以新的反馈驱动重写不需要重做通信层，只要增加新的 prompt 构造函数和新的验证逻辑即可。

---

### 2.2 当前实现的根本缺口

#### 2.2.1 Seed enrichment 只有“缺消息类型补全”，没有“失败反馈重写”

当前 enrichment 流程：

- `get_seeds_with_messsage_types()`：`ChatAFL/afl-fuzz.c:2637`
- `enrich_sequence()`：`ChatAFL/chat-llm.c:1421`
- `write_new_seeds()`：`ChatAFL/chat-llm.c:1229`

现有逻辑只是：

1. 找种子里缺失的 message types；
2. 让 LLM 把缺失消息补进去；
3. 只要新结果和原文不完全相同，就写成新 seed。

问题在于：

- 没有利用运行时 coverage 反馈；
- 没有利用 state transition 反馈；
- 没有判断“这个 seed 过去是不是长期无收益”；
- 没有区分“这个 seed 值不值得 LLM 成本”。

因此它只是 startup-time seed enrichment，不是 feedback-driven optimization。

#### 2.2.2 Stall-breaking 只生成“下一条消息”，没有重写整个低收益 seed

当前停滞突破逻辑在：

- `ChatAFL/afl-fuzz.c:6858-7005`

其特点是：

- 触发条件只有 `uninteresting_times >= UNINTERESTING_THRESHOLD`；
- 只基于当前 prefix/history 让 LLM 生成一条消息；
- 通过 `validate_protocol_request_message()` 做有限校验；
- 然后直接送入 `common_fuzz_stuff()`。

问题是：

- 它解决的是“当前 mutation 序列怎么补一步”，不是“哪条 seed 应该被系统性重写”；
- 没有记录“这次 LLM 调用究竟解决了哪条 seed 的什么问题”；
- 没有把失败原因结构化。

#### 2.2.3 当前验证极弱，无法支撑高质量反馈闭环

`validate_protocol_request_message()` 在 `ChatAFL/chat-llm.c:586` 目前只对 RTSP 做了真实检查，其他协议几乎默认通过。

这会导致反馈闭环里出现一个严重问题：

- 如果 LLM 重写出来的是语法脏数据；
- 系统会误以为“这是 seed rewrite 失败”；
- 实际上失败原因只是“样本不合法”。

这会污染后续分析，也削弱论文中的因果解释力。

#### 2.2.4 当前没有“按 seed 聚合”的收益归因

现在系统能判断某次执行是否 interesting，但不能回答这些更关键的问题：

- 哪些 seed 被 LLM 重写过？
- 每条 seed 被重写了几次？
- 哪种反馈类型最能带来新覆盖？
- 哪种重写更常带来新状态而不是仅仅新边？
- LLM 调用的 ROI 是多少？

没有这些统计，实验就只能报“最终 coverage 变高了”，而很难讲清楚“为什么变高”。

---

## 3. 应该把方法定义成什么

论文和系统实现里，建议把本方向定义为：

**Feedback-Driven Seed Optimization for Stateful Protocol Fuzzing**

它不是普通的“coverage-guided fuzzing”，因为 AFL 本来就有 coverage guidance；它的新增点在于：

1. **反馈对象不是 mutation operator，而是 seed 本身**；
2. **优化动作不是简单加能量，而是调用 LLM 做定向重写**；
3. **反馈信号不是只有 coverage，还包括状态收益和验证失败原因**；
4. **目标不是生成更多 seed，而是用有限 LLM 调用修复高潜力但长期无收益的 seed。**

---

## 4. 详细改进设计

---

### 4.1 新增一层“高潜力低收益 seed 识别器”

这是整个方案的第一步。如果不先选 seed，LLM 成本会失控。

#### 4.1.1 设计目标

只把满足以下特征的 seed 交给 LLM：

- 具备较高探索潜力；
- 但近期 fuzzing 收益明显下降；
- 且其失败更可能是“结构/语义/状态条件不满足”，而不是纯随机性问题。

#### 4.1.2 建议新增字段

在 `struct queue_entry` 中新增：

- `u32 no_gain_cycles;`
- `u32 llm_rewrite_times;`
- `u32 llm_success_times;`
- `u32 llm_fail_format_times;`
- `u32 llm_fail_state_times;`
- `u32 last_new_path_exec;`
- `u32 last_rewrite_exec;`
- `double seed_potential_score;`
- `double seed_stagnation_score;`

建议修改位置：

- `ChatAFL/afl-fuzz.c:243`

#### 4.1.3 候选 seed 评分公式

建议对每条 seed 计算两个值：

1. 潜力分 `Potential(q)`
2. 停滞分 `Stagnation(q)`

可使用如下形式：

```text
Potential(q) =
  w1 * norm(bitmap_size)
+ w2 * norm(unique_state_count)
+ w3 * favored
+ w4 * norm(depth)
- w5 * norm(exec_us)

Stagnation(q) =
  v1 * norm(no_gain_cycles)
+ v2 * I(llm_rewrite_times < K)
+ v3 * I(has_new_cov == 0 recently)
+ v4 * I(paths_discovered around generating_state_id stagnates)
```

最终候选值：

```text
RewritePriority(q) = Potential(q) * Stagnation(q)
```

筛选规则：

- 只在 top-N 的 queue entries 中选；
- 每个 queue cycle 限制最多触发 M 次；
- 对同一 seed 增加 cooldown，避免重复重写。

#### 4.1.4 为什么这一步是必要的

因为 LLM 应该服务于：

- 高价值但被卡住的 seed；

而不是服务于：

- 天然低质量 seed；
- 已经被充分 fuzz 的 seed；
- 运行极慢且状态浅的 seed。

否则 LLM 只是增加成本，不会增加论文贡献。

---

### 4.2 从“全局停滞触发”改成“全局 + 局部双触发”

当前只有全局计数器：

- `uninteresting_times`
- `chat_times`

见：

- `ChatAFL/afl-fuzz.c:401`
- `ChatAFL/afl-fuzz.c:6858`

这太粗。

建议改成双触发：

#### 4.2.1 全局触发

当全局长时间没有新 coverage/new state：

- 触发一次“全局重写机会”；
- 但不是直接对当前 seed 做 stall message 生成；
- 而是调用候选 seed 排名器，从全局 queue 中选最值得重写的 seed。

#### 4.2.2 局部触发

当某条 seed 满足：

- 被选中过多次；
- 最近多轮 mutation 没有进入新路径；
- 但它是 favored 或 state-rich seed；

立即把它标记为 `rewrite_candidate`。

#### 4.2.3 效果

这样做可以避免两个问题：

1. 当前 `queue_cur` 恰好不是最值得优化的对象；
2. 全局停滞掩盖了局部高潜力 seed 的长期失败。

---

### 4.3 让 LLM 做“定向重写”，而不是“自由发挥”

#### 4.3.1 新 prompt 的输入不应只有历史文本

新的 rewrite prompt 应包含 4 类信息：

1. 原始 seed 序列；
2. 该 seed 到达的状态信息；
3. 最近 fuzzing 反馈摘要；
4. 希望突破的目标。

建议 prompt 内容包含：

- 原始请求序列；
- 每条请求对应的响应摘要；
- 当前已到达的状态序列；
- 当前 seed 缺少的新收益类型，例如：
  - “未产生新的 edge”
  - “未进入新的 response state”
  - “在 SETUP 后无法进入 PLAY success path”
- 保留字段约束，例如：
  - `CSeq` 必须连续；
  - `Session` 必须沿用响应中的值；
  - 某类 header 不能丢。

#### 4.3.2 LLM 的任务应该是“重写策略”而不是“续写一条消息”

建议定义 3 类 rewrite mode：

1. `INSERT`
   - 在已有消息之间插入缺失请求；
2. `REPLACE`
   - 替换某一条低质量消息；
3. `REPAIR_AND_ADVANCE`
   - 先修复上下文一致性，再尝试进入下一状态。

这比当前 stall-breaking 只做“猜下一条消息”更强。

#### 4.3.3 新函数建议

在 `ChatAFL/chat-llm.c` 新增：

- `construct_prompt_seed_rewrite(...)`
- `rewrite_seed_with_feedback(...)`

而不是复用 `construct_prompt_stall()`。

原因：

- stall prompt 偏向 next-message prediction；
- rewrite prompt 偏向 sequence-level repair and optimization。

---

### 4.4 引入多级反馈摘要，而不是把原始 bitmap 直接喂给 LLM

不要把 `trace_bits` 原样交给 LLM，那没有语义价值。

建议抽象成 4 类本地反馈：

#### 4.4.1 覆盖率反馈

- 是否产生新 edge；
- 是否只是命中旧 edge；
- bitmap_size 与本 seed 历史最优相比是否下降。

#### 4.4.2 状态反馈

- 是否产生新状态；
- 是否产生新状态转移；
- 状态序列在哪一步停止推进。

依赖：

- `update_state_aware_variables()`：`ChatAFL/afl-fuzz.c:1032`

#### 4.4.3 协议验证反馈

对 LLM 输出做本地分类：

- `FORMAT_FAIL`
- `REQUEST_SPLIT_FAIL`
- `HEADER_MISSING`
- `STATE_CONTEXT_FAIL`
- `RESPONSE_REJECTED`
- `NO_NEW_COVERAGE`
- `NO_NEW_STATE`

这一步很关键。因为论文里真正有价值的是：

> 我们不仅知道 LLM 失败了，还知道失败在协议语法、会话依赖、状态推进还是覆盖率收益层面。

#### 4.4.4 字段级差异反馈

对原 seed 和 rewrite 后 seed 做 message diff：

- 哪条消息被替换；
- 哪个字段被修改；
- 是否新增消息；
- 是否修改了关键依赖字段。

这既方便调试，也方便论文案例分析。

---

### 4.5 新增“重写后准入验证器”

当前 seed enrichment 最薄弱的地方之一是：

- `enrich_sequence()` 返回后，只做了最基础的格式处理；
- `write_new_seeds()` 直接落盘：`ChatAFL/chat-llm.c:1229`

这显然不够。

建议在写入 queue 前加入 4 级验证：

#### 4.5.1 Level-1: 格式验证

- 是否能被 `extract_requests()` 正确切分；
- 是否每条消息以正确分隔结尾；
- 是否无明显 markdown/拒答/解释文字污染。

依赖：

- `clean_llm_response()`：`ChatAFL/chat-llm.c:203`
- `extract_requests_*()`：`ChatAFL/aflnet.c`

#### 4.5.2 Level-2: 协议语法验证

扩展 `validate_protocol_request_message()`：

- 当前只有 RTSP 比较真实；
- 应至少扩展到 FTP / HTTP / SIP；
- 对于不支持完整语法验证的协议，也应做到 request-line + mandatory headers + terminator 的弱验证。

建议修改：

- `ChatAFL/chat-llm.c:586`

#### 4.5.3 Level-3: 会话上下文验证

需要新增 sequence-level validator，而不是只校验单条 message。

例如 RTSP：

- `CSeq` 是否单调；
- `Session` 是否在收到后才被使用；
- `PLAY/PAUSE/TEARDOWN` 是否出现在 `SETUP` 之后；
- `SETUP` 是否保留 `Transport`。

建议新增：

- `validate_protocol_request_sequence(...)`

#### 4.5.4 Level-4: 轻量级试执行验证

对 rewrite 出来的 seed 先做一次“验证执行”：

- 只执行 1 次；
- 检查是否有正常响应；
- 检查是否完全被服务端拒绝；
- 检查是否至少推进到原 seed 已知的状态深度。

如果连原来的状态深度都达不到，就没必要正式入队。

这是区别“新 seed 看起来复杂”和“新 seed 实际有效”的关键。

---

### 4.6 将“LLM 重写 seed”作为新的 queue 来源，而不是临时输入

当前 stall-breaking 的 LLM 输出直接走：

- `common_fuzz_stuff(argv, stall_message, strlen(stall_message))`

见：

- `ChatAFL/afl-fuzz.c:7005`

这相当于把它当成一次普通临时执行，而不是一个可追踪的优化结果。

建议改为：

1. 对 rewrite 结果先做验证执行；
2. 若有效，再以新 seed 身份持久化进入 queue；
3. 给新 seed 打上来源标签：
   - `src:llm_rewrite`
   - `parent:<seed id>`
   - `reason:coverage_stagnation` / `reason:state_stagnation`

可复用现有：

- `save_if_interesting()`：`ChatAFL/afl-fuzz.c:4642`
- `add_to_queue()`：`ChatAFL/afl-fuzz.c:1918`

但要补充父子关系记录。

#### 4.6.1 建议新增字段

在 `struct queue_entry` 中增加：

- `u32 parent_index;`
- `u8 from_llm_rewrite;`
- `u8 rewrite_reason;`

这样论文里就能分析：

- 新 coverage 中有多少来自普通 mutation；
- 有多少来自 LLM rewrite；
- 哪类 rewrite 最有效。

---

### 4.7 让重写结果反向影响能量分配

如果某类 rewrite seed 明显更有效，应该让 scheduler 感知到。

#### 4.7.1 建议的轻量做法

在 `calculate_score()` 之外，加一个 rewrite bonus：

```text
if (q->from_llm_rewrite && q->has_new_cov) bonus += a
if (q->from_llm_rewrite && q->unique_state_count > parent.unique_state_count) bonus += b
if (q->llm_success_times > threshold) bonus += c
```

这样不会破坏 AFL 原有能量模型，但会让成功的 rewrite seed 更容易被继续挖掘。

#### 4.7.2 为什么不能直接大幅加权

因为 rewrite seed 可能更长、更慢、更不稳定。

因此 bonus 应该是保守的，只作为：

- 二级调度偏好；

而不是替代 `calculate_score()` 的主逻辑。

---

### 4.8 增加系统化日志，支撑实验和论文

当前 stall 相关日志只保存：

- `stall-interactions/prompt-*`
- `stall-interactions/response-*`

见：

- `ChatAFL/afl-fuzz.c:6965`
- `ChatAFL/afl-fuzz.c:6979`

这远远不够。

建议新增：

#### 4.8.1 `llm-rewrite-log.csv`

每次 rewrite 记录：

- time
- seed_id
- parent_seed_id
- target_state_id
- trigger_type
- rewrite_mode
- validation_result
- new_coverage
- new_state
- new_transition
- exec_us
- accepted_to_queue

#### 4.8.2 `llm-rewrite-failures.jsonl`

保存失败样本及失败原因：

- 原 seed 摘要
- 重写结果摘要
- fail_reason
- protocol
- state_sequence_before
- state_sequence_after

#### 4.8.3 `seed-evolution.dot` 或 `seed-evolution.csv`

记录 seed 衍化关系：

- 原始 seed
- mutation seed
- rewrite seed
- 覆盖收益
- 状态收益

这样论文中可以给出很强的案例图，而不是只报表格。

---

## 5. 建议的代码修改路线

---

### 第一阶段：先做最小可跑版本

目标：在不大改主循环的前提下，让“反馈驱动重写”跑起来。

#### 修改点

1. `ChatAFL/afl-fuzz.c`
   - 扩展 `struct queue_entry`
   - 给每条 seed 维护 `no_gain_cycles`
   - 在 `common_fuzz_stuff()` / `save_if_interesting()` 之后更新 per-seed 收益统计
   - 新增 `select_rewrite_candidate()`

2. `ChatAFL/chat-llm.c`
   - 新增 `construct_prompt_seed_rewrite()`
   - 新增 `rewrite_seed_with_feedback()`
   - 扩展 `validate_protocol_request_message()`
   - 新增 `validate_protocol_request_sequence()`

3. `ChatAFL/aflnet.c`
   - 尽量复用已有 `extract_requests_*()`，不做大改

#### 这一阶段先不做

- 不做复杂在线状态机修正；
- 不做强化学习式调度；
- 不做字段级自动归因模型。

这样可以快速拿到第一批结果。

---

### 第二阶段：把反馈从 coverage 扩展到 state transition

第一阶段先用：

- `is interesting`
- `has_new_cov`

第二阶段再加入：

- `new state`
- `new transition`
- `transition depth increase`

这样可以在论文里形成两层结果：

1. 先证明 coverage 提升；
2. 再证明深状态探索也提升。

---

### 第三阶段：按协议扩展 validator

优先级建议：

1. RTSP
2. FTP
3. HTTP
4. SIP

原因：

- RTSP 代码和实验基础最成熟；
- FTP/HTTP/SIP 更容易展示泛化性；
- 审稿人更容易接受“不是只对一个协议写特判”。

---

## 6. 预期效果

下面给的是基于当前代码结构的保守预期，不是实测结论。

### 6.1 对覆盖率的预期

相对当前 ChatAFL：

- Branch/edge coverage：`+5% ~ +12%`
- 路径数：`+8% ~ +18%`

提升来源主要是：

- 原本长期无收益但高潜力的 seed 被重新激活；
- LLM 不再只是补消息类型，而是定向修复上下文条件；
- 新 seed 更可能通过协议解析和状态校验，减少无效执行。

### 6.2 对状态探索的预期

对强状态协议更明显：

- 新状态数：`+10% ~ +25%`
- 新状态转移数：`+12% ~ +30%`

原因：

- rewrite 不再只做字节扰动，而是有机会修复“上一跳到下一跳”的协议依赖；
- 对 `Session`、`CSeq`、认证顺序等上下文敏感字段更友好。

### 6.3 对 LLM 成本效率的预期

如果加了 seed ranking 和 cooldown：

- 单位 LLM 调用收益会明显提高；
- 无效 LLM 调用占比会下降。

建议重点报告指标：

- `accepted_rewrite / total_rewrite`
- `interesting_rewrite / accepted_rewrite`
- `new_transition_rewrite / total_rewrite`
- `coverage_gain_per_llm_call`

这是比“总调用次数”更重要的指标。

### 6.4 对稳定性的预期

通过 validator 和轻量试执行：

- 无效 seed 入队率会下降；
- 因 LLM 输出脏数据造成的污染会下降；
- 实验波动性会变小。

这对论文非常重要，因为稳定性提升本身就是贡献。

---

## 7. 创新性分析

这一方向的创新点不能写成“我们把 coverage 喂给 LLM”，那样太浅。

建议概括成以下 4 点。

### 7.1 从 coverage-guided fuzzing 变成 coverage-guided seed rewriting

传统 AFL 的 feedback 只用于：

- 决定输入是否 interesting；
- 调整能量；
- 选择 favored seed。

你的改进把 feedback 往前推进了一层：

- 它不只影响“怎么 fuzz”，
- 还影响“该重写哪条 seed、如何重写 seed”。

这就是新意。

### 7.2 将 LLM 从“生成器”变成“在线种子修复器”

原始 ChatAFL 的 LLM 更像：

- grammar extractor
- enrichment generator
- stall helper

改进后，LLM 的角色变成：

- 针对失败反馈做定向修复的在线优化器。

这个角色变化本身就值得写成系统贡献。

### 7.3 把 coverage 与 state feedback 联合用于 seed 优化

这不是纯 coverage 优化，而是协议 fuzzing 场景下的：

- coverage + state transition + protocol validation

联合反馈。

这使得它和普通 program fuzzing 上的 prompt rewrite 工作区分开来。

### 7.4 反馈不仅决定是否保留，还决定失败原因归类

如果系统能稳定区分：

- 语法失败；
- 上下文失败；
- 状态失败；
- 收益失败；

那么它就比“黑盒 LLM + 黑盒 fuzzing”更可解释。

可解释性是论文里很强的一项卖点。

---

## 8. 为什么这种方法可以发表论文

这条路线可以发论文，不是因为“用了 LLM”，而是因为它满足论文贡献的三个硬条件。

### 8.1 它解决的是一个真实且尚未被原始 ChatAFL 解决的问题

原始 ChatAFL 的主要问题不是“没有 LLM”，而是：

- LLM 生成是开环的；
- 缺少按 seed 的反馈优化；
- 缺少系统化收益归因。

你的方案正面解决这个问题，因此不是简单补丁。

### 8.2 它有清晰的方法闭环

方法链条是完整的：

1. 识别高潜力低收益 seed；
2. 抽取覆盖率/状态/验证反馈；
3. 用 LLM 做定向重写；
4. 用多级 validator 准入；
5. 把收益重新记录回 seed 和 queue。

这是一套完整 framework，而不是零散技巧。

### 8.3 它可以做很强的实验设计

可以做以下对比组：

1. `AFLNet`
2. `ChatAFL`
3. `ChatAFL + Rewrite w/o validation`
4. `ChatAFL + Rewrite + syntax validation`
5. `ChatAFL + Rewrite + syntax + state feedback`
6. `ChatAFL + Full Feedback-Driven Seed Optimization`

可以做以下指标：

- edge/branch coverage
- reached states
- reached transitions
- unique crashes
- accepted rewrite ratio
- useful rewrite ratio
- coverage gain per LLM call
- new transition gain per LLM call

这组实验能很好回答：

- 是否有效；
- 为什么有效；
- 哪一层贡献最大。

### 8.4 它兼具系统性和领域针对性

这点非常关键。

如果只是“LLM 帮忙改输入”，论文会显得通用但浅。
如果强调：

- 有状态协议；
- 会话依赖；
- 状态转移；
- 协议验证；

那么它就变成了非常明确的 protocol fuzzing contribution。

### 8.5 它的实现成本与论文收益比很高

这条路线不需要：

- 重新训练模型；
- 构建大型语料集；
- 重写 AFL 主体；
- 改协议服务端。

主要工作是：

- 扩展元数据；
- 加 validator；
- 加 prompt；
- 加日志与实验。

这类工作最适合做系统论文或工程型安全论文。

---

## 9. 建议的论文贡献表述

如果后续你真的按这个方向推进，论文贡献建议写成：

1. 提出一种面向有状态协议 fuzzing 的反馈驱动种子优化框架，将 coverage/state feedback 从被动保留机制提升为主动 seed rewriting 信号。
2. 设计一种高潜力低收益 seed 识别策略，选择最值得被 LLM 重写的协议消息序列，降低无效 LLM 调用。
3. 设计多级验证与收益归因机制，对 LLM 重写结果进行语法、会话上下文、状态推进和覆盖率收益验证。
4. 在 ChatAFL 上实现原型，并在多种协议目标上证明其能提高覆盖率、状态转移探索深度和单位 LLM 调用收益。

---

## 10. 最务实的落地建议

如果你接下来要真正动手做，不建议一开始就把方案铺太大。

最务实的顺序是：

1. 先做 RTSP 单协议版本；
2. 先做 per-seed stagnation tracking；
3. 先做 seed-level rewrite prompt；
4. 先做 sequence validator；
5. 先做 rewrite log；
6. 跑通对比：`ChatAFL` vs `ChatAFL + rewrite`；
7. 再扩展到 FTP / HTTP。

这样可以最快拿到第一轮有效结果，也最容易把论文的“方法 - 实现 - 实验”闭环建立起来。

