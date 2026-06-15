# 类似 ChatAFL 方法的核心问题与行业解决思路梳理

**整理时间**: 2026年04月23日  
**目的**: 总结 2024–2025 年间与 ChatAFL 相近的 LLM-assisted fuzzing / protocol fuzzing 工作中，常见的核心问题是什么，行业通常如何处理，以及这些经验对当前 ChatAFL 增强方向有什么直接启发。  
**适用范围**: 用于论文选题论证、系统设计依据和方法设计参考，不直接替代具体实现文档。

---

## 1. 先说结论

过去两年的代表性工作给出的共识非常明确：

**LLM 最适合做“高价值、低频、可验证”的控制面组件，而不是“高频、无约束、直接入队”的热路径变异器。**

换句话说，类似 ChatAFL 的方法真正难的不是“把模型接进 fuzzing”，而是：

1. 什么时候调用 LLM
2. 调用时给什么上下文
3. 让模型输出什么格式
4. 怎么验证输出是否能用
5. 怎么证明这些调用真的值

---

## 2. 类似 ChatAFL 的核心问题

## 2.1 语法正确不等于状态正确

这是最典型的问题。  
很多 LLM 生成的请求看起来像协议报文，但实际上：

1. 缺关键字段
2. 字段值与上下文不一致
3. 方法顺序不对
4. Session / token / sequence 等跨消息依赖断裂

在 ChatAFL 这类状态型协议 fuzzing 场景里，这会直接导致：

1. 报文被早期拒绝
2. 只能命中浅层路径
3. branch 看着不差，但 transitions 不上去

**行业主流解法**:

1. 增加 validator
2. 增加依赖约束
3. 让输出经过 repair/self-check
4. 从“消息格式正确”转向“状态可推进”

**代表性启发**:

1. KernelGPT：先生成 spec，再验证，再修复
2. ProphetFuzz：先提约束，再做 self-check
3. HLPFuzz：约束求解不是一次性完成，而是多轮反馈修正

---

## 2.2 什么时候调用 LLM 是一等问题

类似 ChatAFL 的系统如果把 LLM 放进热路径，会遇到三个后果：

1. 吞吐明显下降
2. 调用成本上升
3. 结果波动加剧

因此，近两年的主流趋势不是“每轮都让模型参与”，而是**稀疏调用**。

**行业主流触发时机**:

1. 初始化时
   - 用 LLM 提取 grammar / spec / seeds / constraints

2. stall / plateau 时
   - 当 fuzzing 长时间没有新路径、新状态或新转移时，再让 LLM 帮忙跳出局部最优

3. post-fuzzing 时
   - 用 LLM 做日志分析、崩溃解释、约束修复、驱动修复

**代表性工作**:

1. ChatAFL：初始化 + plateau 时生成消息
2. G2Fuzz：初始化时做 generator synthesis，stall 时做 generator mutation
3. HLPFuzz：在复杂约束突破时调用 LLM，而不是每轮都调用

**对当前项目的启发**:

1. 不应把 LLM 设计成每轮 mutation 参与者
2. 应把它设计成：
   - grammar / dependency 提取器
   - plateau 恢复器
   - validator repair 辅助器

---

## 2.3 prompt 上下文构造很难，脏上下文会直接拖垮效果

类似 ChatAFL 的方法都依赖上下文构造。  
但这里最常见的问题是：

1. 历史太长，噪声太多
2. 历史太短，状态信息不够
3. 直接塞原始日志，模型抓不到关键信号
4. 小模型容易忽略前一轮反馈

**行业主流解法**:

1. 上下文摘要化
2. 只保留高价值历史
3. 反馈驱动的迭代构造，而不是一次塞满
4. 按优先级选择最值得求解的约束

**代表性工作**:

1. HLPFuzz：提出 iterative context construction 和 constraint prioritization
2. G2Fuzz：把历史 feature feedback 融入 generator mutation prompt
3. Fuzz4All：通过 autoprompting 先蒸馏 prompt，再进入 fuzz loop

**对当前项目的启发**:

1. stall prompt 不能直接拼接脏 history
2. 需要引入“状态摘要”“最近失败点”“当前缺失依赖”这类结构化上下文
3. 多步 stall-breaking 的 prompt 应该比单步更强调当前状态与目标状态之间的缺口

---

## 2.4 输出格式不稳定，是最普遍的工程痛点之一

这类问题几乎所有 LLM-assisted fuzzing 项目都会遇到：

1. Markdown 包裹
2. 解释性文字
3. JSON 不闭合
4. 漏字段
5. 多输出无关内容
6. 小模型不听格式指令

HLPFuzz 甚至明确观察到，小模型更容易：

1. 返回错误格式
2. 忽略前序反馈
3. 产生幻觉

**行业主流解法**:

1. constrained decoding
   - JSON schema
   - regex
   - grammar

2. structured outputs
   - 让模型直接输出受约束结构，而不是靠 prompt 口头约束

3. parse + sanitize + repair
   - 先解析
   - 再清洗
   - 再修复

4. 输出中间表示
   - 如果直接吐原始输入不稳定，就让模型输出：
     - spec
     - generator
     - driver
     - constraint set

**代表性工作**:

1. KernelGPT：生成 JSON specs，再通过 `syz-check` 验证和修复
2. PromptFuzz：强调 syntax / semantics / behavior / coverage sanitization
3. G2Fuzz：让 LLM 生成 generator，而不是直接吐复杂非文本输入
4. vLLM：已提供 JSON / regex / grammar 结构化输出支持

**对当前项目的启发**:

1. `clean_llm_response()` 可以保留，但不能继续作为主保障手段
2. grammar extraction 应升级为 schema constrained
3. stall-breaking 应尽量使用 request grammar 或受限结构，而不是仅靠 prompt 禁止 markdown

---

## 2.5 幻觉和随机性不会自动消失，必须用验证闭环吃掉

类似 ChatAFL 的工作在以下环节最容易受幻觉影响：

1. grammar extraction
2. constraint extraction
3. state prediction
4. seed generation

单次生成经常不稳定，因此行业里很少再假设：

**“prompt 写好了，模型自然就稳定。”**

**行业主流解法**:

1. self-consistency
   - 多次生成，多数表决

2. validation feedback
   - 用程序工具校验，再把错误喂回去

3. self-check
   - 让模型自己对前一轮结果做一致性检查

4. capability-aware model selection
   - 不把关键约束求解任务交给过弱的小模型

**代表性工作**:

1. ChatAFL：语法抽取阶段就使用 repeated sampling
2. KernelGPT：validate-and-repair
3. ProphetFuzz：constraint extraction + self-check
4. HLPFuzz：多轮迭代求解复杂约束

**对当前项目的启发**:

1. validator 不是“锦上添花”，而是系统必要模块
2. protocol grammar / dependency 信息应允许多次采样再聚合
3. 小模型如果格式遵循能力差，应限制其承担关键输出任务

---

## 2.6 仅报 coverage 已经不够了

类似 ChatAFL 的系统如果只报告 coverage，很容易遇到两个问题：

1. 看起来涨了，但其实大量输入无效
2. 覆盖涨幅不一定能说明 LLM 调用值得

因此近年的趋势是：

1. 报 coverage
2. 也报代价
3. 也报有效率
4. 也报 bug/constraint/repair 层面的收益

**行业主流解法**:

1. coverage + bug finding
2. per-call / per-token / per-time 收益
3. success rate / valid rate / repair rate
4. 多模型对比与泛化分析

**代表性工作**:

1. HLPFuzz：比较不同模型效果，并观察小模型的格式问题
2. ProphetFuzz：直接报高风险组合命中效率与漏洞发现效率
3. PromptFuzz：不仅报覆盖，也报有效 crash 与 bug

**对当前项目的启发**:

除了 `states / transitions / branches`，还应增加：

1. `valid input rate`
2. `semantic consistency rate`
3. `transitions per LLM call`
4. `transitions per 1k tokens`
5. `coverage per hour`

---

## 3. 行业当前比较成熟的解决套路

把近两年的代表性工作综合起来，可以归纳成以下套路。

## 3.1 稀疏调用

**思路**:
只在高价值时刻调用 LLM。

**常见策略**:

1. 初始化时调用
2. plateau 时调用
3. 后处理时调用

**好处**:

1. 降低吞吐损失
2. 降低 token 成本
3. 提高每次调用的边际收益

## 3.2 结构化输出

**思路**:
尽量让模型输出结构化数据，而不是自由文本。

**常见形式**:

1. JSON
2. regex-constrained text
3. grammar-constrained text
4. generator / driver / spec 等中间表示

**好处**:

1. 降低解析失败率
2. 降低 Markdown / 解释性污染
3. 便于程序侧验证

## 3.3 验证与修复闭环

**思路**:
模型输出后不直接信任，而是：

1. 校验
2. 标错
3. 反馈
4. 让模型修

**代表机制**:

1. validate-and-repair
2. self-check
3. self-debug

## 3.4 把 LLM 放在更擅长的任务上

**思路**:
如果模型直接生成原始输入不稳定，就让它做更适合的任务。

**常见替代任务**:

1. 生成 spec
2. 生成 generator
3. 生成 fuzz driver
4. 提取 constraints
5. 做 post-mortem analysis

## 3.5 反馈驱动的 prompt 演化

**思路**:
不是写一个静态 prompt 一直用，而是让 prompt 随 fuzzing 状态演化。

**典型策略**:

1. autoprompting
2. prompt mutation
3. 历史成功样例回灌
4. rare-feature directed prompt 更新

---

## 4. 对当前 ChatAFL 增强方向的直接启发

## 4.1 你的系统应如何设计 LLM 的角色

当前最合理的角色分工是：

1. **冷路径**
   - grammar / dependency 提取
   - state hint / protocol knowledge 抽取

2. **温路径**
   - plateau 时的 multi-step stall-breaking
   - 少量候选序列生成

3. **热路径**
   - 尽量避免
   - 仍由传统 fuzzing engine 承担

## 4.2 你的系统应优先补哪三个短板

1. **validator**
   - 把“格式有效”提升到“状态有效”

2. **dependency-preserving mutation**
   - 把“像报文”提升到“像合法会话中的下一步”

3. **multi-step planning**
   - 把“单条请求补洞”提升到“状态推进序列”

## 4.3 你的系统不应继续依赖的旧思路

1. 不要把 prompt 文本约束当成唯一约束机制
2. 不要只用 branch coverage 评估效果
3. 不要把当前先只跑两协议，误当成正式评测边界
4. 不要把接口迁移和输出清洗本身当作主创新点

---

## 5. 对论文写作最有用的表述方式

论文中可以把行业共识总结成下面这句话：

**现有类似 ChatAFL 的方法已证明 LLM 可以帮助协议 fuzzing，但仍普遍面临调用时机不当、上下文构造困难、输出格式不稳定、协议语义依赖缺失以及收益评估不足等问题。近年的代表性工作普遍采用稀疏调用、结构化输出、验证修复闭环和反馈驱动调度等策略来解决这些问题。**

然后自然引出你的工作：

**本项目的重点不是再次证明 LLM 能参与协议 fuzzing，而是解决 LLM 输出从格式有效走向状态有效的问题。**

---

## 6. 参考资料

以下资料对本文件中的判断最关键：

1. ChatAFL (NDSS 2024)  
   https://www.ndss-symposium.org/ndss-paper/large-language-model-guided-protocol-fuzzing/

2. HLPFuzz (USENIX Security 2025)  
   https://www.usenix.org/conference/usenixsecurity25/presentation/yang-yupeng

3. HLPFuzz 论文 PDF  
   https://www.usenix.org/system/files/usenixsecurity25-yang-yupeng.pdf

4. G2Fuzz (USENIX Security 2025)  
   https://www.usenix.org/conference/usenixsecurity25/presentation/zhang-kunpeng

5. G2Fuzz 论文 PDF  
   https://www.usenix.org/system/files/conference/usenixsecurity25/sec25cycle1-prepub-1291-zhang-kunpeng.pdf

6. KernelGPT 官方仓库  
   https://github.com/ise-uiuc/KernelGPT

7. ProphetFuzz 官方仓库  
   https://github.com/NASP-THU/ProphetFuzz

8. PromptFuzz 官方仓库  
   https://github.com/FuzzAnything/PromptFuzz

9. Fuzz4All 官方仓库  
   https://github.com/fuzz4all/fuzz4all

10. vLLM Structured Outputs 文档  
    https://docs.vllm.ai/features/structured_outputs.html
