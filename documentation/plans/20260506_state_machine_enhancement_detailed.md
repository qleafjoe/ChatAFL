# 基于协议状态机增强的 ChatAFL 改进：细化设计与论文论证

**目标**：将 `plan/20260429_ChatAFL_Improvement_Directions_Analysis.md` 中“State Machine Enhancement”从概念级方向，细化为可实现、可实验、可投稿的方法设计。

---

## 1. 先给结论

当前 ChatAFL 已经具备“状态感知 fuzzing”的外壳，但它的**状态表示、状态选择、状态驱动生成**仍然比较粗糙，因此会出现一个典型现象：

1. `states / transitions` 数字能涨；
2. 但很多“状态”本质只是**响应码差异**，不是真正的**协议会话语义状态**；
3. LLM 在 stall 时虽然能生成“像请求的东西”，但没有显式利用“当前状态 -> 目标状态 -> 合法下一跳”的状态机信息；
4. 最终表现为浅层转移较多，深层状态推进效率不足。

因此，这个方向真正应该做的，不是再加一个模糊的“状态机模块”，而是把现有状态反馈链路升级成下面这条闭环：

`粗粒度响应状态` -> `语义增强状态表示` -> `转移收益建模` -> `目标状态调度` -> `状态条件化消息生成` -> `执行后反哺状态图`

这条路线既贴合当前代码，也比单纯“优化 prompt”更有论文价值。

---

## 2. 当前代码里的状态机机制到底是什么

### 2.1 现有状态从哪里来

当前 ChatAFL 的状态信息主要来自 `extract_response_codes_*()`：

1. `ChatAFL/aflnet.c`
2. 例如：
   - `extract_response_codes_rtsp()` 直接抽取 RTSP 响应码，如 `200/401/454`。
   - `extract_response_codes_ftp()` 直接抽取 FTP 三位响应码。
   - `extract_response_codes_http()` 直接抽取 HTTP 状态码。

这意味着现有“状态”的核心定义，本质是：

**一个响应消息的数值状态码，或由状态码串形成的状态序列。**

这在工程上足够简单，但也带来明显限制：

1. 同为 `200` 的两个响应，在协议语义上可能完全不同；
2. `200` after `SETUP` 和 `200` after `PLAY` 被当成同一类状态；
3. 某些真正重要的会话上下文，例如 `Session` 已建立、是否已认证、是否进入传输阶段，并没有被编码进状态。

### 2.2 现有状态机如何维护

`ChatAFL/afl-fuzz.c` 里已经有一套 AFLNet 风格的状态机维护逻辑：

1. `setup_ipsm()` 初始化 `ipsm`、`khms_states`、`khs_ipsm_paths`。
2. `update_state_aware_variables()` 根据执行得到的 `state_sequence`：
   - 更新 `ipsm.dot`
   - 维护 `state_ids`
   - 维护 `khms_states`
   - 将 seed 关联到可达状态
3. `choose_target_state()` 根据 `RANDOM / ROUND_ROBIN / FAVOR` 选择目标状态。
4. `choose_seed()` 从目标状态可达 seed 集合中选一个 seed 做后续 fuzzing。

也就是说，系统已经有：

1. 状态节点；
2. 状态边；
3. 状态到 seed 的映射；
4. 基于状态的 seed 调度。

所以“状态机增强”不是重起炉灶，而是**增强状态定义和状态利用方式**。

### 2.3 LLM 与状态机现在是怎样耦合的

当前 LLM 与状态机的耦合非常弱：

1. `stall` 路径会构造历史 prompt，请 LLM 给出“下一条请求”；
2. 但 prompt 里没有明确的“当前抽象状态”“候选目标状态”“最有价值的未探索转移”；
3. `validate_protocol_request_message()` 目前只有 RTSP 有基础校验；
4. seed enrichment 也没有真正基于状态可达性做序列级约束。

所以现在更像：

**LLM 在“看历史猜下一条”**，而不是  
**LLM 在“面向状态图规划下一跳”**。

---

## 3. 当前方案的核心缺陷

### 3.1 状态定义过粗

当前很多协议的状态都是纯响应码。

这会导致：

1. 状态别名问题：不同会话阶段共享同一响应码；
2. 调度误导：系统以为自己在探索“新状态”，其实只是命中了不同上下文下的同一个返回码；
3. 深状态不可区分：例如 RTSP 的 `SETUP-200` 和 `PLAY-200` 没被区分。

### 3.2 “interesting transition” 的定义太弱

当前 `is_state_sequence_interesting()` 主要是对**状态序列哈希去重**。

问题是：

1. 它关心“序列新不新”；
2. 但不关心“这个新序列是否代表了真正新的会话语义推进”；
3. 也不关心“是因为新消息类型、合法依赖满足，还是只是随机抖动响应码”。

### 3.3 目标状态选择没有显式考虑“深度价值”

`choose_target_state()` 和 `update_scores_and_select_next_state()` 已经有基于 `fuzzs / selected_times / paths_discovered` 的评分。

但它没有显式建模：

1. 状态深度；
2. 状态稀有度；
3. 状态边界性；
4. 某状态是否卡在“高拒绝、低推进”的瓶颈处；
5. 某状态后面是否还有大量未探索 outgoing transitions。

因此它更像“基于历史收益的经验调度”，还不是“面向深层状态突破的主动调度”。

### 3.4 LLM 生成不以状态转移为直接目标

当前 stall prompt 只要求“给出下一条合适请求”，没有明确告诉模型：

1. 当前处在哪个状态；
2. 当前状态缺什么条件才能去下一个状态；
3. 候选目标状态是什么；
4. 如果想触发 `S -> T`，优先应该用什么消息类型。

这使得 LLM 输出缺少明确的状态转移目标。

---

## 4. 建议的改进总方案

建议把“State Machine Enhancement”定义为：

**State-Semantics-Augmented ChatAFL**

核心思想：

1. 保留 AFLNet 当前 IPSM 主框架；
2. 将“纯响应码状态”升级为“响应码 + 请求类型 + 关键上下文摘要”的增强状态；
3. 用增强状态图而不是原始响应码图做调度和 LLM 引导；
4. 用状态收益反馈判断 LLM 调用是否真的值得。

下面给出 4 个子模块。

---

## 5. 子模块 A：增强状态表示

### 5.1 目标

把当前：

`state = response_code`

升级为：

`enhanced_state = hash(response_code, triggering_request_type, session_phase, key_context_flags)`

### 5.2 为什么这样做

因为协议状态不是单看服务端响应码决定的，而是取决于：

1. 当前客户端发了什么请求；
2. 之前完成了哪些依赖动作；
3. 是否已经建立 session / auth / transport；
4. 当前响应是发生在什么上下文里。

### 5.3 如何在代码里做

建议新增：

1. `ChatAFL/state-model.h`
2. `ChatAFL/state-model.c`

新增数据结构示例：

```c
typedef struct {
  u32 raw_response_code;
  u16 request_type_id;
  u16 phase_flags;
  u32 context_hash;
} enhanced_state_key_t;

typedef struct {
  u32 state_id;
  enhanced_state_key_t key;
  u32 depth;
  u32 indegree;
  u32 outdegree;
  u32 successful_transitions;
  u32 failed_transitions;
} enhanced_state_info_t;
```

### 5.4 phase_flags 建议

以 RTSP 为例，可以先做轻量版本：

1. `PHASE_INIT`
2. `PHASE_DESCRIBED`
3. `PHASE_SETUP_DONE`
4. `PHASE_SESSION_ESTABLISHED`
5. `PHASE_PLAYING`
6. `PHASE_PAUSED`

以 FTP 为例：

1. `PHASE_CONNECTED`
2. `PHASE_USER_SENT`
3. `PHASE_AUTHED`
4. `PHASE_PASV`
5. `PHASE_TRANSFER`

这些 phase 不要求完整 RFC 语义，只需要能区分最关键的会话推进阶段。

### 5.5 上下文摘要来源

可以直接复用或扩展已有请求解析能力：

1. `extract_requests_rtsp()` / `extract_requests_ftp()` 已经能切消息；
2. 在每个 region 上再做轻量 header 提取：
   - RTSP: method, CSeq, Session, Transport
   - FTP: command, USER/PASS/PASV/RETR/STOR
   - HTTP: method, path, Content-Length

然后维护一个 per-seed / per-execution 的上下文摘要：

```c
typedef struct {
  u32 last_request_type;
  u8 has_session;
  u8 has_auth;
  u8 has_transport;
  u8 has_data_channel;
  u32 session_token_hash;
} protocol_context_t;
```

---

## 6. 子模块 B：状态转移质量建模

### 6.1 目标

不再只记录“有没有新边”，而是记录这条边的质量。

建议把每条转移都维护以下统计：

1. 触发次数
2. 成功推进次数
3. 被立即拒绝次数
4. 平均执行耗时
5. 是否由 LLM 生成触发
6. 是否带来新 coverage
7. 是否带来新 enhanced state

### 6.2 新增结构

```c
typedef struct {
  u32 from_state;
  u32 to_state;
  u32 request_type_id;
  u32 hits;
  u32 llm_hits;
  u32 rejects;
  u32 new_cov_hits;
  u32 new_state_hits;
  u64 total_exec_us;
} transition_info_t;
```

### 6.3 作用

有了这层数据后，就能区分三类状态边：

1. **高收益边**：常带来新 coverage / 新状态；
2. **低收益边**：反复执行但没有推进；
3. **伪状态边**：看似不同，实际总是立即 reject 或循环。

这会让后续状态调度更有依据。

---

## 7. 子模块 C：状态驱动的目标状态调度

### 7.1 当前问题

当前 `FAVOR` 分数更偏向历史路径收益，没有显式鼓励深层状态探索。

### 7.2 新评分函数建议

建议为增强状态引入：

```text
score(s) =
  w1 * rarity(s)
  + w2 * frontier(s)
  + w3 * depth(s)
  + w4 * llm_potential(s)
  - w5 * fuzz_cost(s)
  - w6 * reject_ratio(s)
```

其中：

1. `rarity(s)`：到达该状态的 seed 少，说明稀有；
2. `frontier(s)`：该状态已知 outgoing edges 少，但历史上有新边潜力；
3. `depth(s)`：从初始状态到该状态的最短路径更深；
4. `llm_potential(s)`：该状态处于 plateau，但历史上 LLM 在该类状态曾有效；
5. `fuzz_cost(s)`：该状态平均执行代价高；
6. `reject_ratio(s)`：从该状态出发大多立即拒绝。

### 7.3 代码修改点

直接改：

1. `ChatAFL/aflnet.h`
   - 扩展 `state_info_t`
2. `ChatAFL/afl-fuzz.c`
   - `update_scores_and_select_next_state()`
   - `choose_target_state()`

建议在 `state_info_t` 增加：

```c
u32 depth;
u32 frontier_degree;
u32 rejects;
u32 llm_attempts;
u32 llm_successes;
u64 avg_exec_us;
```

这样可以在不大改主流程的前提下，把状态调度改为“深度优先 + 收益约束”。

---

## 8. 子模块 D：状态条件化的 LLM 生成

### 8.1 核心思想

让 LLM 不再只看“原始 history”，而是看：

1. 当前增强状态摘要；
2. 目标状态摘要；
3. 最近失败的消息类型；
4. 当前缺失的依赖条件；
5. 几条历史上成功触发相似转移的示例。

### 8.2 新 prompt 结构建议

当前 stall prompt 更像自由生成。建议改为如下结构：

```text
Protocol: RTSP
Current abstract state:
- last request type: SETUP
- last response code: 200
- session established: yes
- transport established: yes
- phase: SETUP_DONE

Target exploration goal:
- try to reach a new state beyond SETUP_DONE
- prioritize transitions not yet observed from this state

Known successful examples from similar states:
...

Generate exactly one next client request that is valid in the current context.
```

### 8.3 代码修改点

建议新增：

1. `construct_prompt_stall_stateful()`
2. `construct_transition_goal_summary()`
3. `serialize_protocol_context()`

修改文件：

1. `ChatAFL/chat-llm.c`
2. `ChatAFL/chat-llm.h`
3. `ChatAFL/afl-fuzz.c` 的 stall 分支

### 8.4 不要做什么

现阶段不要一上来做复杂多步规划，例如：

1. 一次生成多条请求；
2. 让 LLM 直接输出整个状态机；
3. 多轮自反思 agent 式交互。

论文第一版更稳的做法是：

**单步状态条件化生成 + 本地验证 + 收益回写**

---

## 9. 结合当前代码，具体应该怎么改

下面给出按文件拆分的实施方案。

### 9.1 `ChatAFL/aflnet.c`

当前职责：

1. 切分请求；
2. 提取响应状态码。

建议新增职责：

1. 提取请求类型；
2. 提取最小上下文字段；
3. 将“响应码状态”升级为“增强状态键”的组成部分。

建议新增接口：

```c
u16 extract_request_type_rtsp(unsigned char *buf, unsigned int size);
u16 extract_request_type_ftp(unsigned char *buf, unsigned int size);
u16 extract_request_type_http(unsigned char *buf, unsigned int size);

int update_protocol_context_rtsp(protocol_context_t *ctx,
                                 unsigned char *req_buf,
                                 unsigned int req_size,
                                 unsigned int response_code);
```

### 9.2 `ChatAFL/aflnet.h`

建议新增：

1. `protocol_context_t`
2. `enhanced_state_key_t`
3. `transition_info_t`
4. 新的函数声明

### 9.3 `ChatAFL/afl-fuzz.c`

这是主要改造点。

需要修改的逻辑：

1. `update_region_annotations()`
   - 目前只给 region 标注原始 `state_sequence`
   - 需要额外标注 `enhanced_state_sequence`

2. `is_state_sequence_interesting()`
   - 由“响应码序列去重”改为“增强状态序列去重”
   - 或并行保留两套指标：
     - raw transition
     - semantic transition

3. `update_state_aware_variables()`
   - 在更新 IPSM 时写入增强状态图；
   - 更新状态深度、转移收益、拒绝率、LLM 收益统计。

4. `choose_target_state()`
   - 改成深度/边界/收益混合评分。

5. `choose_seed()`
   - 不只看“能到达目标状态”，还看该 seed 在该状态下是否历史上更容易产生有效下一跳。

6. stall 分支
   - 现在只做 `history -> prompt -> one message`
   - 改为 `current enhanced state + target goal + examples -> one message`

### 9.4 `ChatAFL/chat-llm.c`

建议新增 3 类函数：

1. **状态摘要**
   - 把当前上下文压缩成 prompt 可读文本
2. **状态条件化 prompt**
   - 让 LLM 明确知道“当前状态”和“目标”
3. **状态收益归因**
   - 记录这次调用是否真的带来新语义状态/新语义转移

### 9.5 `ChatAFL/chat-llm.c` 的 validator 要同步增强

因为你一旦引入状态条件化生成，就必须同步检查：

1. 生成消息在当前 phase 下是否合法；
2. 是否缺少依赖字段；
3. 是否引用了尚未建立的 Session；
4. 是否违反 USER/PASS/PLAY/SETUP 这类顺序约束。

也就是说，状态机增强和 validator 实际上是联动的。

---

## 10. 实施顺序建议

### Phase 1：语义增强状态，但不改 LLM

先做：

1. 增强状态键；
2. 增强状态图；
3. 新指标记录。

先不碰 prompt。

目标是验证一个事实：

**当前 ChatAFL 的很多“状态”其实是粗粒度别名。**

这一步本身就能形成论文里的动机图。

### Phase 2：状态调度增强

做：

1. 深度感知状态评分；
2. frontier 感知状态选择；
3. 低收益状态降权。

目标是证明：

**不改 LLM，只改状态调度，也能提高 deep-state exploration。**

### Phase 3：状态条件化 stall generation

做：

1. 新 stall prompt；
2. 结合当前状态和目标状态生成下一跳；
3. 统计每次 LLM 调用的 semantic transition 收益。

目标是证明：

**LLM 只有在显式状态条件下，才真正能帮助推进深层状态。**

### Phase 4：状态条件化 seed enrichment

做：

1. enrich 不再只补消息类型；
2. 而是补“能让序列跨过某个状态边界”的消息。

这一步可作为增强实验，不必第一版就全做完。

---

## 11. 预期效果怎么写才合理

### 11.1 最可能首先提升的指标

我认为这个方向最先提升的不会是 branch coverage，而是：

1. `semantic states`
2. `semantic transitions`
3. `time-to-first-deep-state`
4. `new transitions per LLM call`
5. `immediate reject ratio`

### 11.2 预期提升区间

如果实现做到位，比较合理的论文预期不是“翻倍”，而是：

1. 语义状态数更稳定，重复 run 方差下降；
2. 语义转移数相对原版提高约 `10% - 30%`；
3. 到达深层状态时间缩短约 `15% - 40%`；
4. 每次 LLM 调用带来的新转移收益提高约 `20% - 50%`；
5. 立即拒绝/无效交互比例下降约 `20% - 40%`。

这里要注意：

1. 这些更适合作为**实验假设**；
2. 不应在文档中写成已经确定能达到的结果；
3. 最终发表要以 repeated runs 的均值和方差为准。

### 11.3 为什么 branch coverage 不一定暴涨

因为这个方向主要解决的是：

1. 状态推进效率；
2. 深层交互合法性；
3. LLM 状态引导质量。

它的主收益是**把 fuzzing 更快送入深状态**，而不是让所有目标的代码覆盖都大幅上升。

所以论文里应强调：

**这是面向状态型协议的深交互增强，不是通用分支覆盖优化器。**

---

## 12. 创新性分析：到底新在哪里

### 12.1 不是简单“把状态机接上 LLM”

如果论文只说：

1. AFLNet 有状态图；
2. 我们把状态图给 LLM 看；

这不够新。

真正的创新点应该写成：

### 创新点 1：语义增强状态表示

不是把响应码直接当状态，而是把：

1. 响应码；
2. 触发请求类型；
3. 会话 phase；
4. 关键上下文摘要

联合编码成增强状态。

这是从**syntactic state** 到 **semantic state abstraction** 的升级。

### 创新点 2：状态收益驱动调度

当前大多数状态调度只看：

1. 是否覆盖过；
2. 被选过几次；
3. 有多少 seed。

而这里显式引入：

1. 状态深度；
2. frontier；
3. 拒绝率；
4. LLM 历史收益；

让状态选择真正服务于 deep-state exploration。

### 创新点 3：状态条件化单步生成

LLM 不是盲目生成下一条消息，而是：

1. 在当前增强状态下生成；
2. 朝着未探索目标状态生成；
3. 生成后用本地 validator 和状态反馈验证。

这比“自由生成下一条命令”更系统。

### 创新点 4：语义转移级收益评估

不仅看 coverage，还看：

1. 这次调用是否到达新语义状态；
2. 是否跨过新语义边；
3. 是否降低了无效拒绝。

这会让论文比传统“最终覆盖率对比”更有解释力。

---

## 13. 为什么这条路线适合发表论文

### 13.1 问题真实

当前 ChatAFL 的主要问题之一确实是：

1. 状态表示粗；
2. LLM 不懂当前状态边界；
3. 深状态推进效率有限。

这是协议 fuzzing 的真实难点，不是人为制造的问题。

### 13.2 方法有系统性

这不是一个零碎 patch，而是完整链路：

1. 状态表示增强；
2. 状态图更新；
3. 状态调度增强；
4. 状态条件化生成；
5. 语义转移收益评估。

这足以构成系统论文的主体。

### 13.3 实验可以做出层次

可以做下面的消融：

1. ChatAFL baseline
2. + semantic state abstraction
3. + state-aware scheduling
4. + state-conditioned LLM stall breaking
5. + full system

这会让论文非常清楚地回答：

1. 哪一层最有效；
2. 哪一层最值得花成本；
3. LLM 的收益究竟来自哪里。

### 13.4 指标天然适合讲故事

除了常规的：

1. branches
2. states
3. transitions

你还能报告：

1. semantic states
2. semantic transitions
3. time-to-first-deep-state
4. new semantic transitions per LLM call
5. immediate reject ratio
6. transition efficiency under equal LLM budget

这些指标很适合 protocol fuzzing 论文。

### 13.5 与现有代码基础高度兼容

这一点很关键。

因为它不要求：

1. 重新训练模型；
2. 重写 AFL 主循环；
3. 引入复杂在线规划器。

它是在现有 ChatAFL/AFLNet 之上做增强，落地风险低，论文风险也更可控。

---

## 14. 建议的论文标题表达

可选表达：

1. **State-Semantics-Augmented LLM-Guided Protocol Fuzzing**
2. **Enhancing ChatAFL with Semantic State Abstraction and State-Conditioned Message Generation**
3. **Improving Stateful Protocol Fuzzing via Semantic State Modeling for LLM-Assisted Input Generation**

如果偏中文项目总结，也可以表述为：

**基于语义状态抽象与状态条件化生成的 ChatAFL 增强方法**

---

## 15. 最终建议

如果只选一个最值得做、最有论文味、又不至于过度冒险的方向，我建议把本方向收敛成下面一句：

**不要泛泛做“状态机增强”，而要做“语义增强状态建模 + 状态收益调度 + 状态条件化 LLM 生成”。**

这是因为：

1. 它准确打中当前代码的短板；
2. 它能自然结合 AFLNet 现有状态机制；
3. 它比单纯 validator 更偏算法贡献；
4. 它比多步 agent 规划更容易做稳；
5. 它更适合写成一篇结构完整、指标清晰的论文。

