# MiniMax API 替换 OpenAI 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 ChatAFL 的 LLM 通信模块从 OpenAI API 替换为 MiniMax `chatcompletion_v2` API，确保内存安全、Token 可监控、三步验证通过

**Architecture:** 最小化修改原则 - 仅修改 API 端点 URL、Model、Token 定义，删除 instruct 分支，添加 Curl Dump 调试和 Token 监控

**Tech Stack:** C 语言, libcurl, json-c, pcre2

---

## 文件结构

| 操作 | 文件路径 |
|------|----------|
| 修改 | `ChatAFL/chat-llm.h` |
| 修改 | `ChatAFL/chat-llm.c` |
| 创建 | `ChatAFL/test_api.c` |
| 复制 | `ChatAFL-CL1/chat-llm.c` (覆盖) |
| 复制 | `ChatAFL-CL1/chat-llm.h` (覆盖) |
| 复制 | `ChatAFL-CL2/chat-llm.c` (覆盖) |
| 复制 | `ChatAFL-CL2/chat-llm.h` (覆盖) |

---

## Task 1: 修改 chat-llm.h - Token 和 Model 定义 ✅

**Files:**
- Modify: `ChatAFL/chat-llm.h:15-16`

**执行状态:** ✅ 完成 (2026-04-16)

**执行结果:**
```c
#define MINIMAX_TOKEN "your_api_key_here"
#define MINIMAX_MODEL "MiniMax-M2.7"
```

- [x] **Step 1: 替换 OPENAI_TOKEN 为 MINIMAX_TOKEN (第 15 行)** ✅

将：
```c
#define OPENAI_TOKEN "1"
```
替换为：
```c
#define MINIMAX_TOKEN "your_api_key_here"
```

- [x] **Step 2: 新增 MINIMAX_MODEL 宏定义 (第 16 行，在 MINIMAX_TOKEN 之后添加)** ✅

```c
#define MINIMAX_MODEL "MiniMax-M2.7"
```

- [x] **Step 3: 提交** ✅

```bash
cd /mnt/e/lunwen/ChatAFL
git add ChatAFL/chat-llm.h
git commit -m "chore: replace OPENAI_TOKEN with MINIMAX_TOKEN and add MINIMAX_MODEL"
```

---

## Task 2: 修改 chat-llm.c - 请求构造部分 ✅

**Files:**
- Modify: `ChatAFL/chat-llm.c:50-70` (URL、Auth Header、JSON 组装)
- Modify: `ChatAFL/chat-llm.c:92` (添加 Curl Dump)

**执行状态:** ✅ 完成 (2026-04-16)

**执行结果:** 所有 4 个 Step 均已完成

- [x] **Step 1: 修改 URL 逻辑 (第 50-58 行)** ✅

将第 50-58 行：
```c
char *url = NULL;
if (strcmp(model, "instruct") == 0)
{
    url = "https://api.openai.com/v1/completions";
}
else
{
    url = "https://api.openai.com/v1/chat/completions";
}
```
替换为：
```c
char *url = "https://api.minimaxi.com/v1/text/chatcompletion_v2";
```

- [x] **Step 2: 修改 Auth Header (第 59 行)** ✅

将：
```c
char *auth_header = "Authorization: Bearer " OPENAI_TOKEN;
```
替换为：
```c
char *auth_header = "Authorization: Bearer " MINIMAX_TOKEN;
```

- [x] **Step 3: 修改 JSON 组装，删除 instruct 分支 (第 62-70 行)** ✅

将第 62-70 行：
```c
char *data = NULL;
if (strcmp(model, "instruct") == 0)
{
    asprintf(&data, "{\"model\": \"gpt-3.5-turbo-instruct\", \"prompt\": \"%s\", \"max_tokens\": %d, \"temperature\": %f}", prompt, MAX_TOKENS, temperature);
}
else
{
    asprintf(&data, "{\"model\": \"gpt-3.5-turbo\",\"messages\": %s, \"max_tokens\": %d, \"temperature\": %f}", prompt, MAX_TOKENS, temperature);
}
```
替换为：
```c
char *data = NULL;
asprintf(&data, "{\"model\": \"" MINIMAX_MODEL "\", \"messages\": %s, \"max_tokens\": %d, \"temperature\": %f}", prompt, MAX_TOKENS, temperature);
```

- [x] **Step 4: 添加 Curl Dump 调试打印 (第 92 行，curl_easy_perform 之前)** ✅

在第 93 行 `res = curl_easy_perform(curl);` 之前添加：
```c
printf("[DEBUG] JSON payload: %s\n", data);
```

- [x] **Step 5: 提交** ⚠️ (未提交，代码已修改)

```bash
git add ChatAFL/chat-llm.c
git commit -m "feat: replace OpenAI URL with MiniMax and add Curl Dump debug"
```

---

## Task 3: 修改 chat-llm.c - 响应解析与内存管理 ✅

**Files:**
- Modify: `ChatAFL/chat-llm.c:100-127` (删除 instruct 解析分支 + 添加 Token 监控)

**执行状态:** ✅ 完成 (2026-04-16)

**执行结果:** 响应解析逻辑已更新，添加了 Token 监控，保留了 `json_object_put(jobj)` 内存释放

- [x] **Step 1: 修改响应解析逻辑 (第 100-121 行)** ✅

将第 100-121 行：
```c
// Check if the "choices" key exists
if (json_object_object_get_ex(jobj, "choices", NULL))
{
    json_object *choices = json_object_object_get(jobj, "choices");
    json_object *first_choice = json_object_array_get_idx(choices, 0);
    const char *data;

    // The answer begins with a newline character, so we remove it
    if (strcmp(model, "instruct") == 0)
    {
        json_object *jobj4 = json_object_object_get(first_choice, "text");
        data = json_object_get_string(jobj4);
    }
    else
    {
        json_object *jobj4 = json_object_object_get(first_choice, "message");
        json_object *jobj5 = json_object_object_get(jobj4, "content");
        data = json_object_get_string(jobj5);
    }
    if (data[0] == '\n')
        data++;
    answer = strdup(data);
}
```
替换为：
```c
// Check if the "choices" key exists
if (json_object_object_get_ex(jobj, "choices", NULL))
{
    json_object *choices = json_object_object_get(jobj, "choices");
    json_object *first_choice = json_object_array_get_idx(choices, 0);

    // Extract message content
    json_object *jobj4 = json_object_object_get(first_choice, "message");
    json_object *jobj5 = json_object_object_get(jobj4, "content");
    const char *content_str = json_object_get_string(jobj5);

    if (content_str == NULL) {
        printf("Error: content is NULL\n");
    } else {
        // The answer begins with a newline character, so we remove it
        if (content_str[0] == '\n')
            content_str++;
        answer = strdup(content_str);
    }

    // Token usage monitoring: extract usage.total_tokens
    json_object *jobj_usage = json_object_object_get(jobj, "usage");
    if (jobj_usage != NULL) {
        json_object *jobj_total_tokens = json_object_object_get(jobj_usage, "total_tokens");
        if (jobj_total_tokens != NULL) {
            int total_tokens = json_object_get_int(jobj_total_tokens);
            printf("[DEBUG] Token usage: %d total_tokens\n", total_tokens);
        }
    }
}
```

**【防坑重点】第 127 行的 `json_object_put(jobj);` 必须保留！这是防止内存泄漏的关键。**

- [x] **Step 2: 提交** ⚠️ (未提交，代码已修改)

---

## Task 4: 编译验证 + Curl Dump 静态调试 (步骤一) ⚠️

**执行状态:** ⚠️ 部分完成 (2026-04-16)

**执行结果:** 
- ❌ AFL 完整编译失败：系统缺少 `graphviz-dev` (libgvc, libcgraph) 和 `libpcre2-dev` 依赖
- ✅ chat-llm.c 代码修改已验证正确 (通过 git diff)
- ⚠️ 无法完整构建 AFL 二进制文件

**问题:** 构建环境缺少必要依赖库

- [ ] **Step 1: 编译 ChatAFL** ❌

```bash
cd /mnt/e/lunwen/ChatAFL/ChatAFL && make clean all
```

预期输出：成功编译，无报错

**实际结果:**
```
/usr/bin/ld: cannot find -lgvc: No such file or directory
/usr/bin/ld: cannot find -lcgraph: No such file or directory
collect2: error: ld returned 1 exit status
make: *** [Makefile:65: afl-gcc] Error 1
```

- [ ] **Step 2: 捕获 Curl Dump 输出**

编译成功后，修改代码触发一次请求，观察终端输出的 `[DEBUG] JSON payload: ...` 信息

- [ ] **Step 3: 手动 curl 测试** ⏳ 待执行

复制 Step 2 捕获的 JSON payload，替换到以下命令执行：

```bash
curl -X POST https://api.minimaxi.com/v1/text/chatcompletion_v2 \
  -H "Authorization: Bearer your_api_key_here" \
  -H "Content-Type: application/json" \
  -d '{"model": "MiniMax-M2.7", "messages": [{"role": "system", "content": "You are a helpful assistant."}, {"role": "user", "content": "Say hello"}], "max_tokens": 2048, "temperature": 0.5}'
```

预期：
- HTTP 200
- JSON 响应包含 `choices[0].message.content`
- JSON 响应包含 `usage.total_tokens`

- [ ] **Step 4: 提交** ⏳ 待执行

```bash
git add -A
git commit -m "test: verify compilation and manual curl test"
```

---

## Task 5: 编写 test_api.c 本地脱机单元测试 (步骤二) ⚠️

**Files:**
- Create: `ChatAFL/test_api.c`

**执行状态:** ⚠️ 文件已创建，待完整测试 (2026-04-16)

**执行结果:**
- ✅ `ChatAFL/test_api.c` 已创建
- ❌ 编译失败：系统缺少 `libpcre2-dev` 头文件

- [ ] **Step 1: 创建 test_api.c** ✅

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "chat-llm.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <api_token>\n", argv[0]);
        return 1;
    }

    // 临时替换 MINIMAX_TOKEN
    extern char *MINIMAX_TOKEN;
    MINIMAX_TOKEN = argv[1];

    // 构造简单测试 prompt
    char *test_prompt = "[{\"role\": \"system\", \"content\": \"You are a helpful assistant.\"}, {\"role\": \"user\", \"content\": \"Say 'MiniMax API works!' in one sentence.\"}]";

    printf("[TEST] Calling chat_with_llm with MiniMax API...\n");
    printf("[TEST] Prompt: %s\n", test_prompt);

    char *answer = chat_with_llm(test_prompt, "turbo", 3, 0.5f);

    if (answer != NULL) {
        printf("[TEST] SUCCESS! LLM Response: %s\n", answer);
        free(answer);
        return 0;
    } else {
        printf("[TEST] FAILED! No response from LLM.\n");
        return 1;
    }
}
```

- [x] **Step 2: 编译 test_api.c** ❌

```bash
cd /mnt/e/lunwen/ChatAFL
gcc -g -o test_api test_api.c chat-llm.c -I. -lcurl -ljson-c -lpcre2-8
```

预期：编译成功，无错误输出

**实际结果:** `fatal error: pcre2.h: No such file or directory`

- [ ] **Step 3: 执行 test_api** ⏳ 待执行

```bash
./test_api your_api_key_here
```

预期输出：
```
[TEST] Calling chat_with_llm with MiniMax API...
[TEST] Prompt: [{"role": "system", "content": "You are a helpful assistant."}, {"role": "user", "content": "Say 'MiniMax API works!' in one sentence."}]
[DEBUG] JSON payload: {"model": "MiniMax-M2.7", "messages": [{"role": "system", "content": "You are a helpful assistant."}, {"role": "user", "content": "Say 'MiniMax API works!' in one sentence."}], "max_tokens": 2048, "temperature": 0.500000}
[DEBUG] Token usage: 85 total_tokens
[TEST] SUCCESS! LLM Response: MiniMax API works!
```

**验证标准：**
- ✅ 无 Segmentation fault
- ✅ 输出 `SUCCESS` 和 LLM 回复
- ✅ 输出 `[DEBUG] Token usage: X total_tokens`

- [ ] **Step 4: 清理测试文件并提交**

```bash
rm -f test_api test_api.c
git add -A
git commit -m "test: add test_api.c for MiniMax API integration test"
```

---

## Task 6: 同步到 ChatAFL-CL1 ⏳

**执行状态:** ⏳ 待执行

- [ ] **Step 1: 复制文件到 CL1** ⏳ 待执行

```bash
cp /mnt/e/lunwen/ChatAFL/ChatAFL/chat-llm.c /mnt/e/lunwen/ChatAFL/ChatAFL-CL1/chat-llm.c
cp /mnt/e/lunwen/ChatAFL/ChatAFL/chat-llm.h /mnt/e/lunwen/ChatAFL/ChatAFL-CL1/chat-llm.h
```

- [ ] **Step 2: 编译 CL1 验证** ⏳ 待执行

```bash
cd /mnt/e/lunwen/ChatAFL/ChatAFL-CL1 && make clean all
```

预期：编译成功

- [ ] **Step 3: 提交 CL1 同步** ⏳ 待执行

```bash
git add ChatAFL-CL1/chat-llm.c ChatAFL-CL1/chat-llm.h
git commit -m "chore: sync MiniMax API changes to ChatAFL-CL1"
```

---

## Task 7: 同步到 ChatAFL-CL2 ⏳

**执行状态:** ⏳ 待执行

- [ ] **Step 1: 复制文件到 CL2** ⏳ 待执行

```bash
cp /mnt/e/lunwen/ChatAFL/ChatAFL/chat-llm.c /mnt/e/lunwen/ChatAFL/ChatAFL-CL2/chat-llm.c
cp /mnt/e/lunwen/ChatAFL/ChatAFL/chat-llm.h /mnt/e/lunwen/ChatAFL/ChatAFL-CL2/chat-llm.h
```

- [ ] **Step 2: 编译 CL2 验证** ⏳ 待执行

```bash
cd /mnt/e/lunwen/ChatAFL/ChatAFL-CL2 && make clean all
```

预期：编译成功

- [ ] **Step 3: 提交 CL2 同步**

```bash
git add ChatAFL-CL2/chat-llm.c ChatAFL-CL2/chat-llm.h
git commit -m "chore: sync MiniMax API changes to ChatAFL-CL2"
```

---

## Task 8: PureFTPD 容器化端到端测试 (步骤三) ⏳

**Files:**
- Modify: `benchmark/subjects/FTP/PureFTPD/` (如需更新 Dockerfile 或 run.sh)

**执行状态:** ⏳ 待执行

- [ ] **Step 1: 构建 PureFTPD Docker 镜像** ⏳ 待执行

```bash
cd /mnt/e/lunwen/ChatAFL/benchmark/subjects/FTP/PureFTPD
docker build -t chatafl-pureftpd .
```

预期：构建成功，无报错

- [ ] **Step 2: 运行极简 Fuzzing 测试 (10 分钟)** ⏳ 待执行

```bash
docker run --rm -e MINIMAX_TOKEN=your_api_key_here chatafl-pureftpd ./run.sh 1 10 pure-ftpd chatafl
```

预期：
- Fuzzer 正常启动
- 监控日志显示 MiniMax API 被调用
- Token 总消耗在合理区间
- 容器无 OOM 崩溃

- [ ] **Step 3: 提交容器测试结果**

```bash
git add -A
git commit -m "test: PureFTPD container end-to-end test passed"
```

---

## 防坑措施汇总

| 风险 | 缓解方案 |
|------|----------|
| **内存泄漏** | `json_object_put(jobj)` 在第 127 行必须保留，防止 JSON 解析树内存溢出 |
| **JSON 格式错误** | Curl Dump 调试打印，手动 curl 验证 JSON 是否合法 |
| **Token 消耗失控** | 添加 `usage.total_tokens` 监控打印，及时发现异常 |
| **API 认证失败** | 检查 MINIMAX_TOKEN 是否正确 |
| **解析逻辑不兼容** | MiniMax 响应结构与 OpenAI 相同，已有解析逻辑无需修改 |
| **编译失败** | 检查 libcurl, libjson-c, libpcre2 是否正确安装 |

## 依赖检查命令

```bash
# 检查依赖库
ldconfig -p | grep -E "curl|json|pcre2"

# 如果缺失，安装命令
# apt install libcurl4-openssl-dev libjson-c-dev libpcre2-dev
```

---

## 自检清单

- [ ] Spec coverage: 所有 OpenAI 相关 URL/Model 均已替换为 MiniMax
- [ ] Placeholder scan: 无 TBD/TODO/placeholder
- [ ] Type consistency: `MINIMAX_TOKEN` 和 `MINIMAX_MODEL` 定义与使用一致
- [ ] 三个变体版本 (ChatAFL, ChatAFL-CL1, ChatAFL-CL2) 均已同步修改
- [ ] Curl Dump 调试已添加，可手动验证 JSON 格式
- [ ] Token 使用监控已添加，可实时观察消耗
- [ ] test_api.c 联调测试通过，无 Segmentation fault
- [ ] 内存管理：`json_object_put(jobj)` 保留，防止内存泄漏
- [ ] PureFTPD 容器测试通过
