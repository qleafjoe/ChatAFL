# ChatAFL 下一阶段推进计划

**制定时间**: 2026年04月27日  
**说明**: 本文件按“复现 ChatAFL -> 在复现基础上做效率和稳健性改进 -> 支撑低门槛会议/期刊投稿”的方向，重新调整后续计划。当前状态见 `plan/20260427_current_status.md`。

---

## 1. 当前阶段总判断

2026-04-24 结果说明:

1. ChatAFL 论文核心机制已经基本实现，Live555 状态探索已经接近论文量级。
2. PureFTPD 已经跑通并达到 28 states / 162 transitions，但它不是 ChatAFL 论文 RQ4 中列出的漏洞发现目标。
3. 当前正式结果只有 2/9 个靶机，且只有单次运行。
4. 当前没有 confirmed bug，不能声称漏洞发现能力已经复现。
5. AFL `stability` 不能作为项目评价指标，只能作为运行健康诊断字段。
6. 后续不能只继续优化 prompt 或 transitions，必须补齐时间效率、漏洞确认和对照实验。

因此下一阶段计划分为四条线:

1. 指标体系与自动汇总。
2. 漏洞发现闭环。
3. 同等时间/同等预算复现实验。
4. 小幅可发表改进。

---

## 2. Phase A: 统一指标体系

### A1. 建立单次 run 指标汇总脚本

- [ ] 新增 `scripts/summarize_run.sh` 或同等脚本。
- [ ] 输入一个结果目录，例如 `Result/20260424/out-live555-chatafl`。
- [ ] 输出以下指标:
  - duration from `plot_data`
  - final branches / branch coverage from `cov_over_time.csv`
  - final states / transitions from `plot_data`
  - branches/hour
  - transitions/hour
  - paths/hour
  - execs/sec
  - unique_crashes
  - unique_hangs
  - first_crash_time
  - first_hang_time
  - chat_times
- [ ] 验收:
  - 对 Live555 输出 `duration ~= 1.64h`、`transitions = 156`、`unique_hangs = 13`。
  - 对 PureFTPD 输出 `duration ~= 1.28h`、`transitions = 162`、`unique_hangs = 0`。

### A2. 建立同等时间比较指标

- [ ] 从 `plot_data` 和 `cov_over_time.csv` 提取固定时间点指标:
  - 10 min
  - 30 min
  - 60 min
  - 120 min
  - end
- [ ] 输出 CSV:
  - target
  - fuzzer
  - run_id
  - elapsed_seconds
  - branches
  - states
  - transitions
  - crashes
  - hangs
  - chat_times
- [ ] 验收:
  - 4.24 Live555 在 10min 附近 branches 为 2740。
  - 4.24 PureFTPD 在 10min 附近 branches 为 622。

### A3. 后续 README 必须加入效率指标

- [ ] 每个 `Result/<date>/README.md` 增加:
  - branches/hour
  - transitions/hour
  - time-to-first-crash
  - time-to-first-hang
  - confirmed bugs/hour
  - LLM calls per new transition
- [ ] 当前没有 confirmed bug 时必须写 `0 confirmed bugs`，不能用 hangs 代替漏洞。
- [ ] `stability` 只放在运行健康诊断段落，不进入论文核心评价指标表。

---

## 3. Phase B: 漏洞发现闭环

### B1. Live555 hang triage

- [ ] 对 `Result/20260424/out-live555-chatafl/replayable-hangs/` 中 13 个 hang 做 replay。
- [ ] 记录每个 hang:
  - 是否稳定复现。
  - 是否触发 ASAN/LSAN 报告。
  - 是否只是协议等待/超时。
  - 对应状态序列。
- [ ] 验收:
  - 输出一份 `Result/20260424/live555_hang_triage.md`。
  - 明确 13 个 hangs 中 confirmed bug 数量。

### B2. ASAN/LSAN 运行配置

- [ ] 确认 Live555、ProFTPD、Kamailio 是否已有 ASAN patch 或可用 ASAN 构建方式。
- [ ] 如果当前 Dockerfile 没有 ASAN 版本，先为 Live555 做最小 ASAN/LSAN 实验。
- [ ] 验收:
  - 能用 replay 输入得到 sanitizer 日志，或明确说明该目标当前只能做 hang replay。

### B3. 漏洞去重规则

- [ ] 建立漏洞去重口径:
  - crash signal
  - top stack frame
  - sanitizer type
  - triggered message sequence
  - target state sequence
- [ ] 验收:
  - 每个 confirmed bug 都能分配一个本地编号，如 `LOCAL-LIVE555-001`。

### B4. 对照论文 RQ4 bug 表

- [ ] 用 `doc/chatafl-paper-code-guide.md` 的 9 个 bug 作为复现清单。
- [ ] Live555 对照 Bug #1-#7。
- [ ] ProFTPD 对照 Bug #8。
- [ ] Kamailio 对照 Bug #9。
- [ ] 验收:
  - 维护一张表: `paper_bug_id / target / status / local evidence / missing reason`。

---

## 4. Phase C: 同等时间复现实验矩阵

### C1. 最低实验矩阵

最低可投稿实验矩阵:

| 目标 | 原因 |
|---|---|
| Live555 | 论文漏洞集中目标，当前 states/transitions 已接近论文 |
| PureFTPD | 当前已跑通的 FTP 目标，可做效率对照 |
| ProFTPD | 论文 Bug #8 所在目标 |
| Kamailio | 论文 Bug #9 所在目标 |

### C2. Fuzzer 对照

- [ ] 每个目标至少跑:
  - AFLNet
  - 当前 ChatAFL hardened 版本
- [ ] 若资源允许，再加:
  - ChatAFL-CL1
  - ChatAFL-CL2
  - 原始/未加 validator 版本

### C3. 时间预算

每个目标采用固定时间预算:

1. pilot: 2 小时，1 次运行。
2. 正式最小版: 2 小时，3 次重复。
3. 论文增强版: 6 小时或 12 小时，3 次重复。

验收口径:

- 不再把 1.28 小时结果和 7.70 小时结果直接比最终值。
- 所有表格必须按同等时间点或 AUC 比较。

### C4. 当前未覆盖缺口

- [ ] 当前靶机平台 9 个目标中，正式结果只有 2 个。
- [ ] 尚未正式归档目标:
  - DAAP/forked-daapd
  - FTP/BFTPD
  - FTP/LightFTP
  - FTP/ProFTPD
  - HTTP/Lighttpd1
  - SIP/Kamailio
  - SMTP/Exim
- [ ] 论文漏洞发现直接相关但未跑目标:
  - ProFTPD
  - Kamailio

---

## 5. Phase D: 小幅可发表改进

### D1. 推荐论文主线

建议主线:

**面向国产/兼容 LLM 的验证驱动 ChatAFL 复现与效率改进。**

核心贡献不写成“提出全新协议 fuzzing 框架”，而写成:

1. 复现 ChatAFL 时发现 LLM 输出污染会显著破坏协议 fuzzing。
2. 给出输出清洗、协议级 validator、结果归档和 replay triage 流程。
3. 在同等时间/预算下，提升有效状态探索速度并降低无效 hangs。
4. 给出国产/兼容 LLM 替换 OpenAI 后的工程经验和评价指标。

### D2. 技术改进优先级

优先级从高到低:

1. **指标汇总与复现自动化**
   - 没有统一指标，就无法写论文。

2. **漏洞 triage 闭环**
   - 没有 confirmed bug，就不能写漏洞发现速度。

3. **A3/A4 清洗拆分和 validator 完善**
   - 让 grammar JSON 与协议报文清洗解耦。
   - 增加 FTP/RTSP 协议级 validator。

4. **预算感知 LLM 调度**
   - 记录每次 LLM 调用收益。
   - 只在高价值 stall 或 seed enrichment 阶段调用。

5. **多步 stall-breaking**
   - 暂缓。
   - 只有在确认 stall LLM 触发路径和收益后再做。

---

## 6. 下一轮执行顺序

1. 做 `scripts/summarize_run.sh`，把已有 `Result/20260424` 转成统一指标表。
2. 对 Live555 13 个 replayable hangs 做 replay/triage。
3. 跑 Live555 2 小时重复实验，至少 3 次。
4. 跑 PureFTPD 2 小时重复实验，至少 3 次。
5. 构建并运行 ProFTPD 2 小时 pilot。
6. 构建并运行 Kamailio 2 小时 pilot。
7. 在有 baseline 后再决定是否实现预算感知调度或多步 stall-breaking。

---

## 7. 本轮不处理事项

按前一轮用户指令，本轮仍不处理:

1. 清理硬编码 token。
2. 轮换已经泄露或写入文档的 token。

这些是工程风险，但不纳入本轮计划。
