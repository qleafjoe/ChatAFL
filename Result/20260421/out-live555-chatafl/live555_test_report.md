# ChatAFL 测试结果分析报告：Live555 (RTSP)

基于从容器 `a3b1d91a221e` 提取出的 `out-live555-chatafl` 测试结果数据，结合 ChatAFL 论文中的评价指标，对此次 Fuzzing 过程进行深度分析。

## 一、 测试基本信息
* **测试目标**: `testOnDemandRTSPServer` (Live555)
* **目标协议**: RTSP
* **测试时长**: 约 **11.65 小时** (从 1776710097 到 1776752042，共 41945 秒)
* **执行总次数 (execs_done)**: 139,686 次
* **启动命令行**:
  `/home/ubuntu/chatafl/afl-fuzz -d -i /home/ubuntu/experiments/in-rtsp -x /home/ubuntu/experiments/rtsp.dict -o out-live555-chatafl -N tcp://127.0.0.1/8554 -P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none ./testOnDemandRTSPServer 8554`

---

## 二、 论文核心指标分析

### 1. 代码分支覆盖率 (Branch Coverage)
* **发现的分支总数 (`b_abs`)**: 2902
* **分支覆盖率百分比 (`b_per`)**: 15.7%
* **分析**: 覆盖率达到了 15.7%，但是在前 2 个小时内覆盖率快速爬升后，后续近 10 个小时内涨幅极其微弱。这说明 Fuzzer **陷入了严重的覆盖率高原（Coverage Plateau）**。

### 2. 代码行覆盖率 (Line Coverage)
* **发现的代码行总数 (`l_abs`)**: 5816
* **代码行覆盖率百分比 (`l_per`)**: 24.4%

### 3. 状态转移覆盖率 (State Transition Coverage)
在基于状态机的协议 Fuzzing（如 AFLNet/ChatAFL）中，状态和边（状态转移）的发现数量是衡量 Fuzzer 探索深度的最核心指标之一：
* **状态节点数 (`n_nodes`)**: 13 (从 `plot_data` 提取)
* **状态边数 (`n_edges`)**: 131 (从 `plot_data` 提取)
* **分析**: Live555 作为一个复杂的流媒体服务器，状态机较为庞大。经过近 12 小时的测试，Fuzzer 仅探索到了 13 个独特的状态节点和 131 条转移边。结合执行速度和稳定性异常，可以确认 Fuzzer 在状态空间的探索上受阻严重，未能有效突破浅层交互。

### 4. LLM Stall-Breaking 触发情况
* **交互次数**: 解压目录中的 `stall-interactions` 文件夹出现了 `prompt-61` 和 `response-61`。
* **分析**: 说明测试在半途中就已经快**耗尽了 LLM 调用的预算 (上限 64)**，且 LLM 生成的报文没能解锁新状态。

---

## 三、 异常参数与结果分析 (🚨 核心问题)

### 异常 1：稳定性极差 (Stability: 15.10%)
* **正常预期**: 优秀的 Fuzzing 测试中，Stability 应该在 **90% - 100%** 之间。
* **当前结果**: 仅为 **15.10%**。说明 Live555 在处理请求时存在极大的**非确定性**，导致 Fuzzer 无法复现和保留有趣的路径。

### 异常 2：极低的执行速度 (execs_per_sec: 0.88)
* **正常预期**: 平均速度通常也应在 50~200 次/秒以上。
* **当前结果**: 当前速率仅有 **0.88 次/秒**。

### 异常 3：大量的超时挂起 (unique_hangs: 42)
* **当前结果**: 发现 0 个 Crashes，但是有 **42** 个独特的 Hangs。

---

## 四、 LLM 返回质量与生成种子深度检查 (🚨 致命错误)

经过对提取结果文件中的 LLM 交互记录和种子文件分析，发现**导致上述所有异常的根本原因在于 LLM 返回质量极差且缺乏代码层面的防御和清洗**。

### 1. 种子增强 (Seed Enrichment) 产生拒绝回答与无效种子
查看 `queue` 目录中 LLM 增强生成的种子，发现了非常荒谬的数据：
* `id:000031,orig:enriched_28_rtsp_requests_mp3.raw` 的内容竟然是：
  ```text
  I’m sorry, but I can’t help with that
  ```
  **分析**：MiniMax 触发了安全机制或拒绝回复，而 ChatAFL **没有做任何校验，直接将拒绝回复的话语当作 RTSP 协议报文存入了初始种子库！**
* `id:000000,orig:enriched_0_rtsp_requests_mpg.raw` 的内容是：
  ```text
  OPTIONS rtsp://127.0.0.1:8554/mpeg1or2AudioVideoTest RTSP/1.0
  CSeq: 
  ```
  **分析**：LLM 生成中断或被错误截断，缺失了核心的 `\r\n\r\n` 结尾和 CSeq 数字。把这个发送给 Live555，会导致 Socket 永远挂起等待 Headers 结束，这就是引发 `unique_hangs: 42` 和 `execs/s: 0.88` 的罪魁祸首。

### 2. 语法提取 (Protocol Grammars) 包含 Markdown 污染
* 在 `protocol-grammars/llm-grammar-output-0` 中，LLM 虽然正确生成了 JSON，但却带了大量的口语化回答：
  ```text
  Below is a complete set of **client‑to‑server** request templates...
  ```json
  { ... }
  ```
  ### How to use these templates ...
  ```
  **分析**：如果 Fuzzer 的 JSON 解析器（json-c）没有做正则截取，或者遇到多余文本时崩溃退出，那么整个 Grammar-based 变异将完全失效。

### 3. Stall-breaking 返回请求格式残缺
* 查看 `stall-interactions/response-61` 等文件，发现 LLM 的返回仅有一行：
  ```text
  SETUP rtsp://27.0.0.1:8554/wavAudioTest/ RTSP/1.0
  ```
  **分析**：Prompt 中包含一句 `Output ONLY one single client request line`，大模型严格遵循了“line”这个词，导致它只输出了请求行（Request-Line），省略了 `CSeq` 等请求头和末尾的 `\r\n\r\n`。这种不完整的报文注入到网络流中，必定导致目标服务器 Hang 死。

---

## 五、 下一步修复建议

目前 Fuzzer 本身的执行引擎没问题，但被**脏数据**摧毁了性能。需要对 `chat-llm.c` 和 Prompt 进行如下改造：

1. **修改 Prompt 消除歧义**：
   将 `Output ONLY one single client request line` 改为 `Output ONLY one complete client request message, ending with \r\n\r\n`。
2. **强制内容校验与清洗 (非常关键)**：
   * 在提取 Seed Enrichment 的结果时，必须校验返回内容是否包含 `"RTSP/1.0"` 以及是否以 `\r\n\r\n` 结尾。
   * 如果检测到 `I'm sorry`，`can't help` 或不包含协议特征，必须丢弃该生成结果，进行 Retry。
3. **安全提取 JSON**：
   * 使用代码在 LLM 返回的字符串中查找第一个 `{` 和最后一个 `}`，或者使用正则表达式提取 ```json ... ``` 块，再丢给 `json-c` 解析，彻底屏蔽模型的废话。
