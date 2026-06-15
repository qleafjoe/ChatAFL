# 论文草稿 v0：基于语义状态增强与验证驱动的 LLM 协议模糊测试

本文档给出一版可直接进入论文写作的初稿框架。  
定位为：

- **主线贡献**：State Machine Enhancement
- **支撑模块**：Validation-Driven LLM Fuzzing
- **暂不作为主线**：Feedback-Driven Seed Optimization

目标不是一次性写成终稿，而是先把题目、摘要、贡献点、研究问题、实验设计、消融结构和表格骨架起出来，便于后续边实现边填数据。

---

## 1. 题目候选

建议先准备 3 个版本。

### 1.1 偏稳健系统风格

**State-Semantics-Augmented LLM-Guided Protocol Fuzzing with Validation-Driven Input Control**

优点：

- 最准确地覆盖了两条主线；
- 学术表达完整；
- 适合系统型论文。

风险：

- 略长；
- 标题信息密度高。

### 1.2 偏协议 fuzzing 风格

**Boosting Stateful Protocol Fuzzing with Semantic State Modeling and Validation-Driven LLM Guidance**

优点：

- 更强调“提升有状态协议 fuzzing”；
- 比较适合把 ChatAFL 当直接基线来写。

### 1.3 偏简洁可投稿风格

**Semantic State-Guided and Validation-Aware LLM Fuzzing for Network Protocols**

优点：

- 更短；
- 适合摘要和题目风格统一。

### 1.4 当前推荐标题

如果现在就选一个工作标题，我建议用：

**Boosting Stateful Protocol Fuzzing with Semantic State Modeling and Validation-Driven LLM Guidance**

---

## 2. 一版英文摘要

下面这一版可以直接作为论文摘要初稿继续打磨。

### 2.1 Abstract Draft

Large language models (LLMs) have recently shown promise in protocol fuzzing by providing protocol-aware grammars, seed enrichment, and stall-breaking inputs. However, existing LLM-guided fuzzers such as ChatAFL still suffer from two fundamental limitations. First, their state representation is often too coarse, as protocol progress is mainly approximated by response-code sequences, which weakens deep-state exploration. Second, LLM outputs are insufficiently controlled, allowing malformed or context-inconsistent candidates to enter the fuzzing loop and reduce effectiveness. In this paper, we present a stateful protocol fuzzing framework that combines semantic state modeling with validation-driven LLM guidance. Our approach augments raw response-code states with request semantics and lightweight protocol context, enabling more precise state selection and state-conditioned next-step generation. To ensure that LLM outputs are beneficial rather than disruptive, we further introduce a multi-level validation pipeline that filters candidates at the format, grammar, session-context, and execution-benefit levels before they are admitted into grammar extraction, seed enrichment, or stall-breaking stages. We implement our design on top of ChatAFL/AFLNet and evaluate it on representative text-based protocol servers. Our results show that semantic state modeling and validation-driven admission substantially improve transition exploration efficiency, reduce invalid LLM-assisted inputs, and improve the cost-effectiveness and interpretability of LLM-guided protocol fuzzing.

---

## 3. 一版中文摘要

### 3.1 中文摘要初稿

大语言模型（LLM）近年来已被用于协议模糊测试，通过辅助语法提取、种子丰富和停滞突破，提升了协议输入生成的质量与状态探索能力。然而，以 ChatAFL 为代表的现有方法仍然存在两个关键局限。其一，现有状态表示通常主要依赖响应码序列，难以刻画请求语义、会话阶段和关键上下文依赖，导致深层状态探索效率不足。其二，LLM 输出缺乏统一而严格的准入控制，格式错误、上下文不一致或对状态推进无收益的候选输入仍可能进入 fuzzing 主循环，影响探索效率与结果可解释性。针对上述问题，本文提出一种结合**语义状态增强**与**验证驱动 LLM 引导**的有状态协议模糊测试框架。我们在原始响应状态基础上引入请求类型、协议阶段和轻量上下文摘要，构建更细粒度的语义状态表示，以支持状态收益感知的目标状态调度和状态条件化的下一跳消息生成。同时，我们设计了多级验证管线，在 grammar extraction、seed enrichment 和 stall breaking 三条 LLM 数据路径上，对候选输入进行格式、语法、会话上下文和执行收益验证，确保只有高质量且有潜在收益的样本才能进入模糊测试主循环。我们基于 ChatAFL/AFLNet 实现原型系统，并在多个文本协议服务上进行评测。实验结果表明，该方法能够有效提升状态转移探索效率，降低无效 LLM 输入比例，并提高 LLM 辅助协议模糊测试的成本收益比与机制可解释性。

---

## 4. 论文核心故事

这篇论文的主故事不应写成：

- “我们把 LLM 用得更好了”
- “我们优化了 prompt”
- “我们做了更多过滤”

而应写成：

> 现有 ChatAFL 类方法已经证明了 LLM 能提供协议知识，但它们在状态建模和输出控制上仍然不够精细，导致深层状态探索不足、LLM 样本质量不稳定。为此，我们提出一种语义状态增强与验证驱动结合的协议 fuzzing 框架，使 LLM 从粗粒度辅助生成器，升级为面向语义状态目标、受本地准入控制约束的候选输入提供者。

换句话说，这篇论文真正回答的是两个问题：

1. 为什么已有 LLM-guided protocol fuzzing 到了深层状态会越来越吃力？
2. 如何让 LLM 输出既更“懂状态”，又更“受控制”？

---

## 5. 研究问题

建议论文中明确提出 3 个研究问题。

### 5.1 RQ1：粗粒度响应状态是否限制了深层协议状态探索？

要验证的问题：

- 原始 ChatAFL 的响应码状态是否存在明显状态别名；
- 语义增强状态是否能更准确地区分不同会话阶段；
- 是否能提升 deep-state exploration efficiency。

### 5.2 RQ2：验证驱动的准入机制能否提高 LLM 辅助 fuzzing 的有效性与稳定性？

要验证的问题：

- 它能否减少 invalid / reject / no-gain 的 LLM 输出；
- 它能否提高 accepted sample ratio；
- 它能否提高 per-LLM-call 的状态转移收益。

### 5.3 RQ3：语义状态增强与验证驱动结合后，能否整体提升协议 fuzzing 的状态探索与覆盖效率？

要验证的问题：

- 状态数、状态转移数、time-to-first-deep-state 是否提升；
- transitions/hour、new transitions per LLM call 是否提升；
- coverage 是否同步受益。

---

## 6. 论文贡献点

建议把贡献点控制在 4 条，不要贪多。

### 6.1 Contribution 1

We identify that coarse response-code-based states and weak LLM output control are two key bottlenecks in existing LLM-guided protocol fuzzing systems.

中文表达：

1. 识别出现有 LLM 协议 fuzzing 在深层状态探索中面临的两个关键瓶颈：粗粒度响应状态表示，以及缺乏统一准入控制的 LLM 输出接入方式。

### 6.2 Contribution 2

We propose a semantic state modeling mechanism that augments raw response-code states with request semantics and lightweight protocol context, enabling more accurate state selection and state-conditioned LLM guidance.

中文表达：

2. 提出语义状态增强机制，将请求类型、协议阶段和轻量上下文摘要引入状态建模，以支持更精细的目标状态调度和状态条件化生成。

### 6.3 Contribution 3

We design a validation-driven LLM admission pipeline that filters grammar templates, enriched seeds, and stall-breaking candidates through format, grammar, session-context, and execution-benefit validation.

中文表达：

3. 设计验证驱动的 LLM 准入管线，在 grammar extraction、seed enrichment 和 stall breaking 三条路径上统一实施格式、语法、会话上下文和收益验证。

### 6.4 Contribution 4

We implement the proposed design on top of ChatAFL/AFLNet and demonstrate improved transition exploration efficiency, reduced invalid LLM-assisted inputs, and better LLM cost-effectiveness on representative protocol servers.

中文表达：

4. 在 ChatAFL/AFLNet 上实现原型系统，并通过多协议实验表明，该方法能够提升状态转移探索效率，降低无效 LLM 样本比例，并提高 LLM 调用的成本收益比与结果可解释性。

---

## 7. 方法总览

论文方法部分建议按 4 个模块写。

### 7.1 模块 A：语义状态增强

核心点：

- 原始状态：主要是 `response_code sequence`
- 增强状态：`response_code + request_type + phase_flags + context summary`

作用：

- 缓解状态别名；
- 让状态选择不再只依赖响应码；
- 为 LLM 提供“当前处于什么会话语义阶段”的条件。

### 7.2 模块 B：状态收益驱动调度

核心点：

- 根据稀有度、深度、边界性、reject ratio、历史收益对状态打分；
- 目标状态选择不再只是“过去收益高”，而是“未来突破价值高”。

### 7.3 模块 C：状态条件化 LLM 生成

核心点：

- LLM 不再只看原始 history；
- 而是看当前语义状态、目标探索目标、失败模式、成功示例；
- 输出下一条最可能推动状态机前进的请求。

### 7.4 模块 D：验证驱动准入

核心点：

- 对 LLM 输出做统一 gate；
- 覆盖 grammar / enrichment / stall 三条路径；
- 记录失败原因和收益结果。

---

## 8. 方法章节建议结构

建议论文方法章节这样排：

### 8.1 Overview

- 用一张系统总图展示：
  - Seed queue
  - Semantic state model
  - State selector
  - LLM guidance module
  - Validation pipeline
  - Fuzzing execution loop

### 8.2 Semantic State Modeling

写清：

- 为什么 raw response state 不够；
- 增强状态的定义；
- 如何从请求和响应中提取上下文；
- 如何构造 enhanced state key。

### 8.3 State-Aware Target Selection

写清：

- 当前 AFLNet/ChatAFL 的状态选择偏好；
- 新评分函数；
- 怎样优先探索 high-potential frontier states。

### 8.4 State-Conditioned LLM Guidance

写清：

- stall prompt 如何升级；
- 输入包含哪些状态摘要；
- 如何限制 LLM 输出只生成一个合法下一跳。

### 8.5 Validation-Driven Admission Control

写清：

- validation stages
- failure taxonomy
- admission rules
- dry-run / benefit validation

### 8.6 Complexity and Overhead

这节不要省。

要说明：

- 额外开销主要来自轻量状态摘要和 validation；
- 没有引入大型离线训练或复杂 agent 系统；
- 开销换来了更高的状态推进效率和更低的无效 LLM 调用浪费。

---

## 9. 实验设计

## 9.1 对比对象

最低建议对比：

1. `AFLNet`
2. `ChatAFL`
3. `Ours-ValidationOnly`
4. `Ours-StateOnly`
5. `Ours-Full`

如果资源有限，最少也要有：

1. `ChatAFL`
2. `Ours-StateOnly`
3. `Ours-Full`

### 9.1.1 对比逻辑

- `ChatAFL -> Ours-ValidationOnly`
  证明验证驱动本身能提高输入质量和 LLM 成本收益。
- `ChatAFL -> Ours-StateOnly`
  证明语义状态增强本身能提升状态探索。
- `Ours-StateOnly -> Ours-Full`
  证明 validation 与 state semantics 结合有额外收益。

---

## 9.2 评测目标

建议分两层写。

### 9.2.1 Pilot

- Live555
- PureFTPD

用途：

- 快速迭代；
- 验证机制；
- 生成案例。

### 9.2.2 Main Evaluation

应尽量覆盖 benchmark 中稳定可跑的文本协议目标。  
至少争取：

- RTSP / FTP / HTTP / SIP 中各有代表

如果你最终还是只能稳定跑 2 到 4 个目标，也可以发，但必须：

- 增加 repeated runs；
- 增加强机制指标；
- 减少过度泛化表述。

---

## 9.3 实验指标

论文里建议分 4 类指标。

### 9.3.1 最终效果指标

- `branches`
- `branch coverage`
- `states`
- `transitions`
- `unique crashes`
- `unique hangs`

### 9.3.2 效率指标

- `branches/hour`
- `states/hour`
- `transitions/hour`
- `time-to-first-new-state`
- `time-to-first-deep-state`

### 9.3.3 机制指标

- `LLM output acceptance rate`
- `format fail ratio`
- `grammar fail ratio`
- `context fail ratio`
- `state fail ratio`
- `new transitions per LLM call`
- `coverage per LLM call`

### 9.3.4 健壮性指标

- repeated-run mean/std
- replayable hang/crash ratio
- reject ratio
- timeout ratio

---

## 9.4 消融实验设计

这篇论文很依赖消融。建议最少做下面 4 组：

### 9.4.1 Ablation A：去掉验证模块

`Full - Validation`

看：

- accepted sample ratio 是否下降；
- invalid / reject 是否上升；
- per-LLM-call 收益是否下降。

### 9.4.2 Ablation B：去掉语义状态增强

`Full - SemanticState`

看：

- deep-state exploration 是否变差；
- transitions/hour 是否下降；
- time-to-deep-state 是否变长。

### 9.4.3 Ablation C：只保留格式验证

`FormatOnly Validation`

用于证明：

- 仅做表面清洗不够；
- context/state validation 才是真正重要的增益来源。

### 9.4.4 Ablation D：只做状态增强，不启用状态条件化 LLM prompt

用于证明：

- 状态增强不仅能改善调度；
- 结合状态条件化生成后增益更完整。

---

## 10. 结果表模板

下面这些表格可以直接作为后续填数据的骨架。

### 10.1 主结果表

| Target | Fuzzer | Branches | States | Transitions | Branches/h | Transitions/h | Crashes | Hangs |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Live555 | AFLNet |  |  |  |  |  |  |  |
| Live555 | ChatAFL |  |  |  |  |  |  |  |
| Live555 | Ours-Full |  |  |  |  |  |  |  |
| PureFTPD | AFLNet |  |  |  |  |  |  |  |
| PureFTPD | ChatAFL |  |  |  |  |  |  |  |
| PureFTPD | Ours-Full |  |  |  |  |  |  |  |

### 10.2 机制指标表

| Target | System | Acceptance Rate | Format Fail | Grammar Fail | Context Fail | State Fail | New Transitions / LLM Call |
|---|---|---:|---:|---:|---:|---:|---:|
| Live555 | ChatAFL |  |  |  |  |  |  |
| Live555 | Ours-Full |  |  |  |  |  |  |
| PureFTPD | ChatAFL |  |  |  |  |  |  |
| PureFTPD | Ours-Full |  |  |  |  |  |  |

### 10.3 消融表

| System Variant | Branches | States | Transitions | Acceptance Rate | Transitions / LLM Call |
|---|---:|---:|---:|---:|---:|
| ChatAFL |  |  |  |  |  |
| Ours-FormatOnly |  |  |  |  |  |
| Ours-ValidationOnly |  |  |  |  |  |
| Ours-StateOnly |  |  |  |  |  |
| Ours-Full |  |  |  |  |  |

### 10.4 Overhead 表

| System | Execs/sec | Avg LLM Calls | Avg Validation Overhead | Avg Exec Time | Total Cost Proxy |
|---|---:|---:|---:|---:|---:|
| ChatAFL |  |  |  |  |  |
| Ours-Full |  |  |  |  |  |

---

## 11. 图示建议

建议至少准备 4 类图。

### 11.1 系统总图

内容：

- Fuzzer loop
- Semantic state extractor
- State selector
- LLM generator
- Validation pipeline
- Execution feedback

### 11.2 状态图对比

内容：

- Raw state graph
- Enhanced semantic state graph

目的：

- 直观展示“粗粒度状态别名”问题。

### 11.3 时间曲线

内容：

- transitions over time
- states over time
- branches over time

建议把 `transitions over time` 放主图，coverage 放辅助图。

### 11.4 失败类型分布图

内容：

- format fail
- grammar fail
- context fail
- state fail
- no gain

这张图对 Validation-Driven 部分非常关键。

---

## 12. 预期结果怎么写才合理

现在不要把目标写成：

- “branch coverage 大幅翻倍”
- “发现大量新漏洞”

这不稳。

更合理的预期表述是：

1. 语义状态增强首先提升的是 `states / transitions / time-to-deep-state`；
2. 验证驱动首先提升的是 `acceptance rate / invalid ratio / transitions per LLM call`；
3. coverage 会受益，但未必是最显著的一项；
4. 即便 crash 数不暴涨，只要机制指标和状态推进指标显著更好，这篇论文依然成立。

---

## 13. 讨论与局限性

这一节建议提前想好，不要最后被动补。

### 13.1 适用范围

本方法更适合：

- 文本协议
- 半结构化协议
- 有明确请求边界和轻量上下文依赖的协议

### 13.2 局限性

- 对复杂二进制协议支持有限；
- 仍依赖本地 validator 的质量；
- 若目标协议状态很浅，则语义状态增强带来的收益可能有限；
- 若协议服务端高度非确定性，则状态收益统计会受噪声影响。

### 13.3 外部有效性

- 目前主要在文本协议上评估；
- 向二进制协议推广需要额外的字段恢复与状态摘要机制。

---

## 14. 一版引言开头

下面是一版可直接改写成论文 Introduction 的开头。

### 14.1 Introduction Draft

Network protocol fuzzing is fundamentally harder than file-format fuzzing because valid exploration often depends not only on input syntax, but also on session context and multi-step state transitions. Recent work such as ChatAFL shows that large language models can help protocol fuzzers infer message templates, enrich seed sequences, and generate stall-breaking requests from protocol documents and interaction history. However, our analysis shows that existing LLM-guided protocol fuzzers still struggle to efficiently reach deep protocol states. A key reason is that they rely on coarse-grained response-code states that fail to capture important protocol semantics, such as request roles, session establishment, and phase-specific dependencies. Another reason is that LLM outputs are still admitted into the fuzzing workflow with insufficient runtime control, allowing malformed, context-inconsistent, or low-benefit candidates to consume fuzzing budget.

To address these limitations, we propose a fuzzing framework that combines semantic state modeling with validation-driven LLM guidance. Instead of treating protocol progress as plain response-code transitions, we enrich states with request semantics and lightweight protocol context, enabling the fuzzer to distinguish semantically different stages even when raw response codes are similar. In parallel, instead of trusting LLM outputs as direct fuzzing inputs, we subject them to a multi-level validation pipeline before they are used for grammar extraction, seed enrichment, or stall breaking. This design allows the fuzzer to ask not only whether an LLM output looks valid, but also whether it is context-consistent and likely to provide exploration benefit.

---

## 15. 一版结论段方向

Conclusion 不要写成泛泛而谈的“LLM 很有前景”，建议收束成：

1. 现有 LLM-guided protocol fuzzing 的主要瓶颈不是“有没有协议知识”，而是“状态是否表达得足够好、输出是否控制得足够严”；
2. 语义状态增强和验证驱动准入是两个互补机制；
3. 它们共同提升了深层状态探索效率和 LLM 调用收益；
4. 这为后续把 LLM 更可靠地接入复杂协议 fuzzing 提供了方向。

---

## 16. 现在最应该补的实现与数据

为了把这份草稿真正变成论文，接下来最关键的不是继续写文档，而是尽快补下面这些数据。

### 16.1 第一优先级

1. `Validation-Driven` 最小实现
2. 失败类型日志
3. `LLM acceptance rate`
4. `new transitions per LLM call`

### 16.2 第二优先级

1. 语义增强状态表示
2. 状态调度改造
3. `semantic states / semantic transitions`
4. `time-to-first-deep-state`

### 16.3 第三优先级

1. 状态条件化 stall prompt
2. 多轮重复实验
3. AFLNet / ChatAFL / Ours 对照结果

---

## 17. 当前建议

这版草稿对应的最稳论文路线是：

**标题层面主打 State Semantics，方法层面用 Validation-Driven 做可靠性支撑，实验层面主打 transition efficiency 和 LLM cost-effectiveness。**

如果后续你想继续推进，我建议下一步直接补两样东西：

1. 论文的 `Introduction + Method Overview` 正式文字版
2. 一份 `实验清单与结果记录模板`

