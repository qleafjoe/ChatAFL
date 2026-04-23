# Pure-FTPD 11小时测试报告 (12/out-pure-ftpd-chatafl)

**分析时间**: 2026年04月21日  
**测试周期**: 1440分钟 (实际可能在早期挂起/崩溃)  
**目标状态**: 极度异常 (执行速度 0，覆盖率停滞)

---

## 🔍 核心问题分析

### 1. 种子队列被“毒种子”严重污染 (Refusal Seeds)
通过对 `queue` 目录的检索，发现多个种子文件（如 `id:000033`, `id:000037`, `id:000041`）内容为大模型的拒绝话术：
> `I’m sorry, but I can’t help with that`

**后果**: Fuzzer 将这些无意义的文本视为有效的 FTP 报文。由于 Fuzzer 会优先测试“新发现”的种子，这些拒绝话术由于不包含有效 FTP 指令，导致服务器反馈异常或状态机卡死，极大地浪费了 Fuzzing 算力。

### 2. 种子强化阶段产生的二进制/乱码污染 (Binary Garbage)
查看 `id:000042` 等强化后的种子，发现其中充斥着大量不可读字符：
> `USER fuzzing`  
> `the ", 0ks`  
> `PASS fuzzing`  
...  

**原因分析**: MiniMax 在返回 `enrich_sequence` 结果时，可能由于编码问题或模型幻觉，在正常的 FTP 命令之间插入了二进制垃圾。由于原代码 `chat-llm.c` 盲目信任 LLM 的输出，直接将其写入二进制文件。

### 3. 语法提取模块解析失败 (Grammar Failure)
查看 `protocol-grammars/llm-grammar-output-0` 发现：
*   **Markdown 标记**: JSON 被包裹在 ` ```json ` 块中。
*   **啰嗦文本**: 开头包含了说明性文字，结尾包含了占位符解释表。

**后果**: `json-c` 库在解析非严格 JSON 的字符串时会报错返回 NULL，导致 ChatAFL 核心的“语法感知变异 (Structure-aware Mutation)”退化成了普通的 Havoc 变异。

### 4. 运行状态极度不稳定
*   **plot_data**: 为空，说明 Fuzzer 除了初始同步阶段外，几乎没有记录到有效的执行数据。
*   **execs_per_sec**: 趋近于 0。

---

## 🛠️ 针对 Pure-FTPD 的补丁建议 (纳入统一计划)

1.  **全局 Response 清洗**: 必须在 `chat_with_llm` 层面对所有返回值执行 `clean_llm_response`，剥离 Markdown 块并检查关键词。
2.  **严格 JSON 提取**: 使用正则表达式或大括号计数器定位 JSON 内容，忽略前后的“废话”。
3.  **二进制内容过滤**: 在将 LLM 响应写入 `enriched_` 文件前，执行非 ASCII 字符检测或协议关键词检测，防止“乱码种子”污染。
