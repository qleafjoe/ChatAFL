# ChatAFL 发表导向指标与缺口分析

**记录时间**: 2026年04月27日  
**目的**: 将项目从“单目标复现状态覆盖”调整为“核心机制复现可信度、时间效率、漏洞发现速度、低门槛投稿可行性”的综合评价。

---

## 1. 论文目标重新定义

当前最现实的论文目标是:

**在国产/兼容 LLM 环境下复现 ChatAFL，并通过验证驱动的输出清洗、协议级校验和预算感知评估，提高协议 fuzzing 的单位时间有效探索与漏洞确认效率。**

这比“提出一个全新 fuzzer 并全面超过 ChatAFL”更符合当前基础，也更适合低门槛会议/期刊。

截至 2026-04-27，项目应按“核心机制与主要状态探索现象已基本复现”来表述，而不是按“尚未复现”来表述。当前缺口集中在 confirmed bug、重复实验、AFLNet 对照和 ProFTPD/Kamailio 论文漏洞目标。

---

## 2. 指标体系

后续论文表格至少包含以下指标:

| 类别 | 指标 |
|---|---|
| 覆盖 | branches, branch coverage, lines, line coverage |
| 状态 | states, transitions |
| 时间效率 | branches/hour, states/hour, transitions/hour, time-to-threshold, AUC |
| 漏洞 | raw crashes, raw hangs, replayable crashes/hangs, confirmed bugs |
| 漏洞速度 | time-to-first-crash, time-to-first-confirmed-bug, confirmed bugs/hour |
| 复现可信度 | repeated-run mean/std, replay success rate, fixed-seed/fixed-prompt variance |
| 运行健康诊断 | execs/sec, variable paths, timeout/hang ratio, AFL stability |
| LLM 成本 | chat_times, valid responses, useful responses, new transitions per LLM call |

其中 confirmed bugs 必须经过 replay 与 sanitizer/stack trace 去重，不能用 hangs 直接替代。

`stability` 不作为论文核心评价指标。它是 AFL 路径可重复性诊断字段，在网络协议服务端 fuzzing 中受超时、状态清理、网络交互和服务端非确定性影响较大，只能用于解释运行健康问题，不能用于衡量 ChatAFL 复现或改进效果。

---

## 3. 当前结果按新指标重算

### Live555 2026-04-24

| 指标 | 数值 |
|---|---:|
| plot_data 时长 | 1.64 h |
| branches | 2837 |
| branch coverage | 15.3% |
| states | 15 |
| transitions | 156 |
| branches/hour | 约 782.64 |
| transitions/hour | 约 64.18 |
| execs/sec | 12.62 |
| unique_crashes | 0 |
| unique_hangs | 13 |
| time-to-first-hang | 约 98 s |
| time-to-first-confirmed-bug | 无 |
| confirmed bugs | 0 |
| chat_times | 1 |

### PureFTPD 2026-04-24

| 指标 | 数值 |
|---|---:|
| plot_data 时长 | 1.28 h |
| branches | 993 |
| branch coverage | 21.1% |
| states | 28 |
| transitions | 162 |
| branches/hour | 约 446.22 |
| transitions/hour | 约 67.09 |
| execs/sec | 14.76 |
| unique_crashes | 0 |
| unique_hangs | 0 |
| time-to-first-hang | 无 |
| time-to-first-confirmed-bug | 无 |
| confirmed bugs | 0 |
| chat_times | 0 |

---

## 4. 当前未发现/未确认缺口

以 ChatAFL 论文 RQ4 的 9 个 bug 为复现清单:

| 范围 | 数量 | 当前状态 |
|---|---:|---|
| 论文报告 bug 总数 | 9 | 复现清单 |
| 已确认复现 bug | 0 | 尚无 ASAN/LSAN/replay 证据 |
| Live555 paper bugs | 7 | 目标已跑，但未确认复现 |
| ProFTPD paper bugs | 1 | 目标未正式运行 |
| Kamailio paper bugs | 1 | 目标未正式运行 |
| Live555 hang 线索 | 13 | 待 triage，不可直接算漏洞 |

以当前靶机平台为范围:

| 范围 | 数量 |
|---|---:|
| benchmark/subjects 目标总数 | 9 |
| 已正式归档目标 | 2 |
| 未正式归档目标 | 7 |
| 与论文漏洞直接相关但未正式运行目标 | 2 |

结论:

当前项目已经基本复现 ChatAFL 的核心机制与状态探索现象，但漏洞发现能力尚未闭环。论文若要声称 bug-finding 或漏洞发现速度提升，必须先补 replay + sanitizer + 去重。

---

## 5. 最小可投稿实验设计

最低配置:

| 维度 | 最小要求 |
|---|---|
| 目标 | Live555, PureFTPD, ProFTPD, Kamailio |
| Fuzzer | AFLNet, hardened ChatAFL |
| 重复次数 | 每目标每 fuzzer 3 次 |
| 时间预算 | 2 小时 pilot，正式至少 6 小时更稳 |
| 指标 | 覆盖、状态、时间效率、crash/hang、confirmed bug |

如果资源不足，先做:

1. Live555: 3 次 2 小时，用于验证状态探索和 hang triage。
2. ProFTPD: 1 次 2 小时 pilot，用于对齐论文 Bug #8。
3. Kamailio: 1 次 2 小时 pilot，用于对齐论文 Bug #9。

---

## 6. 推荐投稿故事

推荐题目方向:

**面向国产大模型的 ChatAFL 复现、验证增强与效率评估**

可写贡献:

1. 复现 ChatAFL 在国产/兼容 LLM 环境中的主要失败模式。
2. 提出轻量的输出清洗和协议级 validator，减少无效输入与 hangs。
3. 引入同等时间/同等 LLM 预算的评价方法，避免只比最终覆盖。
4. 建立 replay + sanitizer 的漏洞确认流程，报告 confirmed bugs 或明确报告未确认缺口。

风险:

1. 如果没有 confirmed bug，论文只能弱化为覆盖/状态/效率改进。
2. 如果没有 AFLNet baseline，审稿时缺乏对照。
3. 如果只有 2 个目标，泛化性不足。

---

## 7. 立即行动

1. 写统一指标脚本。
2. triage Live555 13 个 hangs。
3. 补 AFLNet baseline。
4. 补 ProFTPD/Kamailio pilot。
5. 再决定是否做新的算法增强。
