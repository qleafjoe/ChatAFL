# ChatAFL 迁移 MiniMax 专项修复计划

**制定时间**: 2026年04月21日  
**目标背景**: 解决将 OpenAI 接口替换为 MiniMax 模型后，在 Live555 (RTSP) 测试中暴露出的极低稳定性 (15%)、极慢速度 (0.88 次/秒)、大量挂起 (Hangs) 以及“脏数据”污染队列等严重问题。

---

## 🚨 问题 1：模型拒绝回答导致生成无效“毒种子”
* **问题表现**: `queue` 目录中发现部分 LLM 生成的种子内容为 `I’m sorry, but I can’t help with that`。Fuzzer 未做任何拦截，直接将其当成有效 RTSP 报文写入种子队列，导致服务器解析时发生异常崩溃或挂起。
* **解决方案**:
  1. **代码层防御 (Sanity Check)**: 在 `chat-llm.c` 中的 Seed Enrichment 模块接收到 LLM 响应后，增加校验函数。
  2. **特征过滤**: 检查返回内容是否包含拒绝回复的常见特征词（如 `sorry`, `help with that`, `As an AI`）。如果命中，直接丢弃该次结果并触发 Retry。
  3. **合法性过滤**: 校验返回的文本是否包含目标协议标识（如 `RTSP/1.0` 或 HTTP 的 `HTTP/1.1`）。

## 🚨 问题 2：Prompt 歧义导致生成残缺报文，引发大规模 Hangs
* **问题表现**: 在 Stall-breaking 阶段，系统提示词要求 `Output ONLY one single client request line.`。大模型对此的理解是“只输出第一行”，因此仅返回了 `SETUP rtsp://...`，省略了必不可少的 `CSeq` 头和 `\r\n\r\n` 结尾。发送这种残缺包导致 Live555 服务端无限期死等，引发了高达 42 次的 unique_hangs，并将整体执行速度拖垮至 0.88 次/秒。
* **解决方案**:
  1. **修订 Prompt (`chat-llm.c`)**: 将原有的 Prompt 约束修改为：
     `Output exactly ONE complete client request message. It MUST include necessary headers (like CSeq) and MUST end with \r\n\r\n. Do NOT output only the request line.`
  2. **补全机制**: 在 C 代码将 payload 喂给目标前，检查其末尾是否以 `\r\n\r\n` 结束。如果没有，可以尝试用代码主动补全，或者判定为无效生成。

## 🚨 问题 3：语法提取 (Grammar) 受到 Markdown 与闲聊文本污染
* **问题表现**: LLM 确实生成了 JSON 格式的语法模板，但在其前后加上了 ` ```json ` 的 Markdown 标记以及大量的啰嗦解释（如 `Below is a complete set...`）。这会导致原始基于 `json-c` 的解析器报错，使基于语法的变异 (Grammar Mutation) 失效。
* **解决方案**:
  1. **Prompt 强调约束**: 在提取语法的 System Prompt 中加入 `Output strictly valid JSON. No markdown formatting, no code blocks, no explanations.`。
  2. **安全提取算法 (JSON Extractor)**: 在 `chat-llm.c` 解析之前，编写一段字符串截取逻辑。找到第一个出现 `{` （或 `[`） 的位置，以及最后一个 `}` （或 `]`）的位置，将其截取出来再传给 `json_tokener_parse()`，彻底从代码层面免疫大模型的“废话”。

## 🚨 问题 4：Fuzzer 稳定性极差 (Stability: 15.10%)
* **问题表现**: Live555 跑出了 15.10% 的极低稳定性。这意味着 85% 的输入在变异后产生的路径无法重现。
* **调研发现**:
  1. **官方补丁确认**: 检查 `benchmark/subjects/RTSP/Live555/fuzzing.patch` 发现，Session ID 随机化问题已被补丁修复（锁定为 `8888`）。
  2. **脏数据干扰**: 目前的主要不稳定源极大概率是 LLM 注入的“随机废话”和“残缺报文”。服务器在处理这些非协议字符时的行为具有高度的随机性和超时依赖性。
* **解决方案**:
  1. **前置修复**: 必须先完成 Step 1/2/3 的内容过滤，排除随机文本对服务器状态机的干扰。
  2. **内存排查**: 检查 `chat-llm.c` 中 MiniMax 接口的内存释放逻辑，确保没有损坏 Fuzzer 自身的 Bitmaps。
  3. **编译检测**: 使用 AddressSanitizer (`-fsanitize=address`) 重新编译 `afl-fuzz`。

## 🚨 问题 5：LLM 调用阈值 (`CHATTING_THRESHOLD`) 过低
* **问题表现**: 当前 `config.h` 中 `CHATTING_THRESHOLD` 默认为 64。在 11 小时的测试中，该额度会过早耗尽，导致 Fuzzer 后半程无法调用模型突破瓶颈。
* **解决方案**:
  1. **调优参数**: 将 `CHATTING_THRESHOLD` 提升至 **512**。
  2. **平衡点**: 512 次调用在 11 小时内约每 1.2 分钟可触发一次，足以覆盖大部分覆盖率停滞阶段，且 API 成本可控。

---

## 📈 运行时间与效率预估 (Performance Estimation)
基于 11 小时 (39,600 秒) 的测试目标：
*   **预估执行总数**: 若修复速度瓶颈（恢复至 20-50 execs/s），总执行次数预计可达 **80万 - 150万次**。
*   **LLM 交互开销**: 512 次调用 x 3秒平均延迟 ≈ 1536 秒 (约 25 分钟)。
*   **时间占比**: API 等待时间仅占总时长的 4%，对 Fuzzing 效率影响极小。

---

### 📅 执行计划安排
- [ ] **Step 1**: 在 `chat-llm.c` 中实现通用的 `clean_llm_response()` 函数，剥离 Markdown 块并过滤 `sorry` 等非法文本。
- [ ] **Step 2**: 全面检索并修改 `chat-llm.c` 中的所有的 Prompt 模板，消除引起残缺输出的歧义描述。
- [ ] **Step 3**: 针对 Seed Enrichment 和 Stall-breaking 模块，加入强制检查 `\r\n\r\n` 结尾的逻辑。
- [ ] **Step 4**: 修复完成后，重新编译 Fuzzer (`make clean all`)。
- [ ] **Step 5**: 执行本地验证测试 (详见下文)。

---

## 🛠️ 逻辑与策略深度优化 (Strategy Optimization)
针对大模型生成内容“无法实际使用”以及“突破瓶颈效率低”的问题，需进行深层策略调整：

1. **语法可用性强制校验**: 
   - 增加 JSON Schema 基础校验。如果生成的 `protocol_patterns` 列表为空或解析失败，不应启动 Fuzzing，而是立即触发重新提取。
   - 对提取出的 Regex 模式进行预测试，确保其能匹配到基础的种子文件。

2. **Stall-breaking 语义增强**: 
   - 目前 Stall Prompt 过于简略。需在 Prompt 中加入当前已探索到的 **State Code 序列** 和 **最近一次失败的响应**，让大模型知道“哪里走不通”。
   - 引入“多步预测”：让大模型生成一个包含 2-3 个连续请求的 Sequence，而不仅仅是单个请求，以更高概率穿透深层状态。

3. **种子多样性约束**: 
   - 在 Seed Enrichment 时，强制大模型必须包含不同的 RTSP 方法（如 DESCRIBE, SETUP, PLAY），避免其只会生成重复的 OPTIONS。

---

## 🧪 本地验证测试计划 (Local Test Plan)
为确保修复后的系统不再产生“毒种子”并能有效运行，需执行以下本地测试流程：

### 1. 单元测试 (Unit Test) - `clean_llm_response`
* **操作**: 编写一个临时的 `test-chat.c`，模拟 LLM 返回带 Markdown 块、拒绝词、残缺结尾的字符串。
* **目标**: 确认 `clean_llm_response` 能 100% 提取出 `{ ... }` 且补齐 `\r\n\r\n`。

### 2. 离线 Prompt 仿真 (Prompt Simulation)
* **操作**: 手动将生成的 Prompt 粘贴给 MiniMax 网页版或 API 调试工具。
* **目标**: 观察返回结果是否仍然包含“残缺报文”。如果网页版依然返回残缺内容，说明 Prompt 还需要进一步增强约束。

### 3. 冒烟测试 (Smoke Test) - Live555 短程运行
* **操作**: 在宿主机运行单容器测试：`./run.sh 1 5 live555 chatafl` (运行 5 分钟)。
* **检查项**:
  - `out/stall-interactions/`：检查 Response 是否已包含完整 Headers。
  - `out/queue/`：检查 enriched 种子是否不再包含 "I'm sorry"。
  - `out/fuzzer_stats`：确认 `execs_per_sec` 恢复至正常水平（应 > 20），`stability` 恢复至 90% 以上。

### 4. 语法有效性检查
* **操作**: 检查 `out/protocol-grammars/llm-grammar-output-0`。
* **检查项**: 该文件必须是纯净的 JSON，不含任何说明性文字。可以用 `jq . < file` 进行自动校验。

