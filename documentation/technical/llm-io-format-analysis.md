# ChatAFL LLM 调用点输入输出格式全面分析

> 本文档对项目中每次 LLM 调用的**预期输入格式**、**代码判断输出格式的逻辑**、**提示词设计方式**，以及**国产模型适配建议**进行系统性梳理。

---

## 目录

1. [调用点总览](#1-调用点总览)
2. [调用点 A：文法提取第一轮（SA-1）](#2-调用点-a文法提取第一轮sa-1)
3. [调用点 B：文法提取第二轮（SA-2）](#3-调用点-b文法提取第二轮sa-2)
4. [调用点 C：种子消息类型识别（SB-1，已注释）](#4-调用点-c种子消息类型识别sb-1已注释)
5. [调用点 D：种子序列丰富（SB-2）](#5-调用点-d种子序列丰富sb-2)
6. [调用点 E：停滞消息生成（SC）](#6-调用点-e停滞消息生成sc)
7. [输出格式判断机制汇总](#7-输出格式判断机制汇总)
8. [提示词设计原则与改进建议](#8-提示词设计原则与改进建议)
9. [国产模型适配注意事项](#9-国产模型适配注意事项)

---

## 1. 调用点总览

| 编号 | 调用位置 | 函数 | model 参数 | temperature | 输出用途 |
|---|---|---|---|---|---|
| A | `afl-fuzz.c:447` | `setup_llm_grammars()` | `"turbo"` | 0.5 | 文法模板第一批 |
| B | `afl-fuzz.c:454` | `setup_llm_grammars()` | `"turbo"` | 0.5 | 文法模板第二批（续写） |
| C | `chat-llm.c:782` | `get_protocol_message_types()` | `"instruct"` | 0.5 | 消息类型列表（当前已注释不使用）|
| D | `chat-llm.c:968` | `enrich_sequence()` | `"instruct"` | 0.5 | 丰富后的完整请求序列 |
| E | `afl-fuzz.c:6949` | `fuzz_one()` 停滞处理 | `"turbo"` | 1.5 | 下一条可推进状态的请求 |

> A/B 循环调用 `TEMPLATE_CONSISTENCY_COUNT`（默认 5）次，用于自洽性投票过滤噪声字段。

---

## 2. 调用点 A：文法提取第一轮（SA-1）

### 调用代码

```c
// afl-fuzz.c:441-447
char *first_question;
char *templates_prompt = construct_prompt_for_templates(protocol_name, &first_question);

for (int iter = 0; iter < TEMPLATE_CONSISTENCY_COUNT; iter++) {
    char *templates_answer = chat_with_llm(templates_prompt, "turbo", GRAMMAR_RETRIES, 0.5);
```

### 预期输入（发送给 LLM 的 prompt）

`construct_prompt_for_templates()` 构造的 messages 数组（JSON 格式），实际内容为：

```
[
  {"role": "system", "content": "You are a helpful assistant."},
  {"role": "user",   "content": "For the RTSP protocol, the DESCRIBE client request template is:\nDESCRIBE: [\"DESCRIBE <<VALUE>>\r\n\",\"CSeq: <<VALUE>>\r\n\",\"User-Agent: <<VALUE>>\r\n\",\"Accept: <<VALUE>>\r\n\",\"\r\n\"]\nFor the HTTP protocol, the GET client request template is:\nGET: [\"GET <<VALUE>>\r\n\"]\nFor the FTP protocol, all of client request templates are :"}
]
```

**关键设计**：
- 提供 **两个 Few-Shot 示例**（RTSP 的 DESCRIBE 和 HTTP 的 GET），防止模型过拟合到单一协议的格式
- 示例明确展示了 `<<VALUE>>` 占位符的使用方式（可变字段）
- 示例展示了 `\r\n` 的字面转义写法（协议行终止符）

### 期望输出格式

```
COMMAND_NAME: ["头部行模板", "字段行模板1", "字段行模板2", ...]

// 例如 FTP：
USER: ["USER <<VALUE>>\r\n"]
PASS: ["PASS <<VALUE>>\r\n"]
LIST: ["LIST\r\n"]
RETR: ["RETR <<VALUE>>\r\n"]
```

格式规则：
1. 每条文法以 `COMMAND_NAME:` 开头
2. 后跟 JSON 数组 `[...]`
3. 数组内每个元素为双引号字符串
4. 可变字段用 `<<VALUE>>` 标记
5. 每行以 `\r\n` 结尾（字面的反斜杠 r 反斜杠 n，不是真实的回车换行）

### 代码如何判断输出格式

```c
// chat-llm.c:379-405  extract_message_grammars()
void extract_message_grammars(char *answers, klist_t(gram) *grammar_list)
{
    char *ptr = answers;
    while (ptr < answers + len) {
        char *start = strchr(ptr, '[');   // 找第一个 '['
        char *end   = strchr(start, ']'); // 找对应的 ']'
        // 截取 [...] 子串，尝试用 json_tokener_parse() 解析
        json_object *jobj = json_tokener_parse(temp);
        *kl_pushp(gram, grammar_list) = jobj;
    }
}
```

**判断逻辑**：
- **有效**：字符串中存在 `[...]` 且能被 `json_tokener_parse()` 解析为合法 JSON 数组
- **无效/丢弃**：找不到 `[` 或 `]`，或 JSON 解析失败（静默跳过，不报错）
- **自洽性过滤**（`afl-fuzz.c:509`）：同一字段在 5 次采样中出现次数 `>= TEMPLATE_CONSISTENCY_COUNT/2 + 1`（即 3 次）才被采纳，少数噪声字段被过滤

**格式判断弱点**：
- 代码只检查 `[...]` 语法，不验证 `<<VALUE>>` 是否存在
- 不验证数组元素是否包含 `\r\n`
- 若 LLM 输出说明性文字而非纯格式内容，`extract_message_grammars()` 仍会尝试提取其中的 `[...]` 部分

---

## 3. 调用点 B：文法提取第二轮（SA-2）

### 调用代码

```c
// afl-fuzz.c:452-454
char *remaining_prompt = construct_prompt_for_remaining_templates(
    protocol_name, first_question, templates_answer);
char *remaining_templates = chat_with_llm(remaining_prompt, "turbo", GRAMMAR_RETRIES, 0.5);
```

### 预期输入（发送给 LLM 的 prompt）

`construct_prompt_for_remaining_templates()` 构造**多轮对话**格式，将第一轮的问答作为上下文：

```json
[
  {"role": "system",    "content": "You are a helpful assistant."},
  {"role": "user",      "content": "For the RTSP protocol, ...（第一轮问题）...For the FTP protocol, all of client request templates are :"},
  {"role": "assistant", "content": "USER: [\"USER <<VALUE>>\\r\\n\"]\nPASS: [...]...（第一轮LLM回答）"},
  {"role": "user",      "content": "For the FTP protocol, other templates of client requests are:"}
]
```

**关键设计**：
- 利用多轮对话让模型"记住"已经回答的部分，避免重复
- 第二轮的问题措辞为 `"other templates"`，引导模型补充遗漏的命令类型

### 期望输出格式

与调用点 A 完全相同：

```
STOR: ["STOR <<VALUE>>\r\n"]
DELE: ["DELE <<VALUE>>\r\n"]
MKD:  ["MKD <<VALUE>>\r\n"]
...（未在第一轮出现的命令）
```

### 代码如何判断输出格式

与 A 相同：两轮输出拼接后一起传入 `extract_message_grammars()`：

```c
// afl-fuzz.c:460-461
char *combined_templates = NULL;
asprintf(&combined_templates, "%s\n%s", templates_answer, remaining_templates);
extract_message_grammars(combined_templates, grammar_list);
```

---

## 4. 调用点 C：种子消息类型识别（SB-1，已注释）

> **当前状态**：该调用点的代码在 `enrich_testcases()` 中已被注释掉（`afl-fuzz.c:2775-2780`），消息类型集合 `message_types_set` 实际由调用点 A/B 的文法提取结果填充（`setup_llm_grammars()` 中的 `kh_put(strSet, message_types_set, message_type, &discard)`）。

### 原始调用代码（已注释）

```c
// chat-llm.c:782  — 被 get_protocol_message_types() 调用
char *state_answer = chat_with_llm(state_prompt, "instruct", MESSAGE_TYPE_RETRIES, 0.5);
```

### 预期输入

`construct_prompt_for_protocol_message_types()` 构造的**纯文本** prompt（非 JSON 数组）：

```
In the FTP protocol, the message types are:

Desired format:
<comma_separated_list_of_states_in_uppercase_and_without_whitespaces>
```

**重要**：此处 prompt 为纯文本字符串，直接传给使用 `"instruct"` 的 `completions` 端点。这是与 A/B/E 最大的不同——**不是 messages JSON 数组**。

### 期望输出格式

```
USER,PASS,LIST,RETR,STOR,DELE,MKD,RMD,PWD,CWD,QUIT
```

格式规则：
- 全大写
- 逗号分隔
- 无空格
- 无句点、无换行

### 代码如何判断输出格式

```c
// chat-llm.c:787-808
state_answer = format_string(state_answer);       // 去除首尾空白和句点
char *state_tokens = strtok(state_answer, ",");   // 按逗号切分
while (state_tokens != NULL) {
    char *protocol_state = format_string(state_tokens);
    // 存入 state_to_times 计数 map
    state_tokens = strtok(NULL, ",");
}
// 调用 3 次，超过 50% 出现的 token 才入最终集合（自洽性检查）
```

**判断逻辑**：
- 只要能被逗号切分，任何字符串都会被当作消息类型存入
- 没有大写验证，没有格式校验
- 依赖 3 次采样的自洽性投票过滤噪声

---

## 5. 调用点 D：种子序列丰富（SB-2）

### 调用代码

```c
// chat-llm.c:968
char *response = chat_with_llm(prompt, "instruct", ENRICHMENT_RETRIES, 0.5);
```

### 预期输入

`enrich_sequence()` 构造的纯文本 prompt：

```
The following is one sequence of client requests:
USER anonymous\r\n
PASS guest\r\n
LIST\r\n

Please add the RETR, STOR client requests in the proper locations, and the modified sequence of client requests is:
```

实际构造代码（`chat-llm.c:918-964`）：
- 对原始序列内容进行 JSON 转义（处理特殊字符）
- 指定 `MAX_ENRICHMENT_MESSAGE_TYPES`（默认 2）个缺失的消息类型
- 若序列过长，截断到 `MAX_TOKENS - prompt_overhead` 的允许长度

**重要**：与调用点 C 一样，这是纯文本 prompt，传给 `"instruct"` 端点。

### 期望输出格式

```
USER anonymous\r\n
PASS guest\r\n
RETR filename.txt\r\n
LIST\r\n
STOR newfile.txt\r\n
```

格式规则：
- 直接输出修改后的完整请求序列（续写风格）
- 每条命令以 `\r\n` 结尾（字面转义或真实字符均可，代码会处理）
- 不加说明文字，不加代码块标记

### 代码如何判断输出格式

```c
// afl-fuzz.c:2729-2739
char *unescaped_client_requests = unescape_string(client_request_answer);
char *formatted_unescaped_client_requests = format_string(unescaped_client_requests);

// 判断 1：输出不为空
if (formatted_unescaped_client_requests == NULL) continue;

// 判断 2：输出与原始内容不同（LLM没有原样返回）
if (strcmp(formatted_unescaped_client_requests, formatted_nl_file_content) == 0) {
    printf("## Skip the same seed\n");
    continue;
}

// 通过检查后写入 enriched_* 文件
unescaped_client_requests = format_request_message(unescaped_client_requests);
write_new_seeds(enriched_file_path, unescaped_client_requests);
```

`format_request_message()` 的后处理（`chat-llm.c:247-300`）：
- 将孤立的 `\n` 前面补 `\r`（确保 CRLF 格式）
- 末尾追加 `\r\n\r\n`（保证数据包被服务器接受）

**判断逻辑**：
- **没有内容格式验证**——只要非空且与原文不同，直接写入种子文件
- `unescape_string()` 负责将 `\r\n` 字面文本转换为真实字节
- 依赖 `format_request_message()` 做最终的字节级修复

---

## 6. 调用点 E：停滞消息生成（SC）

### 调用代码

```c
// afl-fuzz.c:6947-6949
char *stall_prompt = construct_prompt_stall(protocol_name, examples, history);
char *stall_response = chat_with_llm(stall_prompt, "turbo", STALL_RETRIES, 1.5);
```

### 预期输入

`construct_prompt_stall()` 构造的 messages JSON 数组：

```json
[
  {"role": "system", "content": "You are a helpful assistant."},
  {"role": "user",   "content": "In the FTP protocol, the communication history between the FTP client and the FTP server is as follows.The next proper client request that can affect the server's state are:\n\nDesired format of real client requests:\nRequest-1:\nUSER anonymous\r\nPASS guest\r\n\nRequest-2:\nUSER anonymous\r\nPASS guest\r\nCommunication History:\n\"\"\"\nUSER anonymous\r\n220 FTP server ready\r\nPASS guest\r\n230 Login successful\r\n...\"\"\"\n"}
]
```

构造过程（`afl-fuzz.c:6870-6945`）：
- **examples**：取第一条请求，复制两份作为格式示例（`Request-1` 和 `Request-2`），截断到 `EXAMPLES_PROMPT_LENGTH`（400 字符）
- **history**：交替拼接 `request + response`，截断到 `HISTORY_PROMPT_LENGTH`（1300 字符），**从末尾截取**（保留最近的通信历史）
- `temperature=1.5`：高温度保证输出多样性，避免重复停滞消息

### 期望输出格式

```
RETR interesting_file.txt\r\n
```

或者（LLM 有时会先输出空行再回答）：

```

STOR newfile.dat\r\n
```

格式规则：
- 一条完整的协议客户端请求
- 不含服务器响应
- 不含多余说明文字

### 代码如何判断输出格式

```c
// afl-fuzz.c:6976-6981
char *stall_message = extract_stalled_message(stall_response, strlen(stall_response));

// chat-llm.c:231  extract_stalled_message() 的 PCRE2 正则
pcre2_code *extracter = pcre2_compile("\r?\n?.*?\r?\n", ...);
// 匹配模式：可选的回车换行 + 任意内容 + 换行
// 返回 ovector[1] 之后的内容（即跳过第一行/空行之后的部分）
```

**判断逻辑**：
- 用 `\r?\n?.*?\r?\n` 匹配并**跳过**响应开头的空行或说明行
- 取 `ovector[1]` 之后的内容（第一个匹配项结束位置之后）
- 没有验证剩余内容是否是合法协议命令
- 之后由 `format_request_message()` 补全 CRLF 格式

**格式判断的关键注释**（`chat-llm.c:230`）：
```c
// After a lot of iterations, the model consistently responds with an empty line and then a line of text
```
说明这个正则是针对 GPT 的实际输出习惯（先空行再内容）设计的经验性处理。

---

## 7. 输出格式判断机制汇总

| 调用点 | 判断方式 | 格式验证强度 | 失败处理 |
|---|---|---|---|
| A（文法一）| `json_tokener_parse([...])` + 5 次自洽投票 | **中**：JSON 语法检查，但不验证 `<<VALUE>>` | 静默跳过无效 token |
| B（文法二）| 同 A，合并后一起处理 | **中** | 同 A |
| C（消息类型）| 逗号切分 + 3 次自洽投票 | **弱**：无格式验证 | 静默跳过 NULL |
| D（种子丰富）| 非空 + 与原文不同 | **极弱**：无内容验证 | `continue` 跳过 |
| E（停滞消息）| PCRE2 跳过首行 | **极弱**：无协议命令验证 | NULL 时跳过注入 |

**整体特点**：代码对 LLM 输出的格式验证非常宽松，主要依赖：
1. **自洽性采样**（A/B/C）：多次调用投票过滤噪声
2. **后处理修复**（D/E）：`unescape_string()` + `format_request_message()` 在字节层面修复格式
3. **静默失败**：格式不符时跳过而非报错，保证模糊器不崩溃

---

## 8. 提示词设计原则与改进建议

### 8.1 现有提示词的设计原则

#### 原则一：Few-Shot 格式示例（调用点 A）

```
// 提供两个不同协议的示例，防止模型只记住一种格式
For the RTSP protocol, the DESCRIBE client request template is:
DESCRIBE: ["DESCRIBE <<VALUE>>\r\n", ...]

For the HTTP protocol, the GET client request template is:
GET: ["GET <<VALUE>>\r\n"]

For the {target} protocol, all of client request templates are:
```

**效果**：Few-Shot 示例是文法提取中最关键的设计，决定了输出是否符合 `extract_message_grammars()` 的解析要求。

#### 原则二：Desired format 指令（调用点 C/E）

```
Desired format:
<comma_separated_list_of_states_in_uppercase_and_without_whitespaces>
```

```
Desired format of real client requests:
Request-1:
{example}
```

**效果**：明确告诉模型输出格式，减少解释性文字。

#### 原则三：续写风格（调用点 B/D/E）

- B：多轮对话续写（`other templates`）
- D：`...the modified sequence of client requests is:` 让模型直接续写
- E：`The next proper client request ...are:` 让模型直接续写命令

**效果**：续写风格避免模型输出说明文字，直接产出可用内容。

#### 原则四：高温度采样（调用点 E）

```c
chat_with_llm(stall_prompt, "turbo", STALL_RETRIES, 1.5);  // temperature=1.5
```

**效果**：高温度保证停滞阶段的输出多样性，避免重复生成相同命令。

---

### 8.2 各调用点提示词的弱点与改进建议

#### 调用点 A/B 改进建议

**当前弱点**：LLM 可能在 `[...]` 数组前后输出说明文字，干扰解析：

```
// 不良输出示例（有说明文字）
Here are the FTP templates:
USER: ["USER <<VALUE>>\r\n"]
Note: PASS follows USER
PASS: ["PASS <<VALUE>>\r\n"]
```

**改进提示词**：

```
// 在现有 Few-Shot 后追加格式约束
// 原：
For the {protocol} protocol, all of client request templates are:

// 改为：
For the {protocol} protocol, all of client request templates are (output ONLY the templates, no explanations):
```

或在 system prompt 中添加：

```json
{"role": "system", "content": "You are a protocol expert. Respond ONLY with the requested format, no explanations or notes."}
```

#### 调用点 D 改进建议

**当前弱点**：`enrich_sequence()` 的 prompt 对输出格式没有明确约束，LLM 可能输出：
- Markdown 代码块（` ```ftp ... ``` `）
- 说明文字前缀（`Here is the modified sequence:`）
- 注释（`# Added RETR command`）

**改进提示词**（在现有 prompt 末尾追加）：

```
// 原：
...and the modified sequence of client requests is:

// 改为：
...and the modified sequence of client requests is (output ONLY the raw protocol commands, no markdown, no explanations):
```

#### 调用点 E 改进建议

**当前弱点**：`extract_stalled_message()` 只跳过一个前缀行，若 LLM 输出多行说明则提取错误。

**改进提示词**：

```
// 在 Desired format 部分增加约束
Desired format of real client requests:
Request-1:
{example_request}
Request-2:
{example_request}

Output ONLY one single client request line, no explanations:
```

同时建议将 `extract_stalled_message()` 的正则改为更鲁棒的版本：

```c
// 当前：跳过第一行（空行或说明行）
pcre2_code *extracter = pcre2_compile("\r?\n?.*?\r?\n", ...);

// 改进：跳过所有非协议命令行（以字母开头才是命令）
// 寻找第一个以大写字母开头的行
pcre2_code *finder = pcre2_compile("(?m)^[A-Z][A-Z0-9]+.*\r?\n", ...);
```

---

### 8.3 通用提示词改进模板

针对国产模型（DeepSeek/Qwen 等），可以在 system prompt 中增加中文指令增强格式遵从性：

```json
{
  "role": "system",
  "content": "You are a network protocol expert assistant. Always respond ONLY with the exact requested format. Do not add explanations, notes, markdown formatting, or code blocks unless specifically requested."
}
```

---

## 9. 国产模型适配注意事项

### 9.1 instruct 端点的处理差异

调用点 C 和 D 使用 `"instruct"` 模型，向 OpenAI `completions` 端点发送**纯文本 prompt**。
国产模型不提供 `completions` 端点（续写模式），需要将纯文本 prompt 包装为 `messages` 数组：

```c
// 包装逻辑（需在 chat_with_llm() 内添加）
if (strcmp(model, "instruct") == 0) {
    // 原始 prompt 是纯文本，包装为 user 消息
    asprintf(&messages_json,
             "[{\"role\": \"user\", \"content\": \"%s\"}]",
             escaped_prompt);
}
```

### 9.2 各调用点格式兼容性预测

| 调用点 | 格式兼容性 | 风险点 | 建议 |
|---|---|---|---|
| A（文法一）| **高** | Few-Shot 示例清晰，格式约束强 | 可直接使用 |
| B（文法二）| **高** | 多轮对话格式国产模型支持好 | 可直接使用 |
| C（消息类型）| **中** | 国产模型可能输出带空格的列表 | 加 `without_whitespaces` 约束，已有 |
| D（种子丰富）| **中低** | 国产模型可能添加 Markdown 代码块 | 追加 `no markdown` 指令 |
| E（停滞消息）| **中低** | 国产模型输出习惯可能与 GPT 不同，空行前缀规律不同 | 改进 `extract_stalled_message()` 正则 |

### 9.3 调试建议

代码中有大量被注释的调试打印语句，在调试国产模型适配时可临时开启：

```c
// afl-fuzz.c:451  开启文法输出打印
printf("## Answer from LLM:\n %s\n", templates_answer);

// afl-fuzz.c:6948,6950  开启停滞 prompt/response 打印
printf("Got prompt:\n\n%s\n", stall_prompt);
printf("Got response:\n\n%s\n", stall_response);
```

另外，各调用点的 prompt 和 response 会保存到文件：
- 文法：`{out_dir}/protocol-grammars/llm-grammar-output-{iter}`
- 文法 PCRE2 模式：`{out_dir}/protocol-grammars/pattern-{n}`
- 停滞：`{out_dir}/stall-interactions/prompt-{n}` 和 `response-{n}`

可通过检查这些文件验证国产模型的实际输出格式。
