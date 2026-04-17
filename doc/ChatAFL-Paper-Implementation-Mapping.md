# ChatAFL 论文核心要点与代码实现对应文档

本文档将 ChatAFL 论文（NDSS 2024）中提出的三个核心策略与实际代码实现位置相对应，并说明判断大模型调用是否被采纳的逻辑。

---

## 1. 论文三大核心策略概览

| 策略 | 论文代号 | 功能 | 调用 LLM 时机 |
|------|----------|------|---------------|
| **S_A** | Grammar-guided Mutation | 结构感知变异 | Fuzzing 开始前提取语法 |
| **S_B** | Enriching Initial Seeds | 种子语料库富化 | Fuzzing 开始前富化初始种子 |
| **S_C** | Surpassing Coverage Plateau | 突破覆盖率 plateau | 遇到连续无趣样本时 |

---

## 2. 策略 S_A：Grammar-guided Mutation（语法引导变异）

### 2.1 论文描述

在 fuzzing 开始前，向 LLM 询问协议的消息格式语法，用于**结构感知的变异**。LLM 已经学习了 RFC 文档，能够生成机器可读的语法。

### 2.2 代码实现位置

| 功能 | 函数名 | 文件位置 |
|------|--------|----------|
| **主入口** | `setup_llm_grammars()` | [afl-fuzz.c:434](ChatAFL/afl-fuzz.c#L434) |
| **构建语法 Prompt** | `construct_prompt_for_templates()` | [chat-llm.c:168](ChatAFL/chat-llm.c#L168) |
| **构建后续语法 Prompt** | `construct_prompt_for_remaining_templates()` | [chat-llm.c:197](ChatAFL/chat-llm.c#L197) |
| **调用 LLM** | `chat_with_llm()` | [chat-llm.c:45](ChatAFL/chat-llm.c#L45) |
| **解析语法 JSON** | `extract_message_grammars()` | [chat-llm.c:379](ChatAFL/chat-llm.c#L379) |
| **生成正则模式** | `extract_message_pattern()` | [chat-llm.c:461](ChatAFL/chat-llm.c#L461) |

### 2.3 调用时机

```
afl-fuzz.c:main()
  → setup_llm_grammars()  [line 10686]  ← Fuzzing 开始前调用一次
```

### 2.4 LLM 响应判断

- **成功**：响应包含 JSON 格式的语法，被 `extract_message_grammars()` 成功解析
- **失败**：解析失败或返回空，继续使用随机变异

---

## 3. 策略 S_B：Enriching Initial Seeds（种子语料库富化）

### 3.1 论文描述

利用 LLM 生成缺失的消息类型，并将其插入到初始种子序列的正确位置。解决初始种子覆盖不足的问题。

### 3.2 代码实现位置

| 功能 | 函数名 | 文件位置 |
|------|--------|----------|
| **主入口** | `get_seeds_with_messsage_types()` | [afl-fuzz.c:2625](ChatAFL/afl-fuzz.c#L2625) |
| **核心富化函数** | `enrich_sequence()` | [chat-llm.c:938](ChatAFL/chat-llm.c#L938) |
| **调用 LLM** | `chat_with_llm()` | [chat-llm.c:45](ChatAFL/chat-llm.c#L45) |
| **富化入口** | `enrich_testcases()` | [afl-fuzz.c:2770](ChatAFL/afl-fuzz.c#L2770) |

### 3.3 调用时机

```
afl-fuzz.c:main()
  → enrich_testcases()  [line 2782]
    → get_seeds_with_messsage_types()  [line 2783]  ← Fuzzing 开始前调用
```

### 3.4 关键参数 (chat-llm.h)

```c
#define ENRICHMENT_RETRIES 5              // 富化重试次数
#define MAX_ENRICHMENT_MESSAGE_TYPES 2    // 每次最多添加的消息类型数
#define MAX_ENRICHMENT_CORPUS_SIZE 10      // 最多处理的种子文件数
```

### 3.5 LLM 响应判断

**代码中的判断逻辑 (afl-fuzz.c:2724-2755)：**

```c
char *client_request_answer = enrich_sequence(nl_file_content, subset);

if (client_request_answer == NULL)
    continue;  // LLM 返回空，跳过

// 检查是否与原种子相同
char *formatted_nl_file_content = format_string(nl_file_content);
char *unescaped_client_requests = unescape_string(client_request_answer);
char *formatted_unescaped_client_requests = format_string(unescaped_client_requests);

if (strcmp(formatted_unescaped_client_requests, formatted_nl_file_content) == 0) {
    printf("## Skip the same seed\n");
    continue;  // 相同则跳过，不写入
}

// 写入新种子文件
write_new_seeds(enriched_file_path, unescaped_client_requests);
```

---

## 4. 策略 S_C：Surpassing Coverage Plateau（突破覆盖率 Plateau）

### 4.1 论文描述

当 fuzzer 连续产生大量无趣（uninteresting）的样本，无法突破当前覆盖率时，利用 LLM 根据通信历史生成可能触发新状态转换的消息。

### 4.2 代码实现位置

| 功能 | 函数名 | 文件位置 |
|------|--------|----------|
| **判断是否进入 Plateau** | `fuzz_one()` 中的 stall 处理逻辑 | [afl-fuzz.c:6846](ChatAFL/afl-fuzz.c#L6846) |
| **构建 Stall Prompt** | `construct_prompt_stall()` | [chat-llm.c:150](ChatAFL/chat-llm.c#L150) |
| **调用 LLM** | `chat_with_llm()` | [chat-llm.c:45](ChatAFL/chat-llm.c#L45) |
| **提取生成的消息** | `extract_stalled_message()` | [chat-llm.c:225](ChatAFL/chat-llm.c#L225) |
| **格式化消息** | `format_request_message()` | [chat-llm.c:247](ChatAFL/chat-llm.c#L247) |

### 4.3 调用决策逻辑

#### 4.3.1 判断是否触发 LLM 调用

**判断条件 (afl-fuzz.c:6846)：**

```c
if (uninteresting_times >= UNINTERESTING_THRESHOLD && chat_times < CHATTING_THRESHOLD)
```

| 变量 | 定义位置 | 默认值 | 含义 |
|------|----------|--------|------|
| `uninteresting_times` | afl-fuzz.c:401 | 0 | 连续无趣样本计数 |
| `UNINTERESTING_THRESHOLD` | config.h:76 | **512** | 触发 LLM 的无趣样本阈值 |
| `chat_times` | afl-fuzz.c:403 | 0 | 已调用 LLM 的次数 |
| `CHATTING_THRESHOLD` | config.h:77 | **64** | 最大 LLM 调用次数限制 |

#### 4.3.2 流程图

```
┌─────────────────────────────────────────────────────────────┐
│  每产生一个测试用例 (seed)                                     │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
              ┌───────────────────────────────┐
              │ 该 seed 是否增加覆盖率？        │
              └───────────────────────────────┘
                      │           │
                     Yes          No
                      │           │
                      ▼           ▼
              ┌───────────┐   ┌──────────────────┐
              │uninteresting│   │ uninteresting_times │
              │   _times = 0│   │       ++          │
              └───────────┘   └──────────────────┘
                                      │
                                      ▼
                          ┌─────────────────────┐
                          │ uninteresting_times  │
                          │ >= UNINTERESTING_    │
                          │ THRESHOLD (512)      │
                          │ AND chat_times <     │
                          │ CHATTING_THRESHOLD(64)│
                          └─────────────────────┘
                                      │
                                     Yes
                                      │
                                      ▼
                          ┌─────────────────────┐
                          │   调用 LLM           │
                          │ construct_prompt_   │
                          │ stall()             │
                          │ chat_with_llm()     │
                          └─────────────────────┘
                                      │
                                      ▼
                          ┌─────────────────────┐
                          │ chat_times++         │
                          │ 保存 stall-         │
                          │ interactions/       │
                          └─────────────────────┘
```

#### 4.3.3 Stall Prompt 构建 (chat-llm.c:150)

```c
char *construct_prompt_stall(char *protocol_name, char *examples, char *history)
{
    char *template =
        "In the %s protocol, the communication history between the %s client "
        "and the %s server is as follows."
        "The next proper client request that can affect the server's state are:\n\n"
        "Desired format of real client requests:\n%s"
        "Communication History:\n\"\"\"\n%s\"\"\"";

    // ... 构建 prompt ...
}
```

#### 4.3.4 LLM 响应处理与采纳判断

**关键代码 (afl-fuzz.c:6976-7011)：**

```c
// 1. 从 LLM 响应中提取消息
char *stall_message = extract_stalled_message(stall_response, strlen(stall_response));

if (stall_message == NULL)
    goto free_stall;  // 提取失败，不采纳

// 2. 格式化消息（添加 \r\n 等）
stall_message = format_request_message(stall_message);

if (stall_message != NULL)
{
    // 3. 实际发送给服务器测试
    if (common_fuzz_stuff(argv, stall_message, strlen(stall_message)))
    {
        // 如果返回非0，表示出错了（如崩溃）
        // 仍标记为已处理
    }

    // 4. 无论是否增加覆盖率，都标记本次 LLM 调用完成
    // 因为 chat_times++ 已经在调用前执行
}
```

**采纳判断条件：**
- `extract_stalled_message()` 返回非 NULL（成功提取消息）
- `format_request_message()` 返回非 NULL（格式化成功）
- `common_fuzz_stuff()` 执行完成（无论结果如何）

**注意：LLM 生成的消息不进行覆盖率校验，直接采纳并测试。**

---

## 5. 论文参数与代码对照表

### 5.1 Plateau 检测参数

| 参数 | 论文参考值 | 代码定义位置 | 默认值 |
|------|-----------|-------------|--------|
| MaxPlateau | 512 | config.h:76 | `UNINTERESTING_THRESHOLD = 512` |
| MaxChatCalls | MaxPlateau/4 | config.h:77 | `CHATTING_THRESHOLD = 64` |

### 5.2 LLM 调用参数

| 参数 | 论文参考值 | 代码定义位置 | 默认值 |
|------|-----------|-------------|--------|
| Grammar 提取温度 | 0.5 | chat-llm.c:69 | 使用 `temperature` 参数 |
| Seed 富化温度 | 0.5 | chat-llm.c:968 | 使用 `temperature` 参数 |
| Plateau 突破温度 | 1.5 | afl-fuzz.c:6949 | 使用 `temperature = 1.5` |
| Grammar 重试次数 | 5 | chat-llm.h:28 | `GRAMMAR_RETRIES = 5` |
| Stall 重试次数 | 2 | chat-llm.h:25 | `STALL_RETRIES = 2` |

---

## 6. 论文 Algorithm 1 与代码对照

```
论文 Algorithm 1                    代码实现
─────────────────────────────────────────────────────────
Line 2: Grammar G ← CHATGRAMMAR    setup_llm_grammars() [afl-fuzz.c:434]
Line 3: C ← ENRICHCORPUS          enrich_testcases() [afl-fuzz.c:2770]
Line 4: PlateauLen ← 0            uninteresting_times [afl-fuzz.c:401]
Line 11: if PlateauLen < MaxPlateau  if (uninteresting_times >= UNINTERESTING_THRESHOLD ...) [afl-fuzz.c:6846]
Line 19-21: CHATNEXTMESSAGE        construct_prompt_stall() + chat_with_llm() [afl-fuzz.c:6947-6949]
Line 26: PlateauLen ← 0           uninteresting_times = 0 [afl-fuzz.c:6848]
Line 30: PlateauLen++              uninteresting_times++ [afl-fuzz.c:6301]
Line 32: PlateauLen++              (同 line 30)
```

---

## 7. 关键文件汇总

| 论文概念 | 代码文件 | 核心函数 |
|----------|----------|----------|
| 语法提取 | chat-llm.c | `construct_prompt_for_templates()`, `extract_message_grammars()` |
| 种子富化 | chat-llm.c | `enrich_sequence()` |
| Plateau 判断 | afl-fuzz.c | `fuzz_one()` 中的 stall 处理块 |
| LLM 调用 | chat-llm.c | `chat_with_llm()` |
| 状态机更新 | aflnet.c | `update_state_machine()` |

---

## 8. 验证 LLM 是否正常工作的方法

### 8.1 语法提取验证

```bash
# 查看 LLM 生成的语法文件
cat out-<subject>-<fuzzer>/protocol-grammars/*

# 成功标志：包含 JSON 格式的 {"CMD": ["...", "..."]} 格式
```

### 8.2 种子富化验证

```bash
# 查看富化的种子文件
cat out-<subject>-<fuzzer>/queue/enriched_*

# 成功标志：文件存在且包含协议命令序列
```

### 8.3 Stall 处理验证

```bash
# 查看 stall 交互日志
cat out-<subject>-<fuzzer>/stall-interactions/request-0
cat out-<subject>-<fuzzer>/stall-interactions/response-0

# 检查 chat_times 是否增加
grep "chat_times" out-<subject>-<fuzzer>/fuzzer_stats
```
