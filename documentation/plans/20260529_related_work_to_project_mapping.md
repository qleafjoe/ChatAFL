# Related Work to Project Mapping

本文档将近年 LLM + 网络协议模糊测试相关工作映射到当前 ChatAFL 项目的可落地改造点。目的不是堆相关工作，而是说明为什么本项目可以从“验证层 + 反馈重试”升级为“概率自适应验证 + 运行时反馈检索 + 状态目标生成”。

## 1. ChatAFL: 三条 LLM 路径是本项目的基础

ChatAFL 的核心是：

1. grammar extraction
2. seed enrichment
3. stall-breaking message generation

当前仓库对应位置：

- `ChatAFL/afl-fuzz.c:768` `setup_llm_grammars()`
- `ChatAFL/afl-fuzz.c:3271` `enrich_testcases()`
- `ChatAFL/afl-fuzz.c:7365` stall-breaking block

本项目不应再只说“验证 LLM 输出”，而应围绕这三条路径分别设计：

- grammar 路径：严格准入，防止污染结构感知变异。
- enrichment 路径：中等准入，保留扩展状态空间能力。
- stall 路径：探索优先，用 trial 和状态目标突破停滞。

## 2. MSFuzz: 从“协议通用知识”转向“目标实现相关语法”

MSFuzz 的重点是 message syntax comprehension。它不只让 LLM 根据 RFC 生成消息，而是构造目标协议实现相关的 message syntax tree，再指导 seed expansion 和 syntax-aware mutation。

对本项目启发：

当前 `setup_llm_grammars()` 只做 LLM 模板抽取和 method/field 检查。可增加：

```text
grammar_seed_match_rate:
  新 grammar 对初始 seed、高收益 seed 的命中率

grammar_gain_score:
  使用该 grammar 后带来的 new path/state/transition 数

implementation_hint:
  从 rtsp.dict、in-rtsp、responses-ipsm、高收益 queue seed 中抽取字段模式
```

这比单纯 `validate_grammar_pattern()` 更贴近 MSFuzz 的思想，但不需要完整源码语法树。

## 3. ChatHTTPFuzz: 字段标注、代码逻辑和模板调度

ChatHTTPFuzz 针对 IoT HTTP fuzzing，使用 LLM 标注 HTTP 字段，分析服务代码逻辑，生成符合目标服务逻辑的测试包，并用改进 Thompson sampling 调度 seed templates。

对本项目启发：

1. 当前 enrichment 只补缺失 message type，缺少“模板调度”。
2. 可以给 LLM seed 维护收益分数：

```text
seed_score =
  3 * new_transition
  + 2 * new_state
  + 1 * new_cov
  - 1 * no_gain
  - 2 * repeated_response_seq
```

3. 对不同 seed template 做轻量 bandit 调度：

```text
high-gain template -> more mutation energy
repeated no-gain template -> decay priority
trial success template -> promote to corpus
```

这能让方法不只是“反馈给 LLM”，而是影响 AFLNet/ChatAFL 的后续 fuzzing 调度。

## 4. MultiFuzz: Dense Retrieval / RAG / Multi-Agent 的轻量化落地

MultiFuzz 指出 ChatAFL 存在 LLM hallucination、输出不可靠、默认模型知道协议规范等问题，因此引入 dense retrieval 和 multi-agent。

本项目不建议直接做大 multi-agent。更可行的是 runtime feedback retrieval：

检索源：

- `rtsp.dict`
- `in-rtsp`
- `responses-ipsm`
- `ipsm.dot`
- `llm-validation/*.csv`
- 高收益 queue seed
- `protocol-grammars`

检索目标：

```text
给定当前 stage / 当前 response sequence / 目标 transition:
  找相似高收益 seed
  找触发 rare response code 的历史 trace
  找最近重复 no-gain pattern
  找当前 method 的合法 header 模板
```

最终插入 prompt 的不是整段文档，而是压缩摘要：

```text
Useful pattern:
  SETUP with alternative track IDs recently produced 201->202.

Avoid:
  DESCRIBE on same media URI repeatedly returns 404 and adds no coverage.

Target:
  Prefer candidates likely to leave 400/404 loop or reach 203/204/205.
```

这可以在 C 代码中以日志统计和字符串摘要完成，工程量远小于完整 RAG。

## 5. LLM-Boofuzz: 从真实流量抽取协议信息

LLM-Boofuzz 使用真实流量作为输入，提取协议信息，生成 boofuzz 脚本并迭代修复。

对本项目启发：

当前 ChatAFL 已经保存了请求/响应历史：

- `responses-ipsm`
- `stall-interactions/prompt-*`
- `stall-interactions/response-*`
- `.cur_input`

可以将这些视作“运行时流量”，用于构造 feedback summary：

```text
recent_response_code_distribution
rare_response_codes
current_session_value
last_successful_setup_transport
high_gain_request_headers
```

这比只给 LLM validation error 更接近真实网络行为反馈。

## 6. MALF: Multi-Agent / RAG / Feedback Refinement 的低成本版本

MALF 面向工业控制协议，组合 RAG、多 agent、seed generation、mutation strategy 和 feedback refinement。

对本项目启发：

不建议一开始做多个真实 agent，但可以把单个 LLM prompt 拆成角色化 sections：

```text
Protocol Memory:
  当前协议字段约束和历史成功模板

Runtime Observer:
  最近 response sequence、rare states、no-gain patterns

Mutation Strategist:
  当前应该探索 valid / near-valid / state-violating 哪一类候选

Output Contract:
  只输出 raw protocol message
```

这能借鉴 multi-agent 的分工思想，但仍保持当前 `chat_with_llm()` 单接口。

## 7. 本项目最终差异点

已有工作主要集中在：

| 方向 | 代表工作 | 重点 |
|------|----------|------|
| LLM 生成协议消息 | ChatAFL | grammar / seed / stall |
| 目标实现相关语法 | MSFuzz | message syntax tree |
| 字段标注和模板调度 | ChatHTTPFuzz | field labeling / template scheduling |
| RAG 和多 agent | MultiFuzz / MALF | retrieval / coordination |
| 黑盒脚本生成 | LLM-Boofuzz | traffic to boofuzz script |

本项目应强调：

```text
不是让 LLM 更懂协议本身，
而是让 fuzzing 运行时反馈动态控制 LLM 候选的验证强度、执行路径和保留概率。
```

具体贡献：

1. 概率自适应验证：`p(strict), p(format), p(trial), p(repair)` 随 invalid/no-gain/gain 动态变化。
2. Trial queue：保留半合法输入的探索价值，避免污染主 corpus。
3. Runtime feedback retrieval：从 validation log、network responses、IPSM、high-gain seeds 中生成动态摘要。
4. State-targeted stall-breaking：LLM 生成目标从“下一条合法消息”改为“突破低频/未探索状态转移”。
5. Dynamic temperature：根据错误率、no-gain 率、状态停滞动态调节模型随机性。

这条路线能解释当前实验现象：

- v0 覆盖高：说明不严格过滤有探索价值。
- full validation 效果一般：说明静态严格验证会牺牲探索。
- adaptive + trial 的目标：保留 v0 的探索性，同时用收益准入控制污染。

因此，从理论逻辑上，它比原始 ChatAFL 更有机会提升代码覆盖率和状态转移覆盖率。

## 8. 可引用来源

- ChatAFL, NDSS 2024: https://dev.ndss-symposium.org/ndss-paper/large-language-model-guided-protocol-fuzzing/
- MSFuzz, Electronics 2024: https://www.mdpi.com/2079-9292/13/13/2632
- ChatHTTPFuzz, arXiv 2024: https://arxiv.org/abs/2411.11929
- MultiFuzz, arXiv 2025: https://huggingface.co/papers/2508.14300
- LLM-Boofuzz, Electronics 2025: https://www.mdpi.com/2079-9292/14/23/4550
- MALF, arXiv 2025: https://arxiv.org/abs/2510.02694
