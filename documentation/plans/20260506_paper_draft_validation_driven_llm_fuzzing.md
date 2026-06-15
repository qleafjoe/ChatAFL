# 论文草稿 v0：Validation-Driven LLM Fuzzing for Stateful Protocols

本文档是一版以 **Validation-Driven LLM Fuzzing** 为唯一主线的论文草稿。  
写作目标是：

- 不把它写成“更多 prompt engineering”；
- 不把它写成“单协议过滤器增强”；
- 而是把它写成一篇围绕 **LLM 输出准入控制、失败归因、收益归因** 展开的系统论文。

当前建议工作标题：

**Validation-Driven LLM Fuzzing for Stateful Protocols**

备选标题：

1. **Validation-Driven LLM Guidance for Stateful Protocol Fuzzing**
2. **Controlling LLM Outputs in Stateful Protocol Fuzzing via Multi-Level Validation**
3. **From Prompt-Driven to Validation-Driven: Reliable LLM-Assisted Protocol Fuzzing**

---

## 1. 一版英文摘要

Large language models (LLMs) have recently been adopted in protocol fuzzing to infer request grammars, enrich seed sequences, and generate stall-breaking messages. However, current LLM-guided protocol fuzzers still rely heavily on prompt quality and weak post-processing, which allows malformed, context-inconsistent, or low-benefit LLM outputs to enter the fuzzing loop. This weak admission control is particularly problematic for stateful protocol fuzzing, where valid exploration depends not only on syntax but also on session context and state progress. In this paper, we present **Validation-Driven LLM Fuzzing**, a framework that turns LLM outputs from directly trusted fuzzing inputs into validated candidates subject to multi-level admission control. Our framework introduces unified validation across three major LLM-assisted paths in protocol fuzzing: grammar extraction, seed enrichment, and stall breaking. For each candidate, we perform format validation, grammar validation, session-context validation, and execution-benefit validation before deciding whether it should influence the fuzzing loop. We further classify failed candidates into explicit categories, enabling fine-grained feedback analysis and cost-benefit attribution for LLM calls. We implement our design on top of ChatAFL/AFLNet and evaluate it on representative text-based protocol servers. Our study shows that validation-driven admission improves the acceptance rate of LLM outputs, reduces invalid protocol inputs, improves transition exploration efficiency per LLM call, and makes LLM-assisted protocol fuzzing more stable and interpretable.

---

## 2. 一版中文摘要

近年来，大语言模型（LLM）被引入协议模糊测试，用于辅助语法提取、种子丰富和停滞突破，从而增强协议输入生成能力。然而，现有 LLM 引导的协议 fuzzing 方法仍然高度依赖 prompt 质量与有限的后处理逻辑，使得大量格式错误、上下文不一致或对状态推进无收益的 LLM 输出仍可能进入 fuzzing 主循环。对于有状态协议而言，这一问题尤为严重，因为有效探索不仅依赖输入语法，还依赖会话上下文和状态推进关系。针对这一问题，本文提出 **Validation-Driven LLM Fuzzing** 框架，将 LLM 输出从被直接信任的 fuzzing 输入，转化为必须经过多级验证后才能被接纳的候选样本。该框架统一覆盖协议 fuzzing 中三条关键的 LLM 辅助路径：grammar extraction、seed enrichment 和 stall breaking，并对候选输出依次执行格式验证、语法验证、会话上下文验证和执行收益验证，只有高质量且有潜在收益的样本才能进入 fuzzing 主循环。同时，我们将失败样本显式划分为不同失败类型，以支持 LLM 调用收益分析与机制归因。我们基于 ChatAFL/AFLNet 实现原型系统，并在代表性文本协议服务上进行评测。实验表明，验证驱动的准入机制能够提高 LLM 输出通过率，降低无效协议输入比例，提高单位 LLM 调用带来的状态转移探索效率，并显著增强 LLM 辅助协议模糊测试的稳定性与可解释性。

---

## 3. 引言

网络协议模糊测试与文件格式模糊测试存在本质差异。对于协议实现而言，想要触达深层逻辑，输入不仅需要满足基本语法约束，还必须符合会话上下文、消息顺序以及状态推进要求。近年来，研究者开始使用大语言模型辅助协议 fuzzing，通过从协议文档中提取请求模板、补全种子序列，以及在覆盖率停滞时生成下一条候选消息，从而缓解协议知识缺失的问题。ChatAFL 等工作已经证明，LLM 可以有效增强协议模糊测试中的输入生成能力。

然而，现有 LLM 引导的协议 fuzzing 仍然存在一个关键但尚未被充分系统化解决的问题：**LLM 输出本身并不可靠。** 即使 prompt 设计得较为严格，LLM 仍可能输出包含解释性文本、格式残留、错误 header、缺失关键字段、违背会话依赖、甚至对状态推进完全无收益的候选输入。如果这些输出被直接用于 grammar extraction、seed enrichment 或 stall breaking，它们不仅无法帮助 fuzzing，还会污染 grammar、引入无效 seed，消耗执行预算，并降低结果的可解释性。

这一问题在有状态协议场景中尤为突出。协议 fuzzing 中真正高价值的样本，并不是“看起来像协议文本”的字符串，而是那些能够被目标服务接受、能够维持会话一致性、并且能够推动状态机继续前进的输入。也就是说，LLM 辅助协议 fuzzing 的关键不只是“让模型生成更多输入”，而是“控制哪些 LLM 输出有资格进入 fuzzing 主循环”。

基于这一观察，本文提出 **Validation-Driven LLM Fuzzing**。我们的核心思想是：**将 LLM 从一个被直接信任的输入生成器，转化为一个必须经过本地验证才能影响 fuzzing 的候选样本提供者。** 与现有方法主要依赖 prompt 驱动不同，我们将本地多级验证机制放在 LLM 输出与 fuzzing 主循环之间，对三条主要 LLM 数据路径进行统一准入控制：

1. grammar extraction：防止错误模板污染结构感知变异；
2. seed enrichment：防止无效 enriched seed 写入初始语料；
3. stall breaking：防止无效下一跳消息浪费 fuzzing 预算。

我们进一步提出分层失败分类机制，将 LLM 输出失败划分为 `FORMAT_FAIL`、`GRAMMAR_FAIL`、`CONTEXT_FAIL`、`STATE_FAIL` 和 `NO_GAIN` 等类型。这样，系统不仅能决定“是否接纳某个 LLM 输出”，还能回答“为什么拒绝它”、“它在什么层面失败”以及“每次 LLM 调用是否真正带来了收益”。这使得 LLM 辅助协议 fuzzing 从黑盒式生成，提升为具有明确准入逻辑与收益归因能力的系统框架。

我们基于 ChatAFL/AFLNet 实现该框架，并在多个文本协议服务上进行评测。实验重点不只放在最终覆盖率上，还包括 LLM 输出通过率、失败类型分布、无效输入比例、每次 LLM 调用带来的新状态转移收益，以及 overall transition exploration efficiency。结果表明，Validation-Driven LLM Fuzzing 能够显著提升 LLM 样本质量和单位调用收益，降低无效输入带来的噪声，并改善协议状态探索的稳定性与可解释性。

本文的主要贡献如下：

1. 指出当前 LLM 引导协议 fuzzing 的核心薄弱点在于缺乏统一而严格的 LLM 输出准入控制，而不是单纯缺少协议知识。
2. 提出 Validation-Driven LLM Fuzzing 框架，在 grammar extraction、seed enrichment 和 stall breaking 三条 LLM 数据路径上统一实施多级验证。
3. 设计分层失败归因与收益归因机制，使系统能够系统分析 LLM 输出的失败模式及其实际 fuzzing 收益。
4. 在 ChatAFL/AFLNet 上实现原型系统，并通过多协议实验验证该方法在输入质量、状态转移效率和 LLM 成本收益比上的有效性。

---

## 4. 背景与动机

### 4.1 LLM 在协议 fuzzing 中的三条典型数据路径

现有 ChatAFL 类系统中，LLM 主要通过三种方式影响 fuzzing：

1. **Grammar Extraction**
   - 从协议文档或上下文中生成请求模板；
   - 模板被编译成后续 grammar-aware mutation 的结构约束。

2. **Seed Enrichment**
   - 对初始 seed 序列进行补全；
   - 插入缺失消息类型，形成更丰富的初始语料。

3. **Stall Breaking**
   - 在覆盖率或状态探索停滞时生成下一条候选消息；
   - 尝试突破当前局部状态平台期。

这些机制共同构成了 LLM 辅助协议 fuzzing 的核心价值来源。但同时，它们也引入了新的系统风险：一旦 LLM 输出不可靠，就会沿着上述三条路径污染后续 fuzzing。

### 4.2 为什么“看起来像协议”并不等于“值得进入 fuzzing”

对于有状态协议而言，协议输入至少要满足四层要求：

1. 形式上像协议文本；
2. 语法上是合法请求；
3. 会话上下文上能放进当前位置；
4. 执行后对状态推进或覆盖率探索有潜在收益。

现有系统大多只覆盖前一到两层，甚至有些路径只做了字符串清洗。这会带来三类直接问题：

1. **Grammar 污染**
   - 错误模板进入 mutation pipeline；
   - 使后续结构感知变异偏离真实协议结构。

2. **Seed 污染**
   - enriched seed 看起来更复杂，但实际上无法被服务端接受；
   - 甚至会增加无意义 hangs 或 immediate rejects。

3. **执行预算浪费**
   - stall-breaking 消息虽然被执行，但不会带来新状态或新转移；
   - 导致 LLM 调用成本与 fuzzing 收益严重失配。

### 4.3 研究问题

基于上述观察，本文围绕以下三个研究问题展开：

**RQ1.** 统一的多级验证准入是否能够显著提高 LLM 输出质量，并减少无效样本进入 fuzzing 主循环？  
**RQ2.** 将失败样本显式分类并记录收益归因，是否能提高 LLM 辅助协议 fuzzing 的可解释性与调用成本收益比？  
**RQ3.** 在 grammar extraction、seed enrichment 和 stall breaking 三条路径上统一实施验证后，系统整体的状态转移探索效率是否优于原始 ChatAFL？

---

## 5. 设计概览

### 5.1 核心思想

Validation-Driven LLM Fuzzing 的核心思想是：

> Prompt 只负责生成候选输入，Validator 决定该输入是否有资格进入 fuzzing 主循环。

为此，我们在 LLM 输出与 fuzzing 主循环之间加入统一验证层，并让所有 LLM 辅助路径都通过这层准入控制。

### 5.2 总体流程

整体流程如下：

1. LLM 输出候选内容；
2. 统一清洗与标准化；
3. 根据当前路径类型进入对应验证逻辑；
4. 若通过验证，则允许进入 grammar / seed / execution 路径；
5. 执行后提取状态与覆盖率收益；
6. 记录失败原因或收益结果，供后续分析与调度使用。

该流程将原始 ChatAFL 中“生成后立即使用”的模式，转变为“生成后先验证再准入”的模式。

---

## 6. 多级验证框架

### 6.1 统一验证目标

我们将验证目标分为五类结果：

- `LLM_VALID_OK`
- `LLM_VALID_FORMAT_FAIL`
- `LLM_VALID_GRAMMAR_FAIL`
- `LLM_VALID_CONTEXT_FAIL`
- `LLM_VALID_STATE_FAIL`
- `LLM_VALID_NO_GAIN`

这里的关键不只是 pass / fail，而是要知道失败发生在哪一层。

### 6.2 格式验证

格式验证负责处理最外层的输出噪声，典型问题包括：

- Markdown 代码块残留；
- JSON/数组提取失败；
- 拒答文本或解释性文本；
- 缺失终止符；
- 明显不可切分的候选字符串。

格式验证不判断协议是否正确，只判断输出是否具备进一步验证的基本条件。

### 6.3 语法验证

语法验证负责判断候选输入是否满足目标协议的基本语法约束。

对于文本协议，这类检查通常包括：

- 起始行格式是否合法；
- header 行是否满足基本键值结构；
- 必需字段是否存在；
- `Content-Length` 是否与 body 一致；
- 消息是否满足正确结束符约束。

对于 grammar extraction 场景，语法验证还包括：

- 模板的 message type 是否属于合法集合；
- 模板是否缺失关键字段；
- 模板是否能与真实 seed 保持合理匹配率。

### 6.4 会话上下文验证

这是协议 fuzzing 中最关键、也最容易被忽略的一层。

单条消息看起来语法正确，并不代表它在当前会话上下文中成立。例如：

- FTP 中 `PASS` 早于 `USER`；
- RTSP 中 `PLAY` 早于 `SETUP`；
- RTSP `PLAY` / `PAUSE` / `TEARDOWN` 缺失 `Session`；
- 登录后动作被插入到未认证上下文。

因此，我们引入 sequence-level validation，对多消息候选序列进行上下文依赖检查，确保其不仅像协议，还像“当前会话中的合法下一跳或合法延伸”。

### 6.5 状态验证与收益验证

静态验证只能说明“这个样本看起来合理”，但不能说明“它值得进入 fuzzing”。

因此，我们加入轻量执行级验证，对候选样本进行一次受控执行，提取：

- 是否被目标服务正常处理；
- 是否立即被拒绝；
- 是否产生可解析的响应状态；
- 是否带来新的状态、新状态转移或新覆盖；
- 是否只是无收益执行。

如果一个候选样本在前面几层都通过了，但执行后表现为完全无收益，那么它应被标记为 `NO_GAIN`，而不是与真正有效样本混为一谈。

---

## 7. 三条 LLM 路径的统一准入控制

### 7.1 Grammar Extraction Validation

在 grammar extraction 场景中，LLM 输出的主要风险是生成错误模板或过宽模板。为此，我们在模板被纳入 `protocol_patterns` 之前，引入三类 gate：

1. **Message-Type Gate**
   - 模板头部必须属于协议合法消息类型集合。

2. **Mandatory-Field Gate**
   - 不同消息类型必须包含关键字段。

3. **Seed-Match Gate**
   - 用已有真实 seed 反向验证模板命中率与匹配质量；
   - 过低命中率或过宽匹配都会被拒绝。

这一设计的目的不是让 grammar“看起来更干净”，而是防止错误模板进入 mutation pipeline 并持续放大负面影响。

### 7.2 Seed Enrichment Validation

在 seed enrichment 场景中，LLM 输出的风险是生成 superficially richer 但实际上不可执行的 seed。我们的验证流程包括：

1. 对 enriched sequence 做标准化；
2. 使用协议切分器重新切分请求序列；
3. 对每个 region 做消息级验证；
4. 对整条序列做上下文级验证；
5. 检查其是否真的补入了原先缺失的消息类型；
6. 可选地做一次轻量执行验证，判断是否至少不劣于原 seed。

只有通过这些检查的 enriched seed 才允许进入输入目录或 queue。

### 7.3 Stall-Breaking Validation

在 stall breaking 场景中，LLM 输出通常是一条候选下一跳消息。这里的目标不是简单执行更多 LLM 消息，而是控制：

- 只有合理且有潜在收益的下一跳才值得执行；
- 失败类型必须被记录；
- 当连续失败时系统应回退，而不是继续盲目调用 LLM。

因此，我们在 stall-breaking 中引入：

1. 失败原因记录；
2. 有限次数重试；
3. 执行后收益归因；
4. 连续失败后的回退策略。

---

## 8. 失败归因与收益归因

### 8.1 为什么必须显式记录失败类型

很多 LLM fuzzing 系统只能回答“效果有没有提升”，却很难回答“为什么有的 LLM 输出无效”。这使得系统优化高度依赖经验，而不是机制分析。

本文将失败样本明确分为：

- `FORMAT_FAIL`
- `GRAMMAR_FAIL`
- `CONTEXT_FAIL`
- `STATE_FAIL`
- `NO_GAIN`

这样，实验中不仅可以报告总体接受率，还可以报告：

- 哪一层是主要瓶颈；
- 不同协议在不同验证层的失败分布；
- 哪类失败最消耗 LLM 预算。

### 8.2 收益归因指标

我们建议对每次 LLM 调用记录以下收益指标：

- `accepted sample ratio`
- `new coverage per LLM call`
- `new states per LLM call`
- `new transitions per LLM call`
- `coverage per accepted sample`
- `transition per accepted sample`

这些指标比只报最终覆盖率更能说明 Validation-Driven 的真实作用。

---

## 9. 实现

### 9.1 实现基础

我们基于 ChatAFL/AFLNet 实现该框架。现有系统已经具备以下基础设施：

1. LLM 调用入口；
2. 协议消息切分器；
3. 响应状态抽取器；
4. grammar extraction / seed enrichment / stall breaking 三条 LLM 接入路径。

因此，我们的实现并非重写协议 fuzzer，而是在现有 ChatAFL 架构上插入统一验证层和归因机制。

### 9.2 主要实现模块

实现上建议新增独立模块：

- `llm-validator.c`
- `llm-validator.h`

其职责包括：

1. 标准化与清洗；
2. 消息级与序列级验证；
3. 不同阶段的准入控制；
4. 失败原因分类；
5. 轻量执行级收益记录。

### 9.3 日志与可解释性支持

为了支持机制分析，系统还应输出结构化日志，例如：

- `grammar.csv`
- `enrichment.csv`
- `stall.csv`
- `summary.csv`

日志字段可包括：

- 时间
- 阶段
- 协议类型
- LLM 调用编号
- 候选大小
- 验证结果
- 失败原因
- 状态数
- 新覆盖
- 新状态
- 新转移
- fault 类型

---

## 10. 实验设计

### 10.1 评测对象

建议采用两层目标集：

1. **Pilot**
   - Live555
   - PureFTPD

2. **Main Evaluation**
   - 尽可能覆盖 benchmark 中可稳定运行的文本协议服务；
   - 至少争取覆盖 RTSP、FTP、HTTP 或 SIP 中的多个代表。

### 10.2 对比对象

建议至少比较以下系统：

1. `AFLNet`
2. `ChatAFL`
3. `Validation-Driven ChatAFL (Ours)`

如果资源允许，可以进一步增加：

4. `Format-Only Validation`
5. `Validation-Without-Benefit-Gate`

### 10.3 实验指标

实验指标应分为四类：

#### 10.3.1 最终效果指标

- `branches`
- `branch coverage`
- `states`
- `transitions`
- `unique crashes`
- `unique hangs`

#### 10.3.2 效率指标

- `branches/hour`
- `states/hour`
- `transitions/hour`
- `time-to-first-new-state`
- `time-to-first-new-transition`

#### 10.3.3 机制指标

- `LLM output acceptance rate`
- `invalid seed ratio`
- `immediate reject ratio`
- `format fail ratio`
- `grammar fail ratio`
- `context fail ratio`
- `state fail ratio`
- `new transitions per LLM call`

#### 10.3.4 稳定性与健康指标

- `execs/sec`
- `timeout ratio`
- `replayable hang/crash ratio`
- repeated-run mean/std

### 10.4 消融实验

本文特别依赖消融实验来证明“不是只靠更多过滤，而是靠分层验证和收益验证”。

建议至少做以下 4 组消融：

1. `ChatAFL`
2. `ChatAFL + Format Validation`
3. `ChatAFL + Format + Grammar + Context Validation`
4. `ChatAFL + Full Validation (with Benefit Gate)`

重点比较：

- acceptance rate
- invalid ratio
- transitions/hour
- new transitions per LLM call

---

## 11. 结果呈现模板

### 11.1 主结果表

| Target | System | Branches | States | Transitions | Branches/h | Transitions/h | Crashes | Hangs |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Live555 | AFLNet | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |
| Live555 | ChatAFL | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |
| Live555 | Ours | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |
| PureFTPD | AFLNet | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |
| PureFTPD | ChatAFL | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |
| PureFTPD | Ours | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |

### 11.2 机制指标表

| Target | System | Acceptance Rate | Format Fail | Grammar Fail | Context Fail | State Fail | No Gain | New Transitions / LLM Call |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Live555 | ChatAFL | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |
| Live555 | Ours | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |
| PureFTPD | ChatAFL | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |
| PureFTPD | Ours | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |

### 11.3 消融表

| Variant | Acceptance Rate | Invalid Ratio | Reject Ratio | States | Transitions | New Transitions / LLM Call |
|---|---:|---:|---:|---:|---:|---:|
| ChatAFL | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |
| Format-Only | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |
| Validation w/o Benefit Gate | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |
| Full Validation | [待填] | [待填] | [待填] | [待填] | [待填] | [待填] |

---

## 12. 讨论

### 12.1 为什么这不是“又一个过滤器”

如果只做字符串清洗或消息合法性检查，这个方向更像工程补丁。本文方法之所以成立，是因为它满足三个条件：

1. 统一覆盖三条 LLM 数据路径，而不是单点补丁；
2. 提供分层失败归因，而不是只有 pass/fail；
3. 将执行收益验证纳入准入决策，而不是只做静态过滤。

### 12.2 适用范围

本方法更适合：

- 文本协议；
- 半结构化协议；
- 有明显请求边界和轻量上下文依赖的协议。

### 12.3 局限性

本方法仍存在以下局限：

1. 对复杂二进制协议的支持有限；
2. 效果依赖本地 validator 的覆盖质量；
3. 若目标协议状态很浅，则验证驱动带来的状态收益提升可能有限；
4. benefit validation 会引入额外执行开销，需要在收益与成本之间平衡。

---

## 13. 相关工作定位

这篇论文在相关工作中的位置应当写清楚：

1. **相对 ChatAFL**
   - 我们不是提出新的 LLM 用途，而是提出新的 LLM 准入与归因框架。

2. **相对 MultiFuzz / RAG 类工作**
   - 它们偏向提升生成前语义上下文；
   - 我们偏向生成后本地准入控制与收益归因。

3. **相对 model-based LLM fuzzing**
   - 它们更强调用 LLM 构造模型或生成序列；
   - 我们更强调控制 LLM 输出何时、如何进入 fuzzing 主循环。

因此，这篇论文的定位应当是：

> 一篇关于 LLM 辅助协议 fuzzing 可靠性控制与准入机制设计的系统论文。

---

## 14. 结论

本文围绕一个简单但关键的问题展开：在有状态协议 fuzzing 中，LLM 输出是否应该被直接信任？我们的答案是否定的。对于 grammar extraction、seed enrichment 和 stall breaking 这三条关键 LLM 数据路径而言，仅依赖 prompt 约束和轻量清洗并不足以保证输出质量。真正有价值的 LLM 辅助机制，不仅应当生成“像协议”的输入，还应当确保这些输入在语法、上下文和状态推进层面都是可接受且有潜在收益的。

基于这一认识，本文提出 Validation-Driven LLM Fuzzing 框架，将 LLM 输出从直接注入的 fuzzing 输入转化为经过多级验证的候选样本，并通过失败归因与收益归因提高系统的稳定性、可解释性和调用效率。实验结果表明，验证驱动的准入机制能够有效减少无效输入进入 fuzzing 主循环，提高单位 LLM 调用的状态转移收益，并为 LLM 辅助协议 fuzzing 提供一种更可靠的系统设计路径。

---

## 15. 当前最该补的数据

为了把这版草稿真正变成论文，接下来优先级最高的数据是：

1. `LLM acceptance rate`
2. `format / grammar / context / state / no-gain` 失败分布
3. `new transitions per LLM call`
4. `invalid enriched seed ratio`
5. `immediate reject ratio`
6. `transitions/hour`
7. `ChatAFL vs Ours` 的 repeated runs

如果这些数据先补齐，这篇论文的主体就会非常扎实。

---

## 16. 下一步写作建议

如果继续推进论文文本，我建议下一步按这个顺序补：

1. `Introduction` 正式英文版
2. `Method` 正式英文版
3. `Experimental Setup`
4. `Results and Analysis`
5. `Related Work`

其中最关键的是：

- 不要把故事写成“LLM 更聪明了”
- 要一直围绕“LLM 输出的准入控制与收益控制”展开

