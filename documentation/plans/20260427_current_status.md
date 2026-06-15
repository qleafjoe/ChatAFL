# ChatAFL 当前状态记录

**记录时间**: 2026年04月27日  
**记录目的**: 按“核心机制已基本实现并完成主要复现 -> 在复现基础上改进 -> 形成低门槛会议/期刊论文”的目标，重新整理项目状态、评价指标、当前靶机平台缺口和下一步优先级。  
**迁移说明**: 本文件接收并更新 `plan/20260423_current_status.md` 的状态说明，也修正 `plan/20260427_result_review_and_next_steps.md` 早先过度聚焦 states/transitions 的口径。

---

## 1. 当前任务方向

当前项目的主目标不是单纯把 Live555 transitions 追到论文数值，也不是只做国产 / MiniMax 接口迁移。

截至 2026-04-27，项目状态应更新为:

1. **ChatAFL 论文核心机制已基本实现**
   - 已具备 LLM grammar 提取、seed enrichment、stall-breaking 请求生成的主体流程。
   - 已完成国产/兼容 LLM 接口适配，并加入输出清洗、prompt 上下文清洗和 RTSP 协议级 validator。
   - 已有 `test/test_clean.c`、`test/test_rtsp_validation.c` 等最小验证用例。

2. **论文主要状态探索现象已基本复现**
   - Live555 2026-04-24 结果达到 15 states / 156 transitions，接近本地整理的论文 Live555 transitions 平均值 160.0。
   - PureFTPD 已从“未跑通/无有效 plot_data”恢复为可分析运行，达到 28 states / 162 transitions。
   - 因此当前不应再表述为“尚未复现论文”，更准确的表述是“核心机制与状态探索复现基本完成，漏洞发现复现尚未闭环”。

3. **仍未闭环的是漏洞确认和泛化评估**
   - 当前 confirmed bugs 仍为 0。
   - Live555 的 13 个 hangs 仍需 replay + sanitizer + stack trace 去重。
   - ProFTPD 和 Kamailio 作为论文 RQ4 漏洞相关目标尚未形成正式归档结果。

更准确的任务方向是:

1. **巩固已完成的 ChatAFL 复现**
   - 固化 grammar、enriched seed、stall-breaking 请求生成流程。
   - 用同等时间和重复实验确认状态探索结果是否可复现。
   - 将漏洞发现从 hangs/crashes 线索推进到 confirmed bug 证据。

2. **再在复现实验基础上做小幅但可解释的改进**
   - 改进点不必追求顶会级别。
   - 更适合低门槛会议/期刊的方向是: 国产 LLM 适配、输出验证、防污染、效率/成本控制、漏洞复现流程自动化、时间预算下的发现效率提升。

3. **论文叙事应从“最终覆盖值”扩展为“同等时间/同等预算下的效率”**
   - 不能只报告最终 states/transitions/branches。
   - 必须报告达到同等覆盖/状态/漏洞发现所需时间。
   - 必须报告漏洞发现速度、崩溃/挂起确认速度、exec/s、LLM 调用次数与有效收益。

---

## 2. 结果与代码目录口径

当前正式结果以 `Result/` 为准。

最新正式归档结果:

- `Result/20260424/out-live555-chatafl`
- `Result/20260424/out-pure-ftpd-chatafl`
- `Result/20260424/out-live555-chatafl_1.tar.gz`
- `Result/20260424/out-pure-ftpd-chatafl_1.tar.gz`

`benchmark/results-live555/` 与 `benchmark/results-pure-ftpd/` 是运行脚本临时出口。2026-04-24 的两个 tar 已经字节级归档到 `Result/20260424/`。

`benchmark/subjects/` 是当前构建出的靶机平台，包含 9 个目标:

| 协议 | 目标 |
|---|---|
| DAAP | forked-daapd |
| FTP | BFTPD |
| FTP | LightFTP |
| FTP | ProFTPD |
| FTP | PureFTPD |
| HTTP | Lighttpd1 |
| RTSP | Live555 |
| SIP | Kamailio |
| SMTP | Exim |

当前已经有正式结果的目标只有:

- Live555
- PureFTPD

因此当前实验覆盖面是 **2/9 个目标**。从投稿角度看，这还不足以支撑“泛化改进”结论，只适合作为 pilot / 复现阶段结果。

---

## 3. 评价指标口径调整

后续评价指标分为五组。

重要口径修正:

`stability` 不再作为本项目论文评价指标。它是 AFL 对路径可重复性的内部运行诊断字段，在网络协议服务端 fuzzing 中会受到超时、服务端状态、清理脚本、网络交互和非确定性响应影响。它可以用于发现运行健康问题，但不能直接衡量 ChatAFL 机制是否有效，也不能作为论文中的核心效果指标。

### 3.1 复现基础指标

沿用 ChatAFL / ProFuzzBench:

1. branch coverage
2. state coverage
3. state transition coverage
4. unique crashes
5. unique hangs

### 3.2 时间效率指标

必须新增并长期记录:

1. `branches/hour`
2. `states/hour`
3. `transitions/hour`
4. `time-to-threshold`
   - 例如达到 130/145/156 transitions 所需时间
   - 达到某个 branch coverage 所需时间
5. coverage/state AUC
   - 用于避免只比较最终点

### 3.3 漏洞发现指标

漏洞发现不能只看 `unique_crashes`，需要分层:

1. raw crashes/hangs
2. replayable crashes/hangs
3. ASAN/LSAN 可确认崩溃或泄漏
4. 按 stack trace / root cause 去重后的 unique bugs
5. time-to-first-crash
6. time-to-first-confirmed-bug
7. confirmed bugs/hour

当前结果中:

- Live555: `unique_crashes = 0`，`unique_hangs = 13`
- PureFTPD: `unique_crashes = 0`，`unique_hangs = 0`

因此当前还没有可声称的 confirmed vulnerability。Live555 的 13 个 hangs 只能算待 triage 线索，不能直接算漏洞。

### 3.4 LLM 效率指标

由于研究目标涉及 LLM，后续必须记录:

1. `chat_times`
2. 每次 LLM 输出是否通过 grammar/protocol validator
3. 每次 LLM 输出是否带来新 path/state/transition/branch
4. new transitions per LLM call
5. new branches per LLM call
6. LLM 调用耗时与 token/cost 估算

2026-04-24 结果显示:

- Live555: `chat_times = 1`
- PureFTPD: `chat_times = 0`

这说明当前 4.24 的提升不能简单归因于在线 stall-breaking LLM。后续需要拆分 grammar、enrichment、普通变异、stall-breaking 的贡献。

### 3.5 复现可信度与运行健康诊断

需要记录的评价指标:

1. repeated runs 的均值和方差
2. fixed seed / fixed prompt cache 下的方差
3. exec/s
4. hangs 是否可 replay
5. confirmed bugs 是否可重复触发

需要保留但只作诊断的字段:

1. AFL `stability`
2. `variable_paths`
3. timeout/hang 比例

当前只有单次 4.24 结果，不足以支撑重复性结论。后续判断“复现可信度”应看重复运行的均值/方差、同等时间曲线、replay 成功率和 confirmed bug 证据，而不是单独看 AFL `stability`。

---

## 4. 2026-04-24 当前总体指标

### 4.1 Live555

结果目录: `Result/20260424/out-live555-chatafl`

| 指标 | 数值 |
|---|---:|
| plot_data 时长 | 1.64 小时 |
| branches | 2837 |
| branch coverage | 15.3% |
| states | 15 |
| transitions | 156 |
| paths_total | 790 |
| execs_per_sec | 12.62 |
| unique_crashes | 0 |
| unique_hangs | 13 |
| first_hang_time | 约 98 秒 |
| chat_times | 1 |
| branch 增长速度 | 约 782.64 branches/hour |
| transition 增长速度 | 约 64.18 transitions/hour |

阶段判断:

Live555 的 states/transitions 已接近论文量级，说明论文中的状态探索现象已经基本复现。当前不能因为 AFL `stability = 18.93%` 否定该复现结果；该字段只说明运行存在较强路径非确定性或服务端状态/超时噪声，需要作为工程诊断继续排查。对论文而言，Live555 目前更适合作为“状态探索复现基本成功，漏洞发现尚未闭环”的证据。

### 4.2 PureFTPD

结果目录: `Result/20260424/out-pure-ftpd-chatafl`

| 指标 | 数值 |
|---|---:|
| plot_data 时长 | 1.28 小时 |
| branches | 993 |
| branch coverage | 21.1% |
| states | 28 |
| transitions | 162 |
| paths_total | 341 |
| execs_per_sec | 14.76 |
| unique_crashes | 0 |
| unique_hangs | 0 |
| first_hang_time | 无 |
| chat_times | 0 |
| branch 增长速度 | 约 446.22 branches/hour |
| transition 增长速度 | 约 67.09 transitions/hour |

阶段判断:

PureFTPD 已从“未跑通”变为“可比较”，但它不是 ChatAFL 论文中列出的漏洞发现目标。当前它更适合作为 FTP 状态探索和效率评测目标，而不是漏洞复现目标。

---

## 5. 与 ChatAFL 论文漏洞发现目标的差距

根据本地 `doc/chatafl-paper-code-guide.md` 的整理，ChatAFL 论文中 RQ4 共报告 9 个零日漏洞:

| ID | 目标 | 类型 |
|---|---|---|
| 1-5, 7 | Live555 | UAF / heap overflow |
| 6 | Live555 | memory leak |
| 8 | ProFTPD | heap overflow |
| 9 | Kamailio | memory leak |

当前状态:

1. Live555 已测，但 `unique_crashes = 0`，13 个 hangs 尚未 ASAN/LSAN triage。
2. PureFTPD 已测，但论文 RQ4 的 bug 表中不是 PureFTPD。
3. ProFTPD 未形成正式结果。
4. Kamailio 未形成正式结果。

因此，从“复现 ChatAFL 漏洞发现能力”的角度看:

- 已确认复现的漏洞数: **0**
- 待 triage 的 Live555 hang 线索: **13**
- 论文 RQ4 中尚未确认复现的漏洞: **9/9**
- 当前靶机平台中尚未正式运行归档的目标: **7/9**
- 与论文漏洞发现直接相关但未正式运行的目标: **ProFTPD、Kamailio**

这不是坏消息，而是说明当前项目已经从“能否实现与复现”推进到“如何确认漏洞、补齐对照和形成可发表评价”的阶段。

---

## 6. 当前最关键的技术判断

### 6.1 论文方向应调整

原先计划过度强调 Live555 transitions。现在应调整为:

**国产 LLM 环境下，验证驱动与预算感知的 ChatAFL 复现及效率改进。**

可投稿叙事:

1. 复现 ChatAFL 时，国产/兼容 LLM 容易产生 Markdown、拒答、残缺协议报文，直接破坏 fuzzing 效率。
2. 引入输出清洗、协议级 validator、结果归档和 replay/triage 流程后，可以恢复状态探索能力，并显著改善单位时间状态转移增长。
3. 后续再加入预算感知调度与漏洞 triage，报告同等时间/同等调用预算下的覆盖和漏洞发现效率。

这条线比“我发现一个新算法大幅超越 ChatAFL”更适合低门槛会议/期刊，也更贴合当前工程基础。

### 6.2 当前最大缺口不是 transitions，而是 bug-finding 闭环

现在最缺的是:

1. ASAN/LSAN 版本运行。
2. crashes/hangs replay。
3. stack trace 去重。
4. 与论文 9 个 bug 的触发条件对照。
5. time-to-first-confirmed-bug。

没有这条闭环，不能写“漏洞发现速度提升”，只能写“状态转移和覆盖效率提升”。

### 6.3 当前靶机平台还不够支撑泛化结论

当前正式结果只有 Live555 和 PureFTPD。下一步至少应补:

1. ProFTPD: 对应论文 Bug #8。
2. Kamailio: 对应论文 Bug #9。
3. AFLNet baseline: 至少对 Live555 和 PureFTPD 做相同时间对照。

如果时间有限，最低投稿实验矩阵可以是:

| 目标 | 角色 |
|---|---|
| Live555 | 复现 ChatAFL 状态探索与漏洞线索 |
| PureFTPD | FTP 可比较性与状态探索效率 |
| ProFTPD | 论文漏洞复现目标 |
| Kamailio | 论文漏洞复现目标 |

---

## 7. 当前执行结论

1. ChatAFL 核心机制已经基本实现，Live555 状态探索结果已经基本复现论文量级。
2. 当前还没有 confirmed vulnerability，漏洞发现速度指标尚未建立。
3. `stability` 不能作为项目评价指标，只能作为 AFL 运行健康诊断字段。
4. 当前平台有 9 个目标，正式结果只有 2 个；实验覆盖面不足。
5. 下一阶段优先级应改为:
   - 建立统一指标脚本。
   - 建立 ASAN/LSAN + replay + 去重漏洞确认流程。
   - 对 Live555/PureFTPD 做同等时间重复实验。
   - 补 ProFTPD/Kamailio。
   - 再考虑新的算法增强。
