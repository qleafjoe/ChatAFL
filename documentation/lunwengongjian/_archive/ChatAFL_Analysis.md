# ChatAFL 完整流程与功能详细分析

## 一、整体架构概览

ChatAFL 是基于 AFLNet 改进的协议模糊测试工具（NDSS 2024），核心创新是引入大语言模型（GPT-3.5-turbo）在三个关键环节辅助模糊测试。代码量约 10,933 行（比 AFLNet 基线多约 1,500 行 LLM 相关代码）。

**核心源文件：**
- `afl-fuzz.c`（10,933行）— 主模糊测试引擎
- `chat-llm.c`（1,183行）— LLM 通信与提示词构建
- `chat-llm.h`（94行）— 常量定义与数据结构
- `aflnet.c`（2,168行）— 协议解析（支持 RTSP/FTP/SMTP/SIP/HTTP 等 11 种协议）
- `config.h`（369行）— 配置常量

**三个变体：**
- **ChatAFL**（完整版）：三大 LLM 策略全部启用
- **ChatAFL-CL1**：仅结构感知变异（无种子丰富、无停滞突破）
- **ChatAFL-CL2**：结构感知变异 + 种子丰富（无停滞突破）

---

## 二、从启动到结束的完整流程

### 阶段 1：初始化（`main()` 函数，约 line 10500）

```
1. 解析命令行参数（-P RTSP 选择协议，-E 启用状态感知模式）
2. 设置共享内存（64KB bitmap 用于边覆盖率反馈）
3. 初始化已实现状态机（IPSM）
4. 创建输出目录结构
```

### 阶段 2：LLM 语法提取（line 10686）

```c
if (protocol_selected) {
    protocol_patterns = kl_init(rang);
    message_types_set = kh_init(strSet);
    setup_llm_grammars();    // 第一次 LLM 调用：语法提取
    enrich_testcases();      // 第二次 LLM 调用：种子丰富
}
read_testcases();            // 加载所有种子（原始 + enriched）
```

### 阶段 3：干运行与队列初始化

```
perform_dry_run()   — 对每个初始种子执行一次，校准执行时间
cull_queue()        — 标记 favored 种子
show_init_stats()   — 显示初始化统计
```

### 阶段 4：主模糊测试循环（line 10746）

```
while(1) {
    1. choose_target_state()  — 选择目标协议状态（轮询/随机/优先）
    2. cull_queue()           — 更新该状态的 favored 种子
    3. choose_seed()          — 选择到达该状态的种子
    4. fuzz_one()             — 对种子执行变异与测试
    5. 更新统计、检查同步
}
```

### 流程图

```
┌─────────────────────────────────────────────────────────────┐
│                        启动阶段                              │
├─────────────────────────────────────────────────────────────┤
│  解析参数 → 设置共享内存 → 初始化IPSM → 创建输出目录          │
│       ↓                                                      │
│  setup_llm_grammars()  ← LLM 提取协议语法（5次自一致性）      │
│       ↓                                                      │
│  enrich_testcases()    ← LLM 丰富种子语料库                  │
│       ↓                                                      │
│  read_testcases()      ← 加载所有种子（原始+enriched）        │
│       ↓                                                      │
│  perform_dry_run()     ← 干运行校准                          │
└───────────────────────────────┬─────────────────────────────┘
                                ↓
┌─────────────────────────────────────────────────────────────┐
│                      主循环                                  │
├─────────────────────────────────────────────────────────────┤
│  choose_target_state() → cull_queue() → choose_seed()        │
│       ↓                                                      │
│  fuzz_one():                                                 │
│    ├── 确定性变异阶段（bitflip/arithmetic/interest/dict）     │
│    ├── Havoc 阶段：                                          │
│    │     ├── 50% 探索：随机变异整个缓冲区                    │
│    │     └── 50% 利用：语法引导变异（只变异可变字段）         │
│    ├── 停滞检测：连续512次无趣 → LLM 生成新消息              │
│    └── Splicing 阶段（最后手段）                             │
│       ↓                                                      │
│  save_if_interesting() → 覆盖率反馈 → 更新种子队列           │
└─────────────────────────────────────────────────────────────┘
```

---

## 三、LLM 三大策略的详细流程

### 策略 1：语法模板提取（`setup_llm_grammars()`，line 434）

#### 步骤 1：构建第一次提示词

`construct_prompt_for_templates()`（chat-llm.c:168）：

**提示词内容：**
```
For the RTSP protocol, the DESCRIBE client request template is:
DESCRIBE: ["DESCRIBE <<VALUE>>\r\n", "CSeq: <<VALUE>>\r\n",
           "User-Agent: <<VALUE>>\r\n", "Accept: <<VALUE>>\r\n", "\r\n"]

For the HTTP protocol, the GET client request template is:
GET: ["GET <<VALUE>>\r\n"]

For the [RTSP] protocol, all of client request templates are:
```

**格式说明：** `<<VALUE>>` 标记表示可变字段，`\r\n` 表示协议行尾。

**调用模型：** `gpt-3.5-turbo`（chat completions API），temperature=0.5

#### 步骤 2：多轮对话 — 追问剩余模板

`construct_prompt_for_remaining_templates()`（chat-llm.c:197）：

将第一次的回答作为上下文，构建多轮对话：

```json
[
  {"role": "system", "content": "You are a helpful assistant."},
  {"role": "user", "content": "第一次的问题"},
  {"role": "assistant", "content": "第一次的回答"},
  {"role": "user", "content": "For the RTSP protocol, other templates of client requests are:"}
]
```

**目的：** 让 LLM 补充第一次未提到的消息类型。

#### 步骤 3：自一致性检查 — 重复 5 次

整个步骤 1-2 重复 `TEMPLATE_CONSISTENCY_COUNT=5` 次（afl-fuzz.c:443-513）。

每次迭代的结果存入 `consistency_table` 哈希表：
- 外层 key = 消息头（如 "DESCRIBE"）
- 内层 `field_table`：key = 字段模板，value = 出现次数

**过滤规则**（line 509）：只保留出现次数 > `TEMPLATE_CONSISTENCY_COUNT / 2`（即 >= 3 次）的字段。

```
迭代 1: DESCRIBE → [CSeq: <<VALUE>>, User-Agent: <<VALUE>>, Accept: <<VALUE>>]
迭代 2: DESCRIBE → [CSeq: <<VALUE>>, User-Agent: <<VALUE>>, Accept: <<VALUE>>]
迭代 3: DESCRIBE → [CSeq: <<VALUE>>, User-Agent: <<VALUE>>]
迭代 4: DESCRIBE → [CSeq: <<VALUE>>, User-Agent: <<VALUE>>, Accept: <<VALUE>>]
迭代 5: DESCRIBE → [CSeq: <<VALUE>>, User-Agent: <<VALUE>>, Accept: <<VALUE>>]

一致性结果：CSeq 出现5次 ✓，User-Agent 出现5次 ✓，Accept 出现4次 ✓
```

#### 步骤 4：编译为 PCRE2 正则表达式

`extract_message_pattern()`（chat-llm.c:461）：

将模板转换为两个正则模式：
- **patterns[0]（头部模式）：** `^(?:DESCRIBE (.*))\r\n`
- **patterns[1]（字段模式）：** `(?|(?:CSeq: (.*))\r\n)|(?:User-Agent: (.*))\r\n)|(?:Accept: (.*))\r\n)`

这些模式存入 `protocol_patterns` 链表，消息类型名存入 `message_types_set`。

**输出文件：**
- `protocol-grammars/llm-grammar-output-{0..4}` — 5 次 LLM 原始回答
- `protocol-grammars/pattern-{N}` — 编译后的正则模式

#### 语法提取流程图

```
┌──────────────────────────────────────────────────────────────┐
│                   setup_llm_grammars()                        │
├──────────────────────────────────────────────────────────────┤
│  构建提示词（含 RTSP/HTTP 示例）                              │
│       ↓                                                      │
│  for iter in 0..4 (共5次):                                   │
│    ├── LLM 调用1: "所有客户端请求模板是？"                    │
│    │     → 返回第一批模板（如 DESCRIBE, SETUP）               │
│    ├── LLM 调用2: "其他模板是？"（多轮对话）                  │
│    │     → 返回剩余模板（如 PLAY, TEARDOWN, OPTIONS）         │
│    ├── 合并两次回答                                           │
│    ├── extract_message_grammars() 提取 JSON 数组              │
│    └── 更新 consistency_table（字段出现次数统计）             │
│       ↓                                                      │
│  过滤：只保留出现 >= 3 次的字段                               │
│       ↓                                                      │
│  for 每个消息类型:                                            │
│    ├── extract_message_pattern() 编译为 PCRE2 正则            │
│    ├── 存入 protocol_patterns 链表                            │
│    └── 消息类型名存入 message_types_set                       │
└──────────────────────────────────────────────────────────────┘
```

---

### 策略 2：种子丰富（`enrich_testcases()`，line 2771）

#### 步骤 1：识别缺失的消息类型（line 2668-2706）

对输入目录中的每个种子文件：
1. 调用协议特定的 `extract_requests()` 解析种子为消息区域
2. 提取每个区域的消息头（如 "DESCRIBE"、"SETUP"）
3. 与 `message_types_set` 对比，找出缺失的消息类型

```
种子文件内容：
  DESCRIBE rtsp://127.0.0.1:8554/aacAudioTest RTSP/1.0\r\n
  CSeq: 2\r\n
  \r\n

已包含消息类型: {DESCRIBE}
所有消息类型:    {DESCRIBE, SETUP, PLAY, TEARDOWN, OPTIONS}
缺失消息类型:    {SETUP, PLAY, TEARDOWN, OPTIONS}
```

#### 步骤 2：生成组合（line 2717）

从缺失类型中选取最多 `MAX_ENRICHMENT_MESSAGE_TYPES=2` 个的组合：
```
组合: {SETUP, PLAY}, {SETUP, TEARDOWN}, {SETUP, OPTIONS}, {PLAY, TEARDOWN}, ...
```

如果缺失类型超过 `MAX_ENRICHMENT_CORPUS_SIZE=10` 个，随机删除到 10 个。

#### 步骤 3：LLM 丰富（`enrich_sequence()`，chat-llm.c:916）

**提示词模板：**
```
The following is one sequence of client requests:
[原始种子内容]
Please add the SETUP, PLAY client requests in the proper locations,
and the modified sequence of client requests is:
```

**调用模型：** `gpt-3.5-turbo-instruct`（completions API），temperature=0.5，最多重试 5 次。

#### 步骤 4：后处理（line 2729-2758）

1. 反转义 LLM 响应（`\n` → 真实换行）
2. 与原始种子比较，如果相同则跳过
3. 调用 `format_request_message()` 确保 `\r\n\r\n` 结尾
4. 写入新文件 `enriched_{i}_{原始文件名}`

#### 种子丰富流程图

```
┌──────────────────────────────────────────────────────────────┐
│                   enrich_testcases()                          │
├──────────────────────────────────────────────────────────────┤
│  for 每个种子文件:                                            │
│    ├── extract_requests() 解析为消息区域                      │
│    ├── 提取消息头，与 message_types_set 对比                  │
│    ├── 找出缺失的消息类型                                     │
│    │     ↓                                                    │
│    │  如果缺失类型数 > MAX_ENRICHMENT_CORPUS_SIZE(10):        │
│    │    随机删除到 10 个                                      │
│    │     ↓                                                    │
│    │  生成 C(缺失数, 2) 的组合                                │
│    │     ↓                                                    │
│    │  for 每个组合:                                           │
│    │    ├── enrich_sequence() → LLM 插入缺失消息              │
│    │    ├── 反转义 + 格式化                                   │
│    │    ├── 与原始比较，相同则跳过                            │
│    │    └── 写入 enriched_{i}_{原文件名}                      │
└──────────────────────────────────────────────────────────────┘
```

---

### 策略 3：覆盖停滞突破（`fuzz_one()` 中，line 6846）

#### 触发条件

```c
if (uninteresting_times >= UNINTERESTING_THRESHOLD (512)
    && chat_times < CHATTING_THRESHOLD (64))
```

- `uninteresting_times`：每次 `save_if_interesting()` 返回 false 时 +1，发现新覆盖时归零
- `chat_times`：LLM 交互次数，上限 64 次

#### 提示词构建（line 6850-6947）

1. 读取当前种子的服务器响应历史（从 `responses-ipsm/id:{filename}`）
2. 构建请求-响应对的通信历史
3. 将不可打印字符替换为空格，JSON 转义
4. 截断到 `HISTORY_PROMPT_LENGTH=1300` 字符

**提示词模板**（`construct_prompt_stall()`，chat-llm.c:150）：
```json
[{"role": "system", "content": "You are a helpful assistant."},
 {"role": "user", "content": "In the RTSP protocol, the communication history
  between the RTSP client and the RTSP server is as follows. The next proper
  client request that can affect the server's state are:\n\n
  Desired format of real client requests:\n
  Request-1:\n[示例请求]\nRequest-2:\n[示例请求]\n
  Communication History:\n\"\"\"\n[历史记录]\n\"\"\""}]
```

**调用模型：** `gpt-3.5-turbo`，temperature=1.5（高随机性），最多重试 2 次。

#### 响应处理（line 6976-7011）

1. `extract_stalled_message()`：用正则 `\r?\n?.*?\r?\n` 跳过空行，提取消息内容
2. `format_request_message()`：确保 `\n` 前有 `\r`，追加 `\r\n\r\n`
3. 直接通过 `common_fuzz_stuff()` 发送给目标服务器

**日志保存：**
- `stall-interactions/prompt-{N}` — 提示词
- `stall-interactions/response-{N}` — LLM 响应

#### 停滞突破流程图

```
┌──────────────────────────────────────────────────────────────┐
│              覆盖停滞突破（在 fuzz_one() 中）                  │
├──────────────────────────────────────────────────────────────┤
│  uninteresting_times >= 512 && chat_times < 64 ?             │
│       ↓ 是                                                   │
│  读取 responses-ipsm/id:{filename} 获取通信历史              │
│       ↓                                                      │
│  构建请求-响应对历史（JSON 转义，截断到 1300 字符）           │
│       ↓                                                      │
│  构建提示词：通信历史 + 示例请求格式                          │
│       ↓                                                      │
│  LLM 调用（gpt-3.5-turbo, temp=1.5, 重试2次）               │
│       ↓                                                      │
│  extract_stalled_message() 提取消息                          │
│       ↓                                                      │
│  format_request_message() 格式化（确保 \r\n\r\n 结尾）       │
│       ↓                                                      │
│  common_fuzz_stuff() 直接发送给目标服务器                    │
└──────────────────────────────────────────────────────────────┘
```

---

## 四、具体示例：RTSP 协议的完整流程

假设目标是 Live555 RTSP 服务器，种子文件包含一个 DESCRIBE 请求。

### 4.1 语法提取过程

**第 1 轮 LLM 调用（5 次中的第 1 次）：**

**第一次提问返回：**
```
DESCRIBE: ["DESCRIBE <<VALUE>>\r\n", "CSeq: <<VALUE>>\r\n",
           "User-Agent: <<VALUE>>\r\n", "Accept: <<VALUE>>\r\n", "\r\n"]
SETUP: ["SETUP <<VALUE>>\r\n", "CSeq: <<VALUE>>\r\n",
        "Transport: <<VALUE>>\r\n", "\r\n"]
```

**第二次追问返回：**
```
PLAY: ["PLAY <<VALUE>>\r\n", "CSeq: <<VALUE>>\r\n",
       "Session: <<VALUE>>\r\n", "Range: <<VALUE>>\r\n", "\r\n"]
TEARDOWN: ["TEARDOWN <<VALUE>>\r\n", "CSeq: <<VALUE>>\r\n",
           "Session: <<VALUE>>\r\n", "\r\n"]
OPTIONS: ["OPTIONS <<VALUE>>\r\n", "CSeq: <<VALUE>>\r\n",
          "User-Agent: <<VALUE>>\r\n", "\r\n"]
```

**5 轮自一致性检查后，编译为正则：**
```
DESCRIBE:
  头部: ^(?:DESCRIBE (.*))\r\n
  字段: (?|(?:CSeq: (.*))\r\n|(?:User-Agent: (.*))\r\n|(?:Accept: (.*))\r\n)

SETUP:
  头部: ^(?:SETUP (.*))\r\n
  字段: (?|(?:CSeq: (.*))\r\n|(?:Transport: (.*))\r\n)

PLAY:
  头部: ^(?:PLAY (.*))\r\n
  字段: (?|(?:CSeq: (.*))\r\n|(?:Session: (.*))\r\n|(?:Range: (.*))\r\n)

TEARDOWN:
  头部: ^(?:TEARDOWN (.*))\r\n
  字段: (?|(?:CSeq: (.*))\r\n|(?:Session: (.*))\r\n)

OPTIONS:
  头部: ^(?:OPTIONS (.*))\r\n
  字段: (?|(?:CSeq: (.*))\r\n|(?:User-Agent: (.*))\r\n)
```

### 4.2 种子丰富过程

**原始种子：**
```
DESCRIBE rtsp://127.0.0.1:8554/aacAudioTest RTSP/1.0\r\n
CSeq: 2\r\n
\r\n
```

**缺失类型：** {SETUP, PLAY, TEARDOWN, OPTIONS}

**LLM 丰富后的种子（enriched_0_seed）：**
```
DESCRIBE rtsp://127.0.0.1:8554/aacAudioTest RTSP/1.0\r\n
CSeq: 2\r\n
\r\n
SETUP rtsp://127.0.0.1:8554/aacAudioTest/track1 RTSP/1.0\r\n
CSeq: 3\r\n
Transport: RTP/AVP;unicast;client_port=38784-38785\r\n
\r\n
```

### 4.3 变异过程

**Havoc 阶段的 epsilon-greedy 决策：**

```c
double epsilon = UR(100) / 100.0;  // 随机 0.0-1.0
int is_exploration = epsilon < 0.5;
```

**探索模式（50%概率）：** 整个缓冲区作为一个可变范围，标准 AFL 随机变异。

**利用模式（50%概率）：** `parse_buffer()` 用正则分解缓冲区：
```
输入: "DESCRIBE rtsp://127.0.0.1:8554/aacAudioTest RTSP/1.0\r\nCSeq: 2\r\n\r\n"

匹配头部模式后，识别出可变范围：
  范围1: start=9,  len=36  (URL: "rtsp://127.0.0.1:8554/aacAudioTest")
  范围2: start=51, len=1   (CSeq 值: "2")

变异操作只在这些可变范围内进行，保护协议结构不被破坏。
```

### 4.4 停滞突破过程

假设模糊测试已运行一段时间，连续 512 次变异都未发现新覆盖。

**通信历史：**
```
Request: DESCRIBE rtsp://127.0.0.1:8554/aacAudioTest RTSP/1.0\r\nCSeq: 2\r\n\r\n
Response: RTSP/1.0 200 OK\r\nContent-Type: application/sdp\r\n...
```

**LLM 生成的新消息：**
```
SETUP rtsp://127.0.0.1:8554/aacAudioTest/track1 RTSP/1.0\r\n
CSeq: 3\r\n
Transport: RTP/AVP;unicast;client_port=38784-38785\r\n
\r\n
```

这个新消息直接发送给服务器，可能触发新的状态转换和代码路径。

---

## 五、测试策略选取详解

### 5.1 种子选择策略

通过 `-s` 参数配置，三种算法：

| 算法 | 值 | 行为 |
|------|---|------|
| RANDOM_SELECTION | 1 | 随机选择 |
| ROUND_ROBIN | 2 | 轮询 |
| FAVOR | 3 | 优先选择覆盖更多路径的种子 |

**FAVOR 模式的评分公式：**
```c
score = ceil(1000 * pow(2, -log10(log10(fuzzs+1) * selected_times + 1))
            * pow(2, log(paths_discovered + 1)))
```
- `fuzzs` 越少 → 分数越高（鼓励探索未充分测试的状态）
- `paths_discovered` 越多 → 分数越高（奖励能发现新路径的状态）

**状态选择策略**（通过 `-q` 参数配置）：
- RANDOM_SELECTION：随机选择状态
- ROUND_ROBIN：轮询（默认）
- FAVOR：优先选择分数高的状态

### 5.2 变异策略选择

**确定性阶段（按顺序执行）：**
1. Bitflip 1/1, 2/1, 4/1, 8/1, 16/1, 32/1 — 位翻转
2. Arithmetic 8/8, 16/8, 32/8 — 整数加减（ARITH_MAX=35）
3. Interest 8/8, 16/8, 32/8 — 替换为特殊值
   - 8-bit: {-128, -1, 0, 1, 16, 32, 64, 100, 127}
   - 16-bit: {-32768, 128, 255, 256, 512, 1000, 1024, 4096, 32767}
   - 32-bit: 类似的大值集合
4. Dictionary（用户提供的 token）— 覆盖和插入
5. Auto extras（自动检测的 token）— 覆盖

**Havoc 阶段（随机堆叠变异）：**
- 基础迭代次数：`HAVOC_CYCLES=256`，乘以 `perf_score` 系数
- 最大堆叠深度：`2^7=128` 次变异叠加
- 变异类型（15 种基础 + 8 种区域级）：
  - case 0: 单 bit 翻转
  - case 1: 设置字节为 interesting 值
  - case 2: 设置 word 为 interesting 值
  - case 3: 设置 dword 为 interesting 值
  - case 4: 随机减法
  - case 5: 随机加法
  - case 6: 随机设置字节
  - case 7: 删除字节
  - case 8: 克隆字节块
  - case 9: 用随机字节覆盖
  - case 10: 用字典 token 覆盖
  - case 11: 用字典 token 插入
  - case 12-14: 大块操作
  - case 17-18: 替换为其他种子的区域
  - case 19-20: 在当前区域前插入
  - case 21-22: 在当前区域后插入
  - case 23-24: 复制当前区域

**Splicing 阶段（最后手段）：**
- 从队列中随机选一个种子，找到差异字节，拼接后回到 havoc
- 最多尝试 `SPLICE_CYCLES=15` 次

### 5.3 LLM 解决的问题

| 传统 AFL/AFLNet 的问题 | ChatAFL 的 LLM 解决方案 |
|------------------------|------------------------|
| 不了解协议语法，盲目变异会破坏消息结构 | 语法提取 → 正则引导的结构感知变异 |
| 初始种子缺少关键消息类型，难以到达深层状态 | 种子丰富 → LLM 自动补充缺失消息 |
| 覆盖停滞时无法突破状态瓶颈 | 停滞突破 → LLM 根据通信历史生成新消息 |

### 5.4 能量调度（`calculate_score()`，line 6363）

每个种子的 `perf_score` 由四个因素决定：

| 因素 | 范围 | 逻辑 |
|------|------|------|
| 执行速度 | 10-300 | 比全局平均快 4 倍 → 300 分；慢 10 倍 → 10 分 |
| Bitmap 大小 | 0.25x-3x | 覆盖率高 → 3 倍权重 |
| Handicap | 2x-4x | 新发现的种子获得额外能量追赶 |
| 深度 | 1x-5x | 深层种子（depth>=26）获得 5 倍能量 |

**上限：** `HAVOC_MAX_MULT * 100 = 1600`

**反馈机制：** havoc 阶段如果发现新路径，`stage_max` 翻倍（最高 16 倍），形成正反馈循环。

**能量计算公式：**
```c
stage_max = (doing_det ? HAVOC_CYCLES_INIT : HAVOC_CYCLES) * perf_score / havoc_div / 100;
```
其中 `HAVOC_CYCLES=256`，`HAVOC_CYCLES_INIT=1024`，`havoc_div` 初始为 1。

---

## 六、策略变异的参数与报告

### 6.1 配置参数

ChatAFL 的变异策略通过以下参数控制：

| 参数 | 值 | 位置 | 含义 |
|------|---|------|------|
| `EPSILON_CHOICE` | 0.5 | config.h:75 | 探索 vs 利用的概率阈值 |
| `UNINTERESTING_THRESHOLD` | 512 | config.h:76 | 触发 LLM 停滞突破的连续无趣次数 |
| `CHATTING_THRESHOLD` | 64 | config.h:77 | 最大 LLM 停滞突破交互次数 |
| `TEMPLATE_CONSISTENCY_COUNT` | 5 | chat-llm.h:22 | 语法提取的自一致性迭代次数 |
| `MAX_ENRICHMENT_MESSAGE_TYPES` | 2 | chat-llm.h:37 | 种子丰富时最多添加的消息类型数 |
| `MAX_ENRICHMENT_CORPUS_SIZE` | 10 | chat-llm.h:40 | 种子丰富时最多考察的缺失类型数 |
| `HISTORY_PROMPT_LENGTH` | 1300 | chat-llm.h:19 | 停滞提示词中通信历史的最大 token 数 |
| `EXAMPLES_PROMPT_LENGTH` | 400 | chat-llm.h:18 | 停滞提示词中示例请求的最大 token 数 |
| `EXAMPLE_SEQUENCE_PROMPT_LENGTH` | 1700 | chat-llm.h:20 | 种子丰富中示例序列的最大 token 数 |
| `MAX_TOKENS` | 2048 | chat-llm.c:16 | LLM 响应的最大 token 数 |
| `HAVOC_CYCLES` | 256 | config.h:96 | 基础 havoc 迭代次数 |
| `HAVOC_CYCLES_INIT` | 1024 | config.h:97 | 初始 havoc 迭代次数 |
| `HAVOC_MAX_MULT` | 16 | config.h:101 | havoc 最大乘数 |
| `HAVOC_MIN` | 16 | config.h:105 | 最小 havoc 迭代次数 |
| `HAVOC_STACK_POW2` | 7 | config.h:108 | 最大堆叠深度（2^7=128） |
| `SPLICE_CYCLES` | 15 | config.h:115 | splicing 尝试次数 |
| `ARITH_MAX` | 35 | config.h:130 | 算术变异最大值 |
| `SKIP_TO_NEW_PROB` | 99 | config.h:141 | 跳过已 fuzz 种子的概率 |
| `SKIP_NFAV_OLD_PROB` | 95 | config.h:147 | 跳过非 favored 旧种子的概率 |
| `SKIP_NFAV_NEW_PROB` | 75 | config.h:153 | 跳过非 favored 新种子的概率 |

### 6.2 报告与日志

**运行时输出目录结构：**
```
out_dir/
├── protocol-grammars/
│   ├── llm-grammar-output-0    # 第1次 LLM 语法回答
│   ├── llm-grammar-output-1    # 第2次 LLM 语法回答
│   ├── ...
│   ├── llm-grammar-output-4    # 第5次 LLM 语法回答
│   ├── pattern-0               # 编译后的正则模式（头部+字段）
│   ├── pattern-1
│   └── ...
├── stall-interactions/
│   ├── prompt-1                # 第1次停滞突破的提示词
│   ├── response-1              # 第1次停滞突破的 LLM 响应
│   ├── prompt-2
│   ├── response-2
│   └── ...
├── responses-ipsm/
│   └── id:{seed_filename}     # 每个种子的服务器响应历史
├── queue/                      # 种子队列
├── crashes/                    # 发现的崩溃
├── hangs/                      # 发现的挂起
├── plot_data                   # 覆盖率随时间变化数据
├── fuzzer_stats                # 模糊测试统计信息
└── ipsm.dot                    # 已实现状态机的 Graphviz 图
```

**fuzzer_stats 文件包含：**
- `execs_done` — 总执行次数
- `execs_per_sec` — 每秒执行次数
- `paths_total` — 总路径数
- `paths_found` — 新发现路径数
- `paths_imported` — 导入路径数
- `unique_crashes` — 唯一崩溃数
- `unique_hangs` — 唯一挂起数
- `levels` — 最大深度
- `variable_paths` — 变量行为路径数

**plot_data 文件格式（TSV）：**
```
# unix_time, cycles_done, paths_total, paths_not_fuzzed, favored_total, ...
1620000000, 0, 5, 5, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
```

---

## 七、LLM 通信机制详解

### 7.1 API 调用（`chat_with_llm()`，chat-llm.c:45）

**模型选择：**
- `"instruct"` → `gpt-3.5-turbo-instruct`（completions API）
  - 端点：`https://api.openai.com/v1/completions`
  - 请求体：`{"model": "gpt-3.5-turbo-instruct", "prompt": "...", "max_tokens": 2048, "temperature": ...}`
  - 响应解析：`choices[0].text`

- `"turbo"` → `gpt-3.5-turbo`（chat completions API）
  - 端点：`https://api.openai.com/v1/chat/completions`
  - 请求体：`{"model": "gpt-3.5-turbo", "messages": [...], "max_tokens": 2048, "temperature": ...}`
  - 响应解析：`choices[0].message.content`

**认证：** Bearer token，通过 `setup.sh` 在构建时注入 `OPENAI_TOKEN` 宏。

**重试机制：** 失败时重试指定次数，API 错误时 sleep(2)。

### 7.2 各策略的 LLM 调用参数

| 策略 | 模型 | Temperature | 重试次数 | 调用时机 |
|------|------|------------|---------|---------|
| 语法提取（第一次） | turbo | 0.5 | 5 | 启动时 |
| 语法提取（第二次） | turbo | 0.5 | 5 | 启动时 |
| 消息类型提取 | instruct | 0.5 | 5 | 启动时 |
| 种子丰富 | instruct | 0.5 | 5 | 启动时 |
| 停滞突破 | turbo | 1.5 | 2 | 运行时（连续512次无趣后） |

### 7.3 Token 预算管理

```
总预算: 2048 tokens（MAX_PROMPT_LENGTH）

语法提取: 提示词 + 示例 + 协议名
停滞突破: 示例(400) + 历史(1300) + 模板开销(~270) + 响应(2048)
种子丰富: 示例序列(1700) + 模板开销 + 响应(2048)
```

---

## 八、协议支持与基准测试

### 8.1 支持的协议

`aflnet.c` 中实现了 11 种协议的请求/响应解析：

| 协议 | 提取函数 | 响应码函数 |
|------|---------|-----------|
| SMTP | `extract_requests_smtp` | `extract_response_codes_smtp` |
| SSH | `extract_requests_ssh` | `extract_response_codes_ssh` |
| TLS | `extract_requests_tls` | `extract_response_codes_tls` |
| DICOM | `extract_requests_dicom` | `extract_response_codes_dicom` |
| DNS | `extract_requests_dns` | `extract_response_codes_dns` |
| FTP | `extract_requests_ftp` | `extract_response_codes_ftp` |
| RTSP | `extract_requests_rtsp` | `extract_response_codes_rtsp` |
| DTLS1.2 | `extract_requests_dtls12` | `extract_response_codes_dtls12` |
| SIP | `extract_requests_sip` | `extract_response_codes_sip` |
| HTTP | `extract_requests_http` | `extract_response_codes_http` |
| IPP | `extract_requests_ipp` | `extract_response_codes_ipp` |

### 8.2 基准测试对象

```
benchmark/subjects/
├── FTP/
│   ├── BFTPD/
│   ├── LightFTP/
│   ├── ProFTPD/
│   └── PureFTPD/
├── SMTP/
│   └── Exim/
├── RTSP/
│   └── Live555/
├── SIP/
│   └── Kamailio/
├── DAAP/
│   └── forked-daapd/
└── HTTP/
    └── Lighttpd1/
```

### 8.3 运行方式

```bash
# 构建 Docker 镜像
./setup.sh

# 运行实验
# 用法: run.sh <容器数> <时间(分钟)> <目标> <模糊器>
./run.sh 1 60 RTSP/Live555 ChatAFL

# 分析结果
./analyze.sh
```

---

## 九、三个变体的详细对比

| 特性 | ChatAFL（完整版） | ChatAFL-CL1 | ChatAFL-CL2 |
|------|------------------|-------------|-------------|
| 语法提取 (`setup_llm_grammars`) | 有 | 有 | 有 |
| 种子丰富 (`enrich_testcases`) | 有 | **注释掉** | 有 |
| 停滞突破 | 有 | **禁用** (`CHATTING_THRESHOLD=0`) | **禁用** (`CHATTING_THRESHOLD=0`) |
| 结构感知变异 | 有 | 有 | 有 |
| LLM 调用次数（启动时） | 5*2+种子丰富 | 5*2 | 5*2+种子丰富 |
| LLM 调用次数（运行时） | 最多 64 次 | 0 | 0 |

`chat-llm.c` 和 `chat-llm.h` 在三个变体中完全相同，差异仅在 `afl-fuzz.c` 和 `config.h` 中。

**CL1 的代码差异（afl-fuzz.c）：**
```c
// setup_llm_grammars();  // 保留
// enrich_testcases();    // 注释掉
```

**CL2 的代码差异（config.h）：**
```c
#define CHATTING_THRESHOLD  0  // 禁用停滞突破
```

---

## 十、关键技术细节

### 10.1 `parse_buffer()` 工作原理（line 550-596）

```c
range_list parse_buffer(char *buf, size_t buf_len) {
    // 遍历所有编译的正则模式
    for (每个 protocol_patterns 中的模式) {
        // 尝试用头部模式匹配缓冲区开头
        if (starts_with(buf, buf_len, header_pattern) 匹配) {
            // 用字段模式找出所有可变范围
            dyn_ranges = get_mutable_ranges(buf, buf_len, header_match.len, fields_pattern);
            return 合并(header_groups, dyn_ranges);
        }
    }
    // 优雅降级：如果没有模式匹配，整个缓冲区都是可变的
    return [{start=0, len=buf_len, mutable=1}];
}
```

### 10.2 覆盖率反馈机制

**共享内存 bitmap：** 64KB（`MAP_SIZE = 1<<16`）

**`has_new_bits()` 返回值：**
- 0：无新覆盖
- 1：已有边的新命中次数
- 2：全新边

**四个 virgin map：**
- `virgin_bits` — 从未见过的边
- `virgin_tmout` — 超时中从未见过的边
- `virgin_crash` — 崩溃中从未见过的边
- `session_virgin_bits` — 当前会话中未见的边（AFLNet 添加）

**`classify_counts()` 分桶：**
```
原始计数 → 分桶值
1        → 1
2        → 2
3        → 3
4-7      → 4
8-15     → 8
16-31    → 16
32-127   → 32
128+     → 128
```

### 10.3 状态感知覆盖

`update_state_aware_variables()`（line 1020）：
1. 从服务器响应中提取状态序列
2. 检查状态序列是否"有趣"（IPSM 上的新状态转换）
3. 如果有趣：保存可重放消息，更新 IPSM 图
4. 为每个区域标注其状态序列
5. 更新 `khms_states` 中每个状态的种子列表

`is_state_sequence_interesting()`（line 688）：
- 哈希修剪后的状态序列（去除连续重复 >2 次）
- 检查 `khs_ipsm_paths` 哈希集

---

## 十一、总结

ChatAFL 通过引入 LLM 解决了传统协议模糊测试的三个核心问题：

1. **语法盲区**：LLM 自动提取协议语法模板，编译为正则表达式，引导结构感知变异
2. **种子覆盖不足**：LLM 自动识别并补充缺失的消息类型，丰富种子语料库
3. **覆盖停滞**：当连续 512 次变异都无法发现新覆盖时，LLM 根据通信历史生成突破性消息

这三个策略相互配合：
- 语法提取为变异提供结构指导
- 种子丰富确保初始语料库覆盖所有消息类型
- 停滞突破在变异陷入局部最优时提供新方向

通过 epsilon-greedy 策略（50% 探索 + 50% 利用），ChatAFL 在保持 AFL 随机探索能力的同时，利用 LLM 的协议知识进行有针对性的变异，显著提高了协议模糊测试的效率和覆盖率。
