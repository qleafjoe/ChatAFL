# ChatAFL 4.22 结果复盘与下一阶段推进计划

**制定时间**: 2026年04月23日  
**适用范围**: 当前仓库中基于 MiniMax/国产 LLM 接口改造后的 ChatAFL 主线版本，以及后续 Live555 / PureFTPD 的迭代优化工作。

---

## 1. 结果与代码目录口径

为方便迭代管理，当前仓库采用以下口径：

1. `Result/` 目录保存**已归档的实验结果**。  
   - `Result/20260422/out-live555-chatafl`
   - `Result/20260422/out-pure-ftpd-chatafl`

2. `benchmark/subjects/<协议>/<目标>/` 目录中的代码视为**实际运行所使用的构建与执行代码**。  
   - `build_targets.sh` 会将 `ChatAFL/`、`ChatAFL-CL1/`、`ChatAFL-CL2/`、`aflnet/` 同步到对应 benchmark 目标目录中，再基于该目录构建镜像。

3. `run.sh` 理论上的原始运行结果会先落在 `benchmark/results-<subject>/`，但本仓库当前可用于分析和复盘的正式结果，以 `Result/` 目录中的归档内容为准。

---

## 2. 当前阶段结论

### 2.1 项目当前在做什么

当前工作的核心不是从零开发 ChatAFL，而是：

1. 将原始 OpenAI 接口迁移为国产 / MiniMax 兼容接口。
2. 修复国产模型在协议生成任务中常见的输出污染问题：
   - 拒答文本
   - Markdown 包裹
   - 啰嗦解释
   - 残缺协议报文
   - 非法或乱码 seed
3. 在 Live555 与 PureFTPD 上回归验证迁移后的 ChatAFL 是否还能接近论文中 ChatAFL 的预期表现。

### 2.2 4.22 结果的总体判断

#### Live555

`Result/20260422/out-live555-chatafl` 表明：

- 拒答词污染已基本清除
- grammar 输出已恢复为纯净 JSON
- stall-breaking 返回的请求格式明显优于 4.21
- 代码覆盖率已接近论文中 ChatAFL 的量级

但仍存在以下问题：

- `stability = 38.55%`
- `execs_per_sec = 0.91`
- `unique_hangs = 33`
- 状态转移覆盖仍低于论文基线

因此，**Live555 当前状态可定义为：已经完成“输入质量修复”，但尚未完成“状态探索能力恢复”。**

#### PureFTPD

`Result/20260422/out-pure-ftpd-chatafl` 表明：

- grammar 污染问题已明显缓解
- enriched seed 已从拒答文本转为基本合法的 FTP 命令序列
- 但主循环没有正常推进，`plot_data` 无有效运行记录

因此，**PureFTPD 当前状态不是“效果不足”，而是“仍未跑通到可比较阶段”。**

---

## 3. 与论文基线的差距

本阶段基线口径统一采用 **论文中 ChatAFL 的预期结果**，而不是本地某次临时运行结果。

### 3.1 Live555：当前结果与论文基线

根据 `Result/20260422/out-live555-chatafl`：

- Branches: **2898**
- States: **13**
- Transitions: **122**

论文中 ChatAFL 对 Live555 的平均结果为：

- Branches: **2928.4**
- States: **14.2**
- Transitions: **160.0**

差距可概括为：

1. **代码覆盖已接近论文值**  
   - branches 达到约 **98.96%**

2. **状态数量接近论文值**  
   - states 达到约 **91.55%**

3. **状态转移覆盖明显不足**  
   - transitions 仅达到约 **76.25%**

结论：  
**Live555 当前最主要的短板不是 branch coverage，而是状态转移覆盖能力。**

### 3.2 PureFTPD：当前结果与论文基线

根据 `Result/20260422/out-pure-ftpd-chatafl`：

- Branches: **194**
- 无可靠 states / transitions 数据

论文中 ChatAFL 对 PureFTPD 的平均结果为：

- Branches: **1134.3**
- States: **27.9**
- Transitions: **281.8**

结论：  
**PureFTPD 当前尚未进入可与论文基线比较的阶段。**

---

## 4. 对“高稳定率”的重新定义

### 4.1 结论

“高稳定率”应被视为**运行健康度指标**，而不是最终的主目标指标。

### 4.2 为什么

1. 论文真正比较的是：
   - states
   - state transitions
   - branches

2. AFL 的 `stability` 指标本质上用于衡量：
   - 同一输入重复执行时路径是否稳定
   - 目标是否存在较强非确定性
   - Fuzzer 是否会因为路径抖动而误判新路径

3. 因此，`stability` 的正确使用方式是：
   - 作为排障与健康诊断指标
   - 作为判断当前运行是否被噪声污染的重要信号
   - 但不应替代论文基线中的核心评估指标

### 4.3 对原计划中目标的修正

原计划中将：

- `stability > 90%`
- `execs_per_sec > 20`

作为 smoke test 目标，这在“判断迁移是否把系统搞坏”这一阶段是合理的。  
但在后续正式推进中，应调整为：

1. **一级目标**: 接近论文中的 states / transitions / branches
2. **二级目标**: 提升 stability、降低 hangs、恢复 execs/sec
3. **三级目标**: 提高 LLM 输出质量与协议合法性

---

## 5. 已完成的工作

截至 2026年04月23日，已确认完成：

- [x] 将主线 ChatAFL 切换为环境变量驱动的国产 / MiniMax 接口
- [x] 在 `chat-llm.c` 中加入统一的 `clean_llm_response()`
- [x] 强化 grammar prompt，抑制 Markdown 与解释性文本
- [x] 强化 stall prompt，要求返回完整请求报文
- [x] 对 enrichment / stall 输出补齐 `\r\n\r\n`
- [x] 将 `CHATTING_THRESHOLD` 提升到 512
- [x] 完成一轮 Live555 回归，验证基础清洗逻辑生效
- [x] 完成一轮 PureFTPD 回归，确认毒种子问题已从“拒答/乱码污染”转为“运行早退”

---

## 6. 当前最关键的技术判断

### 6.1 Live555 的主要瓶颈

Live555 当前不是 grammar 完全失效，也不是 seed 全部不可用。  
主要问题在于：

1. **stall prompt 的输入上下文仍然脏**
   - history/examples 中存在破碎 URI、控制字符、粘连字段
   - 模型虽能生成“像样的协议格式”，但很难稳定命中正确状态推进路径

2. **协议级合法性校验仍不够强**
   - 当前更多是在做格式补全
   - 但尚未充分校验 RTSP 方法、版本、`CSeq`、必需头部、Session 关联性

3. **状态突破策略还不够强**
   - 目前多为单条请求补洞
   - 对深层状态穿透更有帮助的“多步序列”尚未真正落地

### 6.2 PureFTPD 的主要瓶颈

PureFTPD 当前优先级最高的问题是：

**它还没有稳定进入主循环。**

因此 PureFTPD 现在最需要的是运行路径排障，而不是继续调 prompt。

---

## 7. 下一阶段行动计划

### Phase A: 先把 Live555 的“状态探索”做实

- [ ] **A1. 清洗 stall prompt 输入历史**
  - 在组装 prompt 前清洗 `examples` 与 `history`
  - 去除控制字符、破碎 URI、异常粘连内容
  - 限制历史长度，只保留高价值上下文

- [ ] **A2. 将协议合法性校验升级为“协议级校验”**
  - 对 RTSP 响应新增白名单检查：
    - 合法方法名
    - `RTSP/1.0`
    - `CSeq` 存在且为数字
    - `SETUP/PLAY/TEARDOWN` 等请求的关键头是否齐全
  - 校验失败时不入队，直接 retry

- [ ] **A3. 拆分响应清洗逻辑**
  - 将现有 `clean_llm_response()` 拆为：
    - JSON/grammar 清洗
    - 协议报文清洗
  - 避免同一套规则同时处理两类内容

- [ ] **A4. 引入“多步 stall-breaking”**
  - 从“生成单条请求”升级为“生成 2-3 步序列”
  - 明确要求输出能够穿透到新状态的连续请求链

- [ ] **A5. 使用状态转移覆盖作为 Live555 主验收指标**
  - 短期目标：transitions 从 **122** 提升到 **140+**
  - 中期目标：接近论文值 **160**

### Phase B: 单独解决 PureFTPD 运行早退

- [ ] **B1. 在 `perform_dry_run` 前后增加日志**
  - 精确判断早退/崩溃发生点

- [ ] **B2. 手动 replay enriched seed**
  - 验证是否是某个 enrichment 结果触发了 PureFTPD 的异常路径

- [ ] **B3. 检查 benchmark 目标目录中的 PureFTPD 实际运行配置**
  - Dockerfile
  - 启动命令
  - 环境变量传递
  - 清理脚本

- [ ] **B4. 必要时启用 ASAN 版本**
  - 目标不是立即提覆盖率
  - 目标是先确认 PureFTPD 提前退出的根因

### Phase C: 统一结果管理与验收标准

- [ ] **C1. 后续所有正式结果统一归档到 `Result/<日期>/`**
- [ ] **C2. 每次归档附带一份简要说明**
  - 运行命令
  - 模型配置
  - 运行时长
  - 关键指标
- [ ] **C3. 统一按以下优先级判断是否“达到预期”**
  1. 是否跑通
  2. 是否接近论文的 states / transitions / branches
  3. stability / hangs / execs/sec 是否明显恶化

---

## 8. 下一轮验收标准

### Live555

下一轮不再以 “stability > 90%” 作为唯一目标，而改为：

1. **硬性要求**
   - 无拒答 seed
   - grammar 输出可解析
   - stall 响应保持完整协议格式

2. **主结果要求**
   - transitions > 122
   - branches >= 当前 2898，不明显回退

3. **健康度要求**
   - stability 相比 38.55% 不下降
   - hangs 不继续上升
   - execs/sec 不低于当前水平

### PureFTPD

下一轮的最小成功标准不是覆盖率，而是：

1. `plot_data` 有实际记录
2. fuzzer 能稳定进入主循环
3. 能拿到第一份可分析的 `fuzzer_stats`

---

## 9. 备注

本计划是对 `20260421_minimax_migration_fix.md` 的后续推进版。  
前一份计划的重点是“把迁移后的脏输出问题止血”；本计划的重点是：

1. **把 Live555 从“能跑”推进到“状态探索接近论文基线”**
2. **把 PureFTPD 从“早退”推进到“可比较”**
3. **把 stability 从主目标降级为健康度指标**

