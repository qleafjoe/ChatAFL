# ChatAFL 论文与代码对照讲解

> 论文：*Large Language Model guided Protocol Fuzzing*，发表于 NDSS 2024  
> 对应代码：`ChatAFL/` 目录（以及消融实验版本 `ChatAFL-CL1/`、`ChatAFL-CL2/`）

---

## 目录

1. [研究背景与动机](#1-研究背景与动机)
2. [整体算法框架](#2-整体算法框架)
3. [策略一：文法提取与结构感知变异（SA）](#3-策略一文法提取与结构感知变异sa)
4. [策略二：初始种子丰富（SB）](#4-策略二初始种子丰富sb)
5. [策略三：覆盖率停滞突破（SC）](#5-策略三覆盖率停滞突破sc)
6. [LLM 通信层](#6-llm-通信层)
7. [AFLNet 状态机层](#7-aflnet-状态机层)
8. [消融实验版本对照](#8-消融实验版本对照)
9. [实验结果摘要](#9-实验结果摘要)

---

## 1. 研究背景与动机

### 论文阐述的三大挑战

协议模糊测试（Protocol Fuzzing）面临三个核心难点：

| 编号 | 挑战 | 描述 |
|---|---|---|
| C1 | **初始种子依赖** | 变异式模糊测试依赖人工录制的消息序列作为种子，难以覆盖协议的多样状态和输入结构 |
| C2 | **消息结构未知** | 没有机器可读的消息结构规范，无法生成特定类型的消息或对消息做结构性修改 |
| C3 | **状态空间未知** | 无法识别当前协议状态，也无法主动引导模糊器探索未见状态 |

### LLM 的能力验证（论文第 III 节案例研究）

论文以 RTSP 协议和 Live555 实现为例，验证了 LLM 三方面的能力：

- **文法质量**：在 50 次采样中，LLM 对 10 种 RTSP 消息类型中的 9 种给出了与 RFC 完全一致的文法，仅 PLAY 消息的 `Range` 字段在 15 次中被遗漏。
- **种子丰富性**：99% 的 LLM 生成消息被插入正确的位置；服务器接受率约 55%，其余拒绝原因是协议本身不支持某些命令或 Session ID 上下文缺失（非 LLM 能力问题）。
- **状态转换引导**：69%～89% 的 LLM 生成消息可成功触发状态转换，且覆盖了每个状态的全部转换路径。

---

## 2. 整体算法框架

### 论文算法 1（Algorithm 1）

论文给出了 LLM 引导协议模糊测试（LLMPF）的伪代码，灰色部分为 ChatAFL 在 AFLNet 基础上的新增逻辑：

```
输入: 协议实现 P₀, 协议名称 p, 初始种子集 C, 总时间 T
输出: 最终种子队列 C, 崩溃种子 Cₓ

1. Pf ← 插桩(P₀)
2. [NEW] Grammar G ← CHATGRAMMAR(p)          # 文法提取
3. [NEW] C ← C ∪ ENRICHCORPUS(C, p)          # 种子丰富
4. [NEW] PlateauLen ← 0
5. StateMachine S ← ∅
6. repeat:
7.   state s ← CHOOSESTATE(S)
8.   Messages M, Response R ← CHOOSESEQUENCE(C, s)
9.   ⟨M₁, M₂, M₃⟩ ← split(M)
10.  for i = 1 to ASSIGNENERGY(M) do:
11.    if PlateauLen < MaxPlateau then:
12.      if UNIFORM() < ε then:
13.        [NEW] M₂' ← GRAMMARMUTATE(M₂, G)   # 文法变异
14.      else:
15.        M₂' ← RANDMUTATE(M₂)
16.    else:
17.      [NEW] M₂' ← CHATNEXTMESSAGE(M₁, R)   # 停滞突破
18.      PlateauLen ← 0
19.    R' ← SENDTOSERVER(Pf, M')
20.    if CRASHES(M', Pf): Cₓ ∪= M'; PlateauLen ← 0
21.    elif INTERESTING(M', Pf, S): C ∪= (M', R'); S ← UPDATE(S, R'); PlateauLen ← 0
22.    else: PlateauLen += 1
23. until 超时
```

### 代码入口：`afl-fuzz.c` 启动序列

```c
// ChatAFL/afl-fuzz.c  ~第 10681 行
if (protocol_selected)
{
    protocol_patterns = kl_init(rang);      // 初始化文法模式列表
    message_types_set = kh_init(strSet);    // 初始化消息类型集合

    setup_llm_grammars();   // 对应论文 Algorithm 1 第 2 行
    enrich_testcases();     // 对应论文 Algorithm 1 第 3 行
}
read_testcases();           // 读取（含新增种子）并加入测试队列
```

这三个函数调用精确对应论文算法的前三步，且均在主模糊循环启动前完成。

---

## 3. 策略一：文法提取与结构感知变异（SA）

### 3.1 论文思想

**目标**：解决挑战 C2（消息结构未知）。  
使用 Few-Shot In-Context Learning 提示 LLM，以机器可读格式输出每种消息类型的文法模板，再用 PCRE2 正则表达式标记可变字段（`<<VALUE>>`），只对可变字段做变异，保留关键字不变。

**关键参数**：
- 变异概率 `ε = 0.5`（`EPSILON_CHOICE`）：以 50% 概率选择文法变异，50% 概率选择标准 AFL havoc 变异
- 文法重复采样 5 次（`TEMPLATE_CONSISTENCY_COUNT = 5`）：多数表决过滤随机错误答案

### 3.2 Prompt 设计

论文图 6 展示了文法提取的 Few-Shot Prompt 格式，代码中的实现位于 `chat-llm.c`：

```c
// ChatAFL/chat-llm.c  第 168 行
char *construct_prompt_for_templates(char *protocol_name, char **final_msg)
{
    // Few-Shot 示例 1：RTSP DESCRIBE 文法
    char *prompt_rtsp_example =
        "For the RTSP protocol, the DESCRIBE client request template is:\n"
        "DESCRIBE: [\"DESCRIBE <<VALUE>>\r\n\","
        "\"CSeq: <<VALUE>>\r\n\","
        "\"User-Agent: <<VALUE>>\r\n\","
        "\"Accept: <<VALUE>>\r\n\","
        "\"\r\n\"]";

    // Few-Shot 示例 2：HTTP GET 文法
    char *prompt_http_example =
        "For the HTTP protocol, the GET client request template is:\n"
        "GET: [\"GET <<VALUE>>\r\n\"]";

    // 最终问题：请给出目标协议的所有消息类型文法
    asprintf(&msg, "%s\n%s\nFor the %s protocol, all of client request templates are:",
             prompt_rtsp_example, prompt_http_example, protocol_name);
    ...
}
```

两个 Few-Shot 示例来自不同协议（RTSP 和 HTTP），目的是防止 LLM 只套用单一格式。

### 3.3 自洽性检查（Self-Consistency Check）

```c
// ChatAFL/afl-fuzz.c  第 443 行
for (int iter = 0; iter < TEMPLATE_CONSISTENCY_COUNT; iter++)  // 重复 5 次
{
    char *templates_answer = chat_with_llm(templates_prompt, "turbo", GRAMMAR_RETRIES, 0.5);
    // 也提问 "其余消息类型" 避免 LLM 漏掉部分类型
    char *remaining_prompt = construct_prompt_for_remaining_templates(...);
    char *remaining_templates = chat_with_llm(remaining_prompt, "turbo", ...);

    // 合并两次回答，解析出文法列表
    extract_message_grammars(combined_templates, grammar_list);

    // 统计每个字段出现次数（多数表决）
    kh_value(field_table, field_k)++;
}
```

5 次采样后，对每种消息类型只保留出现次数超过阈值（`TEMPLATE_CONSISTENCY_COUNT / 2 + 1`）的字段，过滤掉 LLM 的随机错误。

### 3.4 文法转正则表达式

```c
// ChatAFL/chat-llm.c  第 461 行
// 将 LLM 输出的文法模板（含 <<VALUE>> 占位符）编译为 PCRE2 正则表达式
// 生成两个 pattern：
// patterns[0] = (?:PLAY (.*)\r\n)          ← 匹配消息头
// patterns[1] = (?|(?:CSeq: (.*)\r\n)|...) ← 匹配各字段（可变区域）
char *extract_message_pattern(const char *header_str,
                              khash_t(field_table) *field_table,
                              pcre2_code **patterns, ...)
{
    pcre2_code *replacer = pcre2_compile(
        "(?:(.*)(?:<<(.*)>>)(.*))|(.+)",   // 匹配 <<VALUE>> 占位符
        PCRE2_ZERO_TERMINATED, PCRE2_DOTALL, ...);
    ...
}
```

每种消息类型对应两个 PCRE2 模式：`patterns[0]` 匹配消息头以识别类型，`patterns[1]` 匹配所有字段以定位可变区域。全部模式存入全局链表 `protocol_patterns`。

### 3.5 运行时变异：`parse_buffer`

```c
// ChatAFL/afl-fuzz.c  第 550 行
range_list parse_buffer(char *buf, size_t buf_len)
{
    // 遍历所有已知文法模式
    for (iter_rang = kl_begin(protocol_patterns); ...; iter_rang = kl_next(iter_rang))
    {
        pcre2_code **patterns = kl_val(iter_rang);
        // 1. 用 patterns[0] 判断消息类型是否匹配
        range_list header_groups = starts_with(buf, buf_len, header_pattern);
        if (kv_size(header_groups) == 0) continue;

        // 2. 用 patterns[1] 定位所有可变字段的字节范围
        range_list dyn_ranges = get_mutable_ranges(..., fields_pattern);
        ...
    }
    // 若无匹配，退化为将整个缓冲区视为可变（保持对未知消息的兼容性）
    if (kv_size(best_decomposition) == 0) {
        range v = {.start = 0, .len = buf_len, .mutable = 1};
        ...
    }
}
```

AFL 的变异引擎在调用 `parse_buffer` 后，仅对 `mutable = 1` 的字节范围进行位翻转、字节替换等变异操作，从而保留消息关键字的合法性。

---

## 4. 策略二：初始种子丰富（SB）

### 4.1 论文思想

**目标**：解决挑战 C1（初始种子依赖）。  
LLM 可以识别种子中缺失的消息类型，并将其插入消息序列中合适的位置。论文案例中，ProfuzzBench 的 RTSP 种子仅包含 4 种消息类型（DESCRIBE/SETUP/PLAY/TEARDOWN），而 RFC 定义了 10 种，缺失的 6 种（如 PAUSE、ANNOUNCE 等）导致 Bug #1 等漏洞无法被 AFLNet/NSFuzz 发现。

**关键参数**：
- 每次最多添加 2 种缺失类型（`MAX_ENRICHMENT_MESSAGE_TYPES = 2`）
- 最多检查 10 个种子文件（`MAX_ENRICHMENT_CORPUS_SIZE = 10`）

### 4.2 Prompt 设计

论文图 8 展示了种子丰富的 Prompt，代码对应 `chat-llm.c` 中的 `enrich_sequence`：

```
Prompt（自动生成）：
  For the RTSP protocol, the following is one sequence of client requests:
  DESCRIBE rtsp://...
  SETUP rtsp://...
  PLAY rtsp://...

  Please add the SET_PARAMETER and TEARDOWN client requests in the
  accurate locations, and the modified sequence of client request is:

LLM Output：
  DESCRIBE rtsp://...
  SETUP rtsp://...
  PLAY rtsp://...
  SET_PARAMETER rtsp://...
  TEARDOWN rtsp://...
```

Prompt 采用"续写格式"（continuation format），直接要求 LLM 输出完整修改后的序列，而不是描述如何修改，这样输出可以直接用作种子文件。

### 4.3 代码实现

**第一步：识别缺失消息类型**

```c
// ChatAFL/afl-fuzz.c  第 2625 行
void get_seeds_with_messsage_types(const char *in_dir, khash_t(strSet) *message_types_set)
{
    // 扫描 in_dir 下的所有原始种子（跳过已命名为 "enriched" 的文件）
    for (int i = 0; i < nl_cnt; i++)
    {
        if (strstr(nl_file_name, "enriched") != NULL) continue;

        // 读取种子文件内容
        region_t *regions = (*extract_requests)(nl_file_content, fsize, &region_count);

        // 复制消息类型全集，逐条删去已存在的类型，剩余即为 MissingTypes
        khash_t(strSet) *messages = duplicate_hash(message_types_set);
        for (int j = 0; j < region_count; j++) {
            // 解析每条请求的消息类型（取第一个词）
            ...
            kh_del(strSet, messages, ...);  // 从缺失集合中移除已观察到的类型
        }
```

**第二步：枚举缺失类型子集并调用 LLM**

```c
        // 生成所有大小为 MAX_ENRICHMENT_MESSAGE_TYPES 的缺失类型子集
        message_set_list message_subsets = message_combinations(messages, MAX_ENRICHMENT_MESSAGE_TYPES);

        for (int i = 0; i < kv_size(message_subsets); i++)
        {
            khash_t(strSet) *subset = kv_A(message_subsets, i);
            // 调用 LLM 丰富种子，返回包含新消息类型的完整序列
            char *enriched = enrich_sequence(nl_file_content, subset);

            // 将新种子写入 in_dir，命名为 enriched_<i>_<原文件名>
            write_new_seeds(enriched_file_path, enriched);
        }
```

**第三步：新种子随普通种子一同被 `read_testcases()` 读入队列**

```c
// afl-fuzz.c  第 10687 行（startup 顺序）
setup_llm_grammars();   // 获取文法 → 同时确定 AllTypes
enrich_testcases();     // 丰富种子 → 写入 enriched_* 文件
read_testcases();       // 读取所有文件（包含新种子）→ 加入队列
```

丰富后的种子文件名含 `enriched_` 前缀，可在 `<out_dir>/queue/` 目录中找到形如 `id:...,orig:enriched_` 的文件。

---

## 5. 策略三：覆盖率停滞突破（SC）

### 5.1 论文思想

**目标**：解决挑战 C3（状态空间未知）。  
当模糊器连续生成 `UNINTERESTING_THRESHOLD`（默认 512）个无效输入时，判定进入覆盖率停滞（Coverage Plateau）。此时向 LLM 提供当前请求-响应通信历史，要求其生成能触发状态转换的下一条消息。

**关键参数**：
- `UNINTERESTING_THRESHOLD = 512`：连续多少个无效输入后触发 LLM 请求
- `CHATTING_THRESHOLD = 64`：整个 Fuzzing 会话中最多触发 LLM 64 次
- LLM 温度 `1.5`（高于文法提取的 0.5）：鼓励多样化生成而非精确复现

### 5.2 Prompt 设计

论文图 9 展示了停滞突破的 Prompt 模板，代码在 `chat-llm.c` 中：

```c
// ChatAFL/chat-llm.c  第 150 行
char *construct_prompt_stall(char *protocol_name, char *examples, char *history)
{
    char *template =
        "In the %s protocol, the communication history between the %s client "
        "and the %s server is as follows."
        "The next proper client request that can affect the server's state are:\n\n"
        "Desired format of real client requests:\n"
        "%s"                    // ← 从初始种子中抽取的真实消息格式示例
        "Communication History:\n\"\"\"\n"
        "%s"                    // ← 当前请求+响应历史
        "\"\"\"";
    ...
}
```

`examples` 字段给 LLM 展示真实消息格式（防止输出描述性文本而非实际消息），`history` 字段提供对话历史让 LLM 推断当前状态。

### 5.3 代码实现

```c
// ChatAFL/afl-fuzz.c  第 6846 行（位于 fuzz_one() 函数中）
if (uninteresting_times >= UNINTERESTING_THRESHOLD && chat_times < CHATTING_THRESHOLD)
{
    uninteresting_times = 0;   // 重置计数器

    // 1. 读取当前种子对应的服务器响应历史（存于 responses-ipsm/ 目录）
    char *response_fname = alloc_printf("%s/responses-ipsm/id:%s",
                                         out_dir, basename(queue_cur->fname));
    char **responses_temp = get_responses_from_file(response_fname, ...);

    // 2. 构建通信历史字符串（请求 + 响应交替拼接，限制到 HISTORY_PROMPT_LENGTH）
    for (int i = 0; i < response_count && it_pref != M2_prev; i++)
    {
        // JSON 转义请求消息，过滤非可打印字符
        json_object *request_v = json_object_new_string_len(...);
        // 拼接 history = "Request\nResponse\nRequest\nResponse\n..."
        memcpy(history + history_len, request, request_len);
        memcpy(history + history_len, response, response_len);
    }

    // 3. 构建 Prompt 并调用 LLM（温度 1.5，鼓励多样生成）
    char *stall_prompt = construct_prompt_stall(protocol_name, examples, history);
    char *stall_response = chat_with_llm(stall_prompt, "turbo", STALL_RETRIES, 1.5);

    // 4. 从 LLM 响应中提取第一条实际消息（跳过空行）
    char *extracted = extract_stalled_message(stall_response, strlen(stall_response));
    // 格式化：确保 \r\n 分隔符正确
    char *formatted = format_request_message(extracted);

    // 5. 将 LLM 生成的消息替换掉 M₂，注入当前序列并发送到服务器
    chat_times++;
}
```

**交互记录保存**：请求和响应被保存到 `<out_dir>/stall-interactions/` 目录，文件名为 `request-<id>` 和 `response-<id>`，便于事后分析 LLM 的推断质量。

### 5.4 关键辅助函数

```c
// ChatAFL/chat-llm.c  第 225 行
// 从 LLM 响应中提取第一条有效消息（跳过空行前缀）
char *extract_stalled_message(char *message, size_t message_len)
{
    // LLM 回复通常以空行开头，用正则跳过
    pcre2_code *extracter = pcre2_compile("\r?\n?.*?\r?\n", ...);
    int rc = pcre2_match(extracter, message, message_len, 0, 0, ...);
    // 返回第一个换行符之后的内容（即实际请求消息）
    res = strdup(message + ovector[1]);
}

// ChatAFL/chat-llm.c  第 247 行
// 修复消息格式：将裸 \n 替换为 \r\n，并在末尾补充 \r\n\r\n
char *format_request_message(char *message) { ... }
```

---

## 6. LLM 通信层

### 6.1 `chat_with_llm()` 函数

所有与 OpenAI API 的通信都经过 `chat-llm.c` 中的 `chat_with_llm()` 函数：

```c
// ChatAFL/chat-llm.c  第 45 行
char *chat_with_llm(char *prompt, char *model, int tries, float temperature)
{
    // model = "instruct" → 调用 gpt-3.5-turbo-instruct（/v1/completions）
    // model = 其他     → 调用 gpt-3.5-turbo（/v1/chat/completions）
    char *url = (strcmp(model, "instruct") == 0)
        ? "https://api.openai.com/v1/completions"
        : "https://api.openai.com/v1/chat/completions";

    // 使用 libcurl 发送 HTTP POST，授权头为 OPENAI_TOKEN
    // 响应通过回调 chat_with_llm_helper 写入内存缓冲区
    // 用 json-c 解析响应，提取 choices[0].message.content 或 choices[0].text

    // 失败（网络错误或 API 错误）时重试，最多 tries 次
    do { ... } while ((res != CURLE_OK || answer == NULL) && (--tries > 0));
}
```

### 6.2 模型选择策略

| 策略 | 使用模型 | API 端点 | 温度 |
|---|---|---|---|
| 文法提取（SA） | `gpt-3.5-turbo` | chat/completions | 0.5（精确） |
| 种子丰富（SB） | `gpt-3.5-turbo` | chat/completions | 0.5（精确） |
| 停滞突破（SC） | `gpt-3.5-turbo` | chat/completions | 1.5（多样） |

文法提取使用低温度以获得稳定、准确的格式输出；停滞突破使用高温度以鼓励生成之前未见过的消息。

### 6.3 API Key 注入机制

```c
// ChatAFL/chat-llm.h
#define OPENAI_TOKEN "1"   // 占位符，由 setup.sh 在构建时替换
```

```bash
# setup.sh
sed -i "s/#define OPENAI_TOKEN \".*\"/#define OPENAI_TOKEN \"$KEY\"/" $x/chat-llm.h
```

`setup.sh` 在构建 Docker 镜像前，用 `sed` 将 `chat-llm.h` 中的 `OPENAI_TOKEN` 替换为真实的 API Key，避免 Key 硬编码进代码仓库。

---

## 7. AFLNet 状态机层

ChatAFL 继承了 AFLNet 的协议状态机，并在其上构建 LLM 策略。

### 7.1 状态定义

AFLNet 不依赖规范中的状态定义，而是将服务器响应码序列定义为状态标识：

```c
// ChatAFL/aflnet.h
typedef struct {
    u32 id;                 // 状态 ID（由响应码序列哈希得到）
    u8 is_covered;          // 是否已被覆盖
    u32 paths;              // 经过该状态的路径总数
    u32 selected_times;     // 被作为目标选择的次数
    void **seeds;           // 能到达该状态的种子集合
} state_info_t;
```

例如，RTSP 服务器返回 `200 OK` 代表成功，不同状态下相同操作可能返回不同状态码。AFLNet 把响应码集合（如 `{200, 461}`）的哈希值作为状态 ID。

### 7.2 协议解析函数指针

```c
// ChatAFL/aflnet.h
extern unsigned int* (*extract_response_codes)(
    unsigned char *buf, unsigned int buf_size, unsigned int *state_count_ref);
```

每个协议提供专属的响应码提取函数（如 `extract_response_codes_ftp`、`extract_response_codes_rtsp` 等）。运行时通过 `-P <proto>` 参数选择，通过函数指针调用，无需修改核心代码即可支持新协议。

### 7.3 区域划分（Region）

```c
// ChatAFL/aflnet.h
typedef struct {
    int start_byte;          // 消息起始字节偏移
    int end_byte;            // 消息结束字节偏移
    char modifiable;         // 是否允许变异
    unsigned int *state_sequence;  // 该区域对应的状态序列
} region_t;
```

`extract_requests_<proto>()` 函数将原始字节缓冲区按消息边界切分为 `region_t` 数组。AFLNet 的变异以单条消息（region）为单位，ChatAFL 在此基础上进一步用 PCRE2 文法细化可变字节范围。

---

## 8. 消融实验版本对照

论文通过四个版本的消融研究量化每个策略的贡献：

| 版本 | 代码目录 | 启用策略 | 相对 AFLNet 的分支覆盖提升 |
|---|---|---|---|
| CL0（基线） | `aflnet/` | 无 | 0%（基准） |
| CL1 | `ChatAFL-CL1/` | SA（文法变异） | +3.04%，快 2.0× |
| CL2 | `ChatAFL-CL2/` | SA + SB（+种子丰富） | +3.86%，快 4.6× |
| CL3（完整） | `ChatAFL/` | SA + SB + SC（+停滞突破） | +5.81%，快 6.1× |

三个 `ChatAFL-*` 目录结构完全相同，区别仅在于 `afl-fuzz.c` 中是否启用对应函数调用：
- `ChatAFL-CL1`：`afl-fuzz.c` 调用 `setup_llm_grammars()`，但不调用 `enrich_testcases()`，且 `fuzz_one()` 中不触发停滞检测
- `ChatAFL-CL2`：在 CL1 基础上增加 `enrich_testcases()` 调用
- `ChatAFL`（CL3）：全部三个策略均启用

---

## 9. 实验结果摘要

### 状态覆盖（论文 RQ1）

| 指标 | vs AFLNet | vs NSFuzz |
|---|---|---|
| 状态转换数提升 | **+47.60%** | +42.69% |
| 加速比（达到同等转换数） | **48×** 更快 | 16× 更快 |
| 状态数提升 | **+29.55%** | +25.75% |

Live555 上最为显著：状态转换数提升 91%，加速比 228×（AFLNet 在多数运行中无法到达深层状态）。

### 代码覆盖（论文 RQ2）

| 指标 | vs AFLNet | vs NSFuzz |
|---|---|---|
| 分支覆盖提升 | **+5.81%** | +6.74% |
| 加速比 | **6×** 更快 | 10× 更快 |

### 漏洞发现（论文 RQ4）

ChatAFL 在 6 个协议实现上共发现 **9 个零日漏洞**，而 AFLNet 仅发现其中 3 个，NSFuzz 发现 4 个：

| ID | 目标 | 漏洞类型 | 状态 |
|---|---|---|---|
| 1-5, 7 | Live555 | 堆释放后使用、堆缓冲区溢出 | 已请求 CVE，部分已修复 |
| 6 | Live555 | 内存泄漏 | 已报告 |
| 8 | ProFTPD | 堆缓冲区溢出（FTP 命令解析） | 已请求 CVE，已修复 |
| 9 | Kamailio | 内存泄漏 | 已报告 |

**典型案例（Bug #1）分析**：
- 触发路径：`SETUP → PLAY → PAUSE → PLAY`（PAUSE 在初始种子中缺失）
- SB 策略：LLM 在种子丰富阶段添加 PAUSE 消息 → 种子库中出现含 PAUSE 的序列
- SA 策略：文法变异保持消息结构有效，不会破坏 PAUSE 的格式
- SC 策略：引导模糊器覆盖 `PLAY→READY→PLAY` 的状态转换路径
- 三个策略缺一不可，AFLNet 和 NSFuzz 在所有运行中均未生成含 PAUSE 的序列

---

*文档对应代码版本：ChatAFL GitHub 主分支（master），论文版本使用 `gpt-3.5-turbo` 和 `gpt-3.5-turbo-instruct`。GPT-4 版本见仓库 `gpt4-version` 分支。*
