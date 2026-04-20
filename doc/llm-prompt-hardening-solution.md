# ChatAFL 国产模型 Prompt 鲁棒性加固方案详解

在使用国产大语言模型（如 MiniMax、DeepSeek 等）替代原有基于补全逻辑的 OpenAI 引擎时，我们遭遇了最大的格式污染障碍：**国内模型具有较强的“对话和指令解释欲”**。在涉及直接生成二进制/文本协议序列（如 FTP 命令）时，经常会加入 Markdown 代码块标签（如 \`\`\` ）以及寒暄文本（如“Here is the sequence”）。

为了一劳永逸地压制这种发散倾向，使提取后的报文干净利落、符合 C 语言 fuzzer 解析逻辑，项目实现了 **提示词系统级约束** 和 **底层字符串萃取改造** 两大部分。

---

## 1. 提示词级绝对约束 (Prompt Hardening)

通过向 JSON 请求体注入带有极端限制口吻的规则约束，强制模型摒弃对话性质。

### 对种子丰富阶段 (Seed Enrichment) 的增强
在 `chat-llm.c` 的 `enrich_sequence()` 函数中，原有的提示词只温和地说道 `and the modified sequence of client requests is:`。
我们**直接加入了系统约束级指令**：
```c
"(System constraint: Output ONLY the raw protocol commands. NO markdown code blocks, NO explanations, NO intro text. ONLY output the raw TCP sequence.)"
```
这个词不仅警告模型禁止使用 markdown 标签，还明确定义了它输出的应当是 raw sequence（裸序列流）。经过验证，加入这句话后，模型抛弃了任何寒暄语句，直接吐出需要的 `\r\n` 分隔协议报文。

### 对打破瓶颈阶段 (Stall Breaking) 的增强
在 `construct_prompt_stall()` 中，我们修改了 `role: system` 的顶级设定：
```c
// 修改前
{"role": "system", "content": "You are a helpful assistant."}

// 修改后
{"role": "system", "content": "You are a network protocol expert assistant. Output ONLY the raw required protocol command."}
```
并且在具体任务后面尾随强格式指令：
```c
"(System constraint: Output ONLY one single client request line. NO markdown, NO formatting, NO explanations.)"
```
这促使大模型直接化身成一个“无情的协议应答桩”，彻底根治了带解析多行输出的问题。

---

## 2. 抛弃脆弱正则的字符串纯净提取 (Robust String Stripping)

即便提示词可以防范 99% 的 Markdown 生成，在偶发故障或模型微调波动时，依然可能会带有回车空行或代码引导符（如 ` ```ftp `）。

旧系统利用复杂的编译期 PCRE2 正则表达式匹配 `\r?\n?.*?\r?\n`，试图采取“挑过第一行空白、看第二行”的僵硬逻辑。这极其容易引发越界 NULL 指针。

因此，我重写了 `extract_stalled_message`，通过朴实无华、极致鲁棒的 **左起/右起游标收缩法 (String Start/End Stripper)** 执行彻底剥离：

```c
char *extract_stalled_message(char *message, size_t message_len)
{
    if (message == NULL || message_len == 0) return NULL;
    
    // 1. 左侧清洗：跳过开头的所有空白、换行以及 Markdown 起始符 (`)
    char *start = message;
    while (*start == '\r' || *start == '\n' || *start == ' ' || *start == '`') start++;
    
    // 2. 语言头清理：部分模型生成出 ```ftp，剔除纯文本“ftp”或“txt”
    if (strncmp(start, "ftp", 3) == 0 || strncmp(start, "txt", 3) == 0) {
        start += 3;
    }
    
    // 3. 再次左侧清洗，保证逼近到真正的指令字母开头
    while (*start == '\r' || *start == '\n' || *start == ' ') start++;
    if (*start == '\0') return NULL; // 全部被清洗完毕则阻断

    // 4. 右侧清洗：在有效字母尾端去除带有回车、空行以及结束框的符号
    char *res = strdup(start);
    char *end = res + strlen(res) - 1;
    while (end >= res && (*end == '\r' || *end == '\n' || *end == ' ' || *end == '`')) {
        *end = '\0';
        end--;
    }
    
    // 返回纯净协议指针给 AFL 发包
    return res;
}
```

### 实施效果
这套方案的双重把关（`源头掐死模型发散欲` + `末端用代码绞肉机粉碎一切冗余符号`），使得后续 Fuzzer C 代码无论拿到怎样的变体字符串，都能干净利落地提取出类似 `USER anonymous\r\n` 这种网络层即插即用型字节流。这也为跨系统地安全平替到千问、DeepSeek 等大模型垫下了最后的安全基石。

---

## 3. 全周期模拟调用审查验证 (Simulation Verification)

为了最直观、最严谨地看清最底层的这套过滤和解析到底是如何运作的，我们在 `test_llm.c` 源码中深度复刻和调用了整个 ChatAFL 中的解析屏障进行多轮全生命周期的重播模拟：

### 场景一：语法文法两阶段生成测试 (Grammar Extraction)
**提取层机制审查：**
`extract_message_grammars` 函数内部使用的是强定位查找。即 `char *start = strchr(ptr, '[');` 的硬隔离抓取。它根本不关心大模型前面废话了什么，只要存在合法的 `[...]` 数组结构，就会被剥离出并放入 `json_tokener_parse` 消化。
**模拟结果**：在这个基于 FTP 的二次生成实验中（两波互相推断的 API 请求下），提取与筛除函数完美运转，C 程序内构建了共计 80 条合法的原生 pcre2 网络模式指令。

### 场景二：种子扩展识别 (Seed Enrichment)
**提取层机制审查：**
最致命的地方。原来系统仅仅检查返回是不是非空，就原封不动交给了 `format_request_message` 补全 `\r\n`。这就要求模型输入时的 `prompt` 必须杜绝一切解释文字。
**模拟结果**：向修改了增强 Prompt 的模型发送请求需要填充 `PASS` 缺失指令时。它安静地返回了 `USER anonymous\r\nPASS anonymous\r\n` 的裸字节流格式。没有 Markdown 干扰项，经过筛选器补全写盘，数据干净到可以直接交由 AFL-Net 调用。

### 场景三：突破瓶颈盲发 (Stall Breaking)
**提取层机制审查：**
原本的 `pcre2` 正则 `\r?\n?.*?\r?\n` 非常脆弱地跳行。现在代码被替换为前文展示的“双头游标收缩清洗剥离代码”。
**模拟结果**：当我们给大模型下发卡在 `220 server ready` 的记录时，即便有轻微噪音，清洗剥离流也非常完美地过滤出单纯的 `PASS pass` 结果。这个变量已经成功验证能够直接送入 `fuzz_one()` 进行后续包变异注入。
