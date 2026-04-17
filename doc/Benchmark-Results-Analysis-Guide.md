# ChatAFL Fuzzing 结果分析指南

本文档说明如何解读 ChatAFL/AFLNet fuzzing 测试的输出结果。

---

## 1. 输出文件结构

```
out-<subject>-<fuzzer>/
├── cov_over_time.csv     # 覆盖率随时间变化数据
├── plot_data             # AFL 标准统计文件（二进制格式）
├── fuzzer_stats          # AFL 统计信息文本文件
├── queue/                # 所有生成的测试用例（种子）
├── replayable-crashes/   # 可复现的崩溃用例
├── replayable-hangs/     # 可复现的挂起用例
├── protocol-grammars/    # LLM 生成的协议语法（ChatAFL 特有）
├── stall-interactions/   # LLM stall 处理交互日志（ChatAFL 特有）
├── ipsm.dot             # 协议状态机图（GraphViz 格式）
├── regions/              # AFLNet 状态区域数据
└── replayable-queue/     # 可复现的队列用例
```

---

## 2. 各文件详细说明

### 2.1 cov_over_time.csv（覆盖率数据）

**格式：**
```csv
Time,l_per,l_abs,b_per,b_abs
1776350518,7.6,1806,4.6,843
1776350522,7.6,1806,4.6,844
```

| 列名 | 含义 |
|------|------|
| `Time` | Unix 时间戳 |
| `l_per` | 行覆盖率百分比 (Line Coverage %) |
| `l_abs` | 覆盖的行数绝对值 |
| `b_per` | 分支覆盖率百分比 (Branch Coverage %) |
| `b_abs` | 覆盖的分支数（边数）绝对值 |

### 2.2 plot_data（AFL 运行时统计）

**原始格式（逗号分隔）：**
```
unix_time,cycles_done,cur_path,paths_total,pending_total,pending_favs,map_size,unique_crashes,unique_hangs,max_depth,execs_per_sec,n_nodes,n_edges,chat_times
```

| 列名 | 含义 |
|------|------|
| `unix_time` | Unix 时间戳 |
| `cycles_done` | 完成的 fuzzing 周期数 |
| `cur_path` | 当前活跃路径数 |
| `paths_total` | 总路径数 |
| `unique_crashes` | 独特崩溃数 |
| `unique_hangs` | 独特挂起数 |
| `max_depth` | 最大探索深度 |
| `execs_per_sec` | 每秒执行次数 |
| `n_nodes` | 协议状态节点数（AFLNet） |
| `n_edges` | 状态转换边数（AFLNet） |
| `chat_times` | LLM 调用次数（ChatAFL） |

### 2.3 fuzzer_stats（统计摘要）

文本格式，可直接查看关键指标。

### 2.4 queue/（测试用例队列）

存放所有发现的测试用例文件，文件名格式：`id:XXXXXX_*`

### 2.5 protocol-grammars/（LLM 生成的语法）

LLM 提取的协议消息模板，JSON 格式。

**示例（FTP）：**
```json
{
  "USER":  ["USER <<username>>\r\n"],
  "PASS":  ["PASS <<password>>\r\n"],
  "RETR":  ["RETR <<file>>\r\n"],
  "STOR":  ["STOR <<file>>\r\n"]
}
```

### 2.6 stall-interactions/（Stall 处理日志）

包含 LLM stall 处理的请求-响应对：
- `request-X` - 发给 LLM 的请求
- `response-Y` - LLM 的响应

---

## 3. 关键指标解读

### 3.1 代码覆盖率（Code Coverage Metrics）

| 指标 | 查看位置 | 参考值范围 | 说明 |
|------|----------|-----------|------|
| **行覆盖率** | `cov_over_time.csv` 的 `l_per` | 5-20% | 协议实现通常较低 |
| **分支覆盖率** | `cov_over_time.csv` 的 `b_per` | 3-10% | — |

**本次测试数据：**

| Subject | 行覆盖率 | 行数 | 分支覆盖率 | 分支数 |
|--------|----------|------|------------|--------|
| Live555 (RTSP) | 7.6% | 1806 | 4.6% | 843 |
| PureFTPD (FTP) | 8.4% | 547 | 4.1% | 193 |

### 3.2 协议状态覆盖（State-Aware Metrics）

| 指标 | 查看位置 | 参考值范围 |
|------|----------|-----------|
| **状态节点数** | `plot_data` 的 `n_nodes` | 20-200 |
| **状态转换边数** | `plot_data` 的 `n_edges` | 50-500 |

**注意：** 当前测试中 `n_nodes` 和 `n_edges` 均为 0.0，表明 fuzzing 运行时间极短或状态机学习尚未开始。

### 3.3 有效消息比例（Valid Message Ratio）

**计算方法：**
```bash
# 有效种子数量
ls queue/ | wc -l

# 对比可复现的队列用例
ls replayable-queue/ | wc -l

# 有效率 = replayable-queue / queue
```

### 3.4 测试效率指标

| 指标 | 查看位置 | 说明 |
|------|----------|------|
| **TFC (首次崩溃时间)** | `replayable-crashes/` 中最早文件的时间戳 | 从开始到首次崩溃的时间 |
| **独特崩溃数量** | `plot_data` 的 `unique_crashes` | 真实漏洞数 |
| **独特挂起数量** | `plot_data` 的 `unique_hangs` | 超时/挂起数 |
| **每秒执行次数** | `plot_data` 的 `execs_per_sec` | Fuzzer 效率 |

### 3.5 LLM 调用统计

| 指标 | 查看位置 | 说明 |
|------|----------|------|
| **LLM 调用次数** | `plot_data` 的 `chat_times` | Stall 处理和语法提取的总调用 |
| **Token 消耗** | `stall-interactions/` 中的日志 | 可在 MiniMax API 日志中查看 |

---

## 4. 查看 LLM 生成内容的具体方法

### 4.1 查看协议语法

```bash
# 查看所有语法文件
cat out-<subject>-<fuzzer>/protocol-grammars/*

# 示例：查看 FTP 语法
cat out-pure-ftpd-chatafl/protocol-grammars/llm-grammar-output-0
```

### 4.2 查看 Stall 处理日志

```bash
# 查看 LLM 生成的 stall 突破消息
cat out-<subject>-<fuzzer>/stall-interactions/request-0
cat out-<subject>-<fuzzer>/stall-interactions/response-0
```

### 4.3 查看生成的种子

```bash
# 查看所有测试用例
ls -la out-<subject>-<fuzzer>/queue/

# 查看原始种子
cat out-<subject>-<fuzzer>/queue/id:000000*

# 查看 LLM 富化的种子（ChatAFL 特有）
cat out-<subject>-<fuzzer>/queue/enriched_*
```

### 4.4 查看崩溃用例

```bash
# 查看崩溃数量
ls out-<subject>-<fuzzer>/replayable-crashes/ | wc -l

# 查看具体崩溃用例
cat out-<subject>-<fuzzer>/replayable-crashes/id:*
```

---

## 5. 数据质量评估

### 5.1 当前测试数据问题

从测试结果来看，存在以下问题：

| 问题 | 现象 | 原因分析 |
|------|------|----------|
| **状态节点为 0** | `n_nodes = 0.0` | Fuzzing 时间过短，状态机未开始学习 |
| **plot_data 为空** | `plot_data` 文件 0 字节 | AFL 统计未写入即终止 |
| **覆盖率增长停滞** | 多行数据相同 | 运行时间极短（约 20 秒） |

### 5.2 建议的最低测试时长

| 目标 | 最短测试时间 |
|------|-------------|
| 初步验证（语法生成） | 5-10 分钟 |
| 状态覆盖评估 | 30-60 分钟 |
| 崩溃发现评估 | 2-4 小时 |
| 完整对比实验 | 24 小时 |

### 5.3 数据正常的标志

- `plot_data` 文件 > 1KB
- `n_nodes` 和 `n_edges` > 0
- `cov_over_time.csv` 中有多行不同数据
- `chat_times` > 0

---

## 6. 对比评估框架

### 6.1 评估维度

| 维度 | 指标 | 权重 |
|------|------|------|
| 代码覆盖深度 | `b_per`, `l_per` | 30% |
| 状态覆盖广度 | `n_nodes`, `n_edges` | 25% |
| 测试效率 | `execs_per_sec`, TFC | 20% |
| 漏洞发现能力 | `unique_crashes` | 25% |

### 6.2 对比结果模板

| 指标 | Fuzzer A | Fuzzer B | 差异 |
|------|----------|----------|------|
| 分支覆盖率 | X% | Y% | ±Z% |
| 状态节点数 | N | M | ±K |
| 独特崩溃数 | C | D | ±E |
| LLM 调用次数 | L | W | ±V |

---

## 7. 快速检查清单

测试完成后，使用以下清单验证数据质量：

- [ ] `cov_over_time.csv` 存在且有多行数据
- [ ] `plot_data` 文件 > 1KB
- [ ] `n_nodes` > 0（状态机已开始学习）
- [ ] `unique_crashes` 或 `unique_hangs` > 0（至少有一定产出）
- [ ] `protocol-grammars/` 有内容（LLM 语法提取成功）
- [ ] `queue/` 有测试用例（fuzzer 正在生成用例）

---

## 8. 附录：关键文件位置汇总

| 想要查看的内容 | 文件路径 |
|--------------|----------|
| 分支覆盖率 | `cov_over_time.csv` → `b_per` |
| 行覆盖率 | `cov_over_time.csv` → `l_per` |
| 状态节点数 | `plot_data` → `n_nodes` |
| 状态边数 | `plot_data` → `n_edges` |
| 独特崩溃数 | `plot_data` → `unique_crashes` |
| LLM 调用次数 | `plot_data` → `chat_times` |
| FTP 协议语法 | `protocol-grammars/*` |
| Stall 突破日志 | `stall-interactions/request-*`, `response-*` |
| 所有测试用例 | `queue/*` |
