# ChatAFL 国产 LLM 接口迁移与测试执行方案

> 本文档描述两项改造计划的完整执行方案：  
> **计划一**：将 LLM 接口从 OpenAI 替换为国产模型（DeepSeek / Qwen / MiniMax / GLM）  
> **计划二**：编写小规模运行测试，验证模型修改的完整性与输出合法性

---

## 目录

1. [现状分析](#1-现状分析)
2. [计划一：国产模型接口迁移](#2-计划一国产模型接口迁移)
   - [2.1 修改 chat-llm.h](#21-修改-chat-llmh)
   - [2.2 修改 chat-llm.c](#22-修改-chat-llmc)
   - [2.3 修改 setup.sh 注入逻辑](#23-修改-setupsh-注入逻辑)
   - [2.4 同步三个副本](#24-同步三个副本)
3. [计划二：小规模测试方案](#3-计划二小规模测试方案)
   - [3.1 第一层：LLM 接口单元测试](#31-第一层llm-接口单元测试)
   - [3.2 第二层：文法结构合法性测试](#32-第二层文法结构合法性测试)
   - [3.3 第三层：端到端冒烟测试](#33-第三层端到端冒烟测试)
4. [各模型配置参数对照表](#4-各模型配置参数对照表)
5. [推荐执行顺序](#5-推荐执行顺序)
6. [成功验收标准](#6-成功验收标准)

---

## 1. 现状分析

### 当前接口实现位置

所有 LLM 交互集中在 `ChatAFL/chat-llm.c` 的 `chat_with_llm()` 函数（第 45–148 行）。

### 存在的硬编码问题

| 问题 | 文件 | 行号 | 原始内容 |
|---|---|---|---|
| API URL 硬编码 | `chat-llm.c` | 53–57 | `https://api.openai.com/v1/completions` 和 `.../chat/completions` |
| Token 宏定义 | `chat-llm.h` | 15 | `#define OPENAI_TOKEN "1"` |
| 模型名称硬编码 | `chat-llm.c` | 65、69 | `"gpt-3.5-turbo-instruct"`、`"gpt-3.5-turbo"` |
| instruct 分支 | `chat-llm.c` | 51–70、107–116 | 区分 `text` 字段和 `message.content` 字段的两套解析逻辑 |
| instruct 调用点 | `chat-llm.c` | 782、968 | `get_protocol_message_types()`、`enrich_sequence()` 传入纯文本 prompt |

### 关键设计结论

国产模型（DeepSeek、Qwen、MiniMax、GLM）**全部兼容 OpenAI Chat Completions API 格式**，只需修改：
1. Base URL
2. 认证 Token
3. 模型名称字符串
4. 将 `instruct` 类型的纯文本 prompt 包装为 `messages` 数组格式

JSON 请求体结构、响应解析路径（`choices[0].message.content`）**无需改动**。

---

## 2. 计划一：国产模型接口迁移

### 2.1 修改 `chat-llm.h`

**目标**：将 Token、URL、模型名从编译时常量改为可配置宏，并由 `setup.sh` 在构建时注入。

```diff
- #define OPENAI_TOKEN "1"
+ /* 以下三项由 setup.sh 在构建时注入 */
+ #define LLM_TOKEN    "YOUR_API_KEY_HERE"
+ #define LLM_BASE_URL "https://api.deepseek.com"
+ #define LLM_MODEL_CHAT     "deepseek-chat"
+ #define LLM_MODEL_INSTRUCT "deepseek-chat"
```

> 注意：国产模型统一使用 Chat Completions 端点，`LLM_MODEL_INSTRUCT` 与 `LLM_MODEL_CHAT` 填写相同值即可。

---

### 2.2 修改 `chat-llm.c`

#### 修改点 1：`chat_with_llm()` 函数体（第 45–148 行）

**原始逻辑**：根据 `model` 参数是否为 `"instruct"` 选择不同 URL 和请求体格式。

**修改后逻辑**：

```c
char *chat_with_llm(char *prompt, char *model, int tries, float temperature)
{
    CURL *curl;
    CURLcode res = CURLE_OK;
    char *answer = NULL;

    /* ① 统一端点：所有国产模型均用 chat completions */
    char url[256];
    snprintf(url, sizeof(url), "%s/v1/chat/completions", LLM_BASE_URL);

    /* ② 认证头使用可配置宏 */
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header),
             "Authorization: Bearer %s", LLM_TOKEN);

    char *content_header = "Content-Type: application/json";
    char *accept_header  = "Accept: application/json";
    char *data = NULL;

    /* ③ 模型名选择 */
    const char *model_name = (strcmp(model, "instruct") == 0)
                             ? LLM_MODEL_INSTRUCT
                             : LLM_MODEL_CHAT;

    /* ④ instruct 类型 prompt 为纯文本，需包装为 messages 数组 */
    char *messages_json = NULL;
    if (strcmp(model, "instruct") == 0) {
        /* 对 prompt 中的双引号做转义 */
        json_object *escaped = json_object_new_string(prompt);
        const char *escaped_str = json_object_to_json_string(escaped);
        asprintf(&messages_json,
                 "[{\"role\": \"user\", \"content\": %s}]",
                 escaped_str);
        json_object_put(escaped);
    } else {
        messages_json = prompt;  /* chat 类型 prompt 已是 JSON 数组 */
    }

    /* ⑤ 统一请求体格式 */
    asprintf(&data,
             "{\"model\": \"%s\", \"messages\": %s,"
             " \"max_tokens\": %d, \"temperature\": %f}",
             model_name, messages_json, MAX_TOKENS, temperature);

    if (strcmp(model, "instruct") == 0) {
        free(messages_json);
    }

    /* ... curl 请求逻辑不变 ... */

    /* ⑥ 统一响应解析：所有模型均走 choices[0].message.content */
    if (json_object_object_get_ex(jobj, "choices", NULL)) {
        json_object *choices     = json_object_object_get(jobj, "choices");
        json_object *first_choice = json_object_array_get_idx(choices, 0);
        json_object *message     = json_object_object_get(first_choice, "message");
        json_object *content     = json_object_object_get(message, "content");
        const char  *data_str    = json_object_get_string(content);
        if (data_str[0] == '\n') data_str++;
        answer = strdup(data_str);
    }
```

#### 修改点 2：删除 `instruct` URL 分支（原第 51–58 行）

```diff
- if (strcmp(model, "instruct") == 0) {
-     url = "https://api.openai.com/v1/completions";
- } else {
-     url = "https://api.openai.com/v1/chat/completions";
- }
```

#### 修改点 3：删除 `text` 字段解析分支（原第 107–111 行）

```diff
- if (strcmp(model, "instruct") == 0) {
-     json_object *jobj4 = json_object_object_get(first_choice, "text");
-     data = json_object_get_string(jobj4);
- } else {
      json_object *jobj4 = json_object_object_get(first_choice, "message");
      json_object *jobj5 = json_object_object_get(jobj4, "content");
      data = json_object_get_string(jobj5);
- }
```

---

### 2.3 修改 `setup.sh` 注入逻辑

`setup.sh` 当前注入 `OPENAI_TOKEN`，需扩展为同时注入 URL 和模型名：

```bash
# setup.sh 新增参数（调用示例）
# KEY=sk-xxx BASE_URL=https://api.deepseek.com MODEL=deepseek-chat ./setup.sh

for fuzzer_dir in ChatAFL ChatAFL-CL1 ChatAFL-CL2; do
    sed -i "s|#define LLM_TOKEN.*|#define LLM_TOKEN \"${KEY}\"|"          $fuzzer_dir/chat-llm.h
    sed -i "s|#define LLM_BASE_URL.*|#define LLM_BASE_URL \"${BASE_URL}\"|" $fuzzer_dir/chat-llm.h
    sed -i "s|#define LLM_MODEL_CHAT.*|#define LLM_MODEL_CHAT \"${MODEL}\"|" $fuzzer_dir/chat-llm.h
    sed -i "s|#define LLM_MODEL_INSTRUCT.*|#define LLM_MODEL_INSTRUCT \"${MODEL}\"|" $fuzzer_dir/chat-llm.h
done
```

---

### 2.4 同步三个副本

需要同步修改的文件（三个版本完全一致）：

```
ChatAFL/chat-llm.h       ← 新增 LLM_TOKEN / LLM_BASE_URL / LLM_MODEL_* 宏
ChatAFL/chat-llm.c       ← 修改 chat_with_llm() 逻辑

ChatAFL-CL1/chat-llm.h
ChatAFL-CL1/chat-llm.c

ChatAFL-CL2/chat-llm.h
ChatAFL-CL2/chat-llm.c
```

> `aflnet/` 目录不含 LLM 逻辑，无需修改。

---

## 3. 计划二：小规模测试方案

### 3.1 第一层：LLM 接口单元测试

利用 `chat-llm.c` 末尾已注释的 `main()` 调试入口（第 976–1183 行），恢复并扩展为独立测试程序。

#### 编译命令

```bash
cd ChatAFL
gcc -g -DTEST_MODE -o test_llm \
    chat-llm.c \
    -lcurl -ljson-c -lpcre2-8 \
    -I.
```

> 编译时需系统已安装：`libcurl4-openssl-dev`、`libjson-c-dev`、`libpcre2-dev`

#### 测试用例

| # | 测试内容 | 通过条件 |
|---|---|---|
| T1 | `chat_with_llm()` API 调用 | 返回非空字符串，无 `Error response is:` |
| T2 | `construct_prompt_for_templates("FTP", ...)` | 返回合法 JSON 字符串（`[{...}]` 格式） |
| T3 | LLM 响应文法提取 `extract_message_grammars()` | 能从响应中提取至少一个 `[...]` JSON 数组 |
| T4 | `extract_message_pattern()` 编译 PCRE2 | 返回非空 `message_type`，`patterns[0]` 非 NULL |
| T5 | `construct_prompt_for_protocol_message_types("FTP")` | 返回包含 `"In the FTP protocol"` 的字符串 |
| T6 | `enrich_sequence()` 种子丰富 | 返回包含原始命令关键字的修改序列 |

#### 测试脚本框架

```c
/* test_llm_main.c（在 chat-llm.c 的 main() 位置恢复） */
#ifdef TEST_MODE
int main(int argc, char **argv) {
    printf("=== T1: API connectivity ===\n");
    char *prompt = "[{\"role\":\"user\",\"content\":\"Say hello.\"}]";
    char *resp = chat_with_llm(prompt, "chat", 3, 0.5);
    printf(resp ? "PASS: %s\n" : "FAIL: NULL response\n", resp ? resp : "");

    printf("=== T2: Grammar prompt construction ===\n");
    char *final_msg = NULL;
    char *grammar_prompt = construct_prompt_for_templates("FTP", &final_msg);
    printf(grammar_prompt ? "PASS\n" : "FAIL\n");

    printf("=== T3: Grammar extraction ===\n");
    char *grammar_resp = chat_with_llm(grammar_prompt, "chat", 3, 0.5);
    klist_t(gram) *grammar_list = kl_init(gram);
    extract_message_grammars(grammar_resp, grammar_list);
    printf(kl_size(grammar_list) > 0 ? "PASS: %zu grammars\n" : "FAIL: 0 grammars\n",
           kl_size(grammar_list));

    printf("=== T4: PCRE2 pattern compile ===\n");
    /* ... 遍历 grammar_list，调用 extract_message_pattern() ... */

    return 0;
}
#endif
```

---

### 3.2 第二层：文法结构合法性测试

验证 LLM 输出的文法格式是否能被解析管道完整处理。

#### 测试脚本 `test_grammar_pipeline.sh`

```bash
#!/bin/bash
set -e
PROTOCOL=${1:-FTP}
BINARY=./test_llm

echo ">>> 策略一：文法提取 + PCRE2 编译"
$BINARY grammar $PROTOCOL 2>&1 | tee /tmp/grammar_out.txt
# 检查是否输出了 Header pattern 和 Fields pattern
grep "Header pattern is" /tmp/grammar_out.txt && echo "[PASS] 头部模式已生成"
grep "Fields pattern is" /tmp/grammar_out.txt && echo "[PASS] 字段模式已生成"

echo ">>> 策略二：种子丰富"
# 使用项目自带的 FTP 测试种子
SEED=$(ls ChatAFL/benchmark/subjects/FTP/LightFTP/in-ftp/*.raw | head -1)
$BINARY enrich $PROTOCOL "$SEED" 2>&1 | tee /tmp/enrich_out.txt
[ -s /tmp/enrich_out.txt ] && echo "[PASS] 返回非空丰富序列" || echo "[FAIL] 空响应"

echo ">>> 策略三：停滞消息生成"
$BINARY stall $PROTOCOL 2>&1 | tee /tmp/stall_out.txt
grep -v "^$" /tmp/stall_out.txt | head -1  # 打印第一个非空行
echo "[PASS] 停滞消息已生成"
```

#### 文法格式合法性检查规则

LLM 对策略一的输出必须满足以下格式（否则 `extract_message_grammars()` 无法解析）：

```
COMMAND_NAME: ["COMMAND_NAME <<VALUE>>\r\n", "Field: <<VALUE>>\r\n", ...]
```

检查项：
- [ ] 每条文法以 `[` 开头、`]` 结尾
- [ ] 数组内元素为双引号括起的字符串
- [ ] 头部元素包含 `<<VALUE>>` 占位符（可变字段）或固定字面量
- [ ] 字符串中包含 `\\r\\n` 转义序列（协议行结束符）

---

### 3.3 第三层：端到端冒烟测试

使用依赖最少的 **LightFTP** 目标，仅运行 **60 秒**，验证三条策略全部触发。

#### 构建测试镜像

```bash
# 先注入 API Key 和模型配置
KEY="your_api_key" \
BASE_URL="https://api.deepseek.com" \
MODEL="deepseek-chat" \
  ./setup.sh

# 只构建 LightFTP 的 ChatAFL 镜像（约 5 分钟）
cd benchmark/subjects/FTP/LightFTP
docker build -t chatafl-lightftp-smoke .
```

#### 运行冒烟测试

```bash
docker run --rm \
  --name smoke_test \
  chatafl-lightftp-smoke \
  /bin/bash -c "
    timeout 60 afl-fuzz \
      -P FTP \
      -i /home/user/in-ftp \
      -o /tmp/smoke_out \
      -N tcp://127.0.0.1/21 \
      -x ftp.dict \
      -- /home/user/lightftp/Source/Release/ftpd /home/user/ftpd.conf \
    2>&1 | tee /tmp/smoke.log
    echo '=== Smoke test log tail ==='
    tail -20 /tmp/smoke.log
  "
```

#### 关键输出验证

```bash
# 检查策略一：文法提取成功
docker logs smoke_test | grep "Header pattern is" | wc -l
# 期望：> 0

# 检查策略二：种子丰富成功  
docker logs smoke_test | grep "enriched_" | wc -l
# 期望：> 0（enriched_* 文件被写入 in_dir）

# 检查是否有 API 错误
docker logs smoke_test | grep "Error response is:" | wc -l
# 期望：= 0

# 检查 afl-fuzz 主循环启动
docker logs smoke_test | grep "afl-fuzz" | head -5
```

---

## 4. 各模型配置参数对照表

| 模型 | `LLM_BASE_URL` | `LLM_MODEL_CHAT` | `LLM_MODEL_INSTRUCT` | 免费额度 |
|---|---|---|---|---|
| **DeepSeek** | `https://api.deepseek.com` | `deepseek-chat` | `deepseek-chat` | 有（新用户） |
| **Qwen（阿里云）** | `https://dashscope.aliyuncs.com/compatible-mode/v1` | `qwen-plus` | `qwen-plus` | 有（百万 token） |
| **MiniMax** | `https://api.minimax.chat/v1` | `abab6.5s-chat` | `abab6.5s-chat` | 有限 |
| **GLM（智谱）** | `https://open.bigmodel.cn/api/paas/v4` | `glm-4-flash` | `glm-4-flash` | 有（glm-4-flash 免费） |

> **推荐优先使用 DeepSeek**：  
> - API 格式与 OpenAI 兼容度最高，改动风险最小  
> - `deepseek-chat` 模型对协议格式理解较好  
> - 中文文档完整，调试方便

---

## 5. 推荐执行顺序

```
Step 1  修改 ChatAFL/chat-llm.h        ← 新增 4 个宏定义，最小改动
Step 2  修改 ChatAFL/chat-llm.c        ← 修改 chat_with_llm() 核心逻辑（约 40 行）
Step 3  本地编译验证（make clean all） ← 确认无编译错误
Step 4  编译 test_llm 独立测试程序     ← 30 秒内验证 API 可达
Step 5  运行第一层单元测试（T1–T6）    ← 验证接口改造完整性
Step 6  运行第二层文法结构测试         ← 验证解析管道不断链
Step 7  同步修改 CL1 / CL2 副本       ← 三文件批量替换
Step 8  运行第三层 Docker 冒烟测试     ← 最终端到端验证（约 10 分钟）
```

---

## 6. 成功验收标准

### 接口层（计划一）

- [ ] `make clean all` 编译无 error、无 warning
- [ ] `chat_with_llm()` 调用国产 API 返回 HTTP 200，`answer != NULL`
- [ ] `instruct` 类型 prompt 被正确包装为 `[{"role":"user","content":"..."}]` 格式
- [ ] 去掉 `gpt-3.5-turbo-instruct` 和 `gpt-3.5-turbo` 的硬编码引用

### 文法解析层（计划二 第一、二层）

- [ ] `extract_message_grammars()` 能从响应中提取至少一个 JSON 数组（`kl_size > 0`）
- [ ] `extract_message_pattern()` 对每条文法编译 PCRE2 无错，`patterns[0] != NULL`
- [ ] `Header pattern is` 和 `Fields pattern is` 出现在 stdout 中
- [ ] `enrich_sequence()` 返回包含原始协议命令关键字的非空字符串

### 系统层（计划二 第三层）

- [ ] Docker 容器正常启动，`afl-fuzz` 主循环执行至少 10 次迭代
- [ ] 无 `Error response is:` 或 `curl error` 输出
- [ ] `stall-interactions/` 目录被创建（策略三触发过至少一次，或 60 秒内未触发但无报错）
- [ ] `enriched_*` 文件存在于输出目录中（策略二触发成功）
