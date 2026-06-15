# ChatAFL 测试流程与消融实验代码梳理

本文从当前仓库代码出发，梳理一次 `./run.sh` 实验从外层脚本进入 Docker、启动 `afl-fuzz`、执行 LLM 三个核心组件、写入候选/队列、记录消融指标的完整流程。

## 1. 总体入口

根目录入口是 `run.sh`，用法为：

```bash
./run.sh NUM_CONTAINERS TIMEOUT_MINUTES TARGET FUZZER [EXPERIMENT_ID]
```

典型示例：

```bash
./run.sh 1 1440 live555 chatafl run_0527
./run.sh 10 1440 live555,pure-ftpd chatafl-v0,chatafl-v1,chatafl-v2,chatafl paper_ablation
```

参数含义：

| 参数 | 含义 |
|------|------|
| `NUM_CONTAINERS` | 每个 target/fuzzer 组合启动多少个 Docker 容器，即重复实验次数 |
| `TIMEOUT` | 单次 fuzzing 时长，单位是分钟；`run.sh` 内部换算成秒 |
| `TARGET` | 目标程序，如 `live555`、`pure-ftpd`、`all` |
| `FUZZER` | fuzzer 版本，如 `aflnet`、`chatafl`、`chatafl-v0/v1/v2` |
| `EXPERIMENT_ID` | 可选，结果目录后缀，例如 `results-live555-run_0527` |

`run.sh` 做三件事：

1. 进入 `benchmark/`。
2. 设置 `PATH`，让 `scripts/execution` 与 `scripts/analysis` 中的脚本可直接调用。
3. 将 LLM 与消融相关环境变量传给 `profuzzbench_exec_all.sh`：
   `LLM_URL`、`LLM_TOKEN`、`LLM_MODEL`、`AFL_LLM_VALIDATION`、`AFL_LLM_VALIDATION_STRICT`、`AFL_LLM_POST_GAIN`、`AFL_LLM_FEEDBACK`、`AFL_LLM_FEEDBACK_MAX_RETRIES`、`AFL_LLM_SKIP_STARTUP` 等。

## 2. `./run.sh` 之后的组织流程

外层脚本链路如下：

```text
run.sh
  -> benchmark/scripts/execution/profuzzbench_exec_all.sh
       -> benchmark/scripts/execution/profuzzbench_exec_common.sh
            -> docker run ...
                 -> /home/ubuntu/experiments/run FUZZER OUTDIR OPTIONS TIMEOUT SKIPCOUNT
                      -> benchmark/subjects/<PROTO>/<TARGET>/run.sh
                           -> /home/ubuntu/<FUZZER>/afl-fuzz ...
                           -> cov_script
                           -> gcovr
                           -> tar result
            -> docker wait
            -> docker cp OUTDIR.tar.gz
```

### 2.1 `profuzzbench_exec_all.sh`

`profuzzbench_exec_all.sh` 负责枚举 `FUZZER_LIST` 与 `TARGET_LIST`。对每一个组合，它根据目标选择协议参数和输出目录。例如：

| 目标 | 协议参数 | 输出目录示例 |
|------|----------|--------------|
| `live555` | `-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none` | `out-live555-chatafl` |
| `pure-ftpd` | `-P FTP -D 10000 -q 3 -s 3 -E -K -t ${TEST_TIMEOUT}+` | `out-pure-ftpd-chatafl` |

如果传入 `EXPERIMENT_ID`，结果根目录会加后缀：

```text
results-live555-${EXPERIMENT_ID}
results-pure-ftpd-${EXPERIMENT_ID}
```

### 2.2 `profuzzbench_exec_common.sh`

`profuzzbench_exec_common.sh` 按 `NUM_CONTAINERS` 循环启动容器：

```text
docker run --cpus=1
  -e LLM_URL=...
  -e LLM_TOKEN=...
  -e LLM_MODEL=...
  -e AFL_LLM_VALIDATION=...
  ...
  DOCIMAGE
  /bin/bash -c "
    cd /home/ubuntu/experiments &&
    if [ -f /home/ubuntu/experiments/${FUZZER}/env.sh ]; then
      . /home/ubuntu/experiments/${FUZZER}/env.sh
    fi &&
    run ${FUZZER} ${OUTDIR} '${OPTIONS}' ${TIMEOUT} ${SKIPCOUNT}
  "
```

这里有一个关键点：容器启动时先透传外层环境变量，再 source 当前 fuzzer 目录下的 `env.sh`。因此消融变体的默认行为主要由各自的 `env.sh` 决定，但外层也可以用环境变量覆盖。

容器结束后，脚本执行：

1. `docker wait` 等所有重复实验完成。
2. `docker cp` 从容器复制 `/home/ubuntu/experiments/${OUTDIR}.tar.gz`。
3. 保存为 `${SAVETO}/${OUTDIR}_${index}.tar.gz`。

### 2.3 目标目录 `run.sh`

以 Live555 为例，目标脚本会：

1. 进入目标构建目录：
   `cd /home/ubuntu/experiments/live/testProgs`
2. 执行 fuzzing：

```text
timeout -k 2s --preserve-status $TIMEOUT \
  /home/ubuntu/${FUZZER}/afl-fuzz \
  -d -i ${INPUTS} -x ${WORKDIR}/rtsp.dict \
  -o $OUTDIR -N tcp://127.0.0.1/8554 \
  $OPTIONS ./testOnDemandRTSPServer 8554
```

3. fuzzing 结束后进入 gcov 版本目录跑 `cov_script`。
4. 用 `gcovr` 生成 HTML 覆盖率。
5. 把 `$OUTDIR` 打包成 `${OUTDIR}.tar.gz`，交给外层 `docker cp`。

## 3. `afl-fuzz` 启动后的代码流程

`ChatAFL/afl-fuzz.c` 中，和 LLM 相关的全局状态包括：

```c
klist_t(rang) *protocol_patterns;
khash_t(strSet) *message_types_set;
char *protocol_name;

u8 afl_llm_validation;
u8 afl_llm_validation_permissive;
u8 afl_llm_validation_strict;
u8 afl_llm_post_gain;
u8 afl_llm_feedback;
u32 afl_llm_feedback_max_retries;
u8 afl_llm_skip_startup;
u32 chat_times;
u32 uninteresting_times;
```

启动时主要流程：

```text
main()
  -> 解析 AFL 参数与协议参数 -P
  -> 读取 AFL_LLM_* 环境变量
  -> setup_ipsm()
  -> setup_dirs_fds()
       创建 queue、replayable-queue、protocol-grammars、stall-interactions 等目录
  -> 如果选择了协议：
       protocol_patterns = kl_init(rang)
       message_types_set = kh_init(strSet)
       init_validation_log(out_dir)  // 如果 AFL_LLM_VALIDATION=1
       if AFL_LLM_SKIP_STARTUP=0:
         setup_llm_grammars()
         enrich_testcases()
       else:
         load_cached_grammars()
  -> read_testcases()
  -> pivot_inputs()
  -> fuzz 主循环
```

因此，三个 LLM 组成部分的时间关系是：

```text
启动期 1：格式/文法生成 setup_llm_grammars()
启动期 2：种子扩展 enrich_testcases()
常规 fuzzing：AFLNet 状态感知变异
停滞期：达到 uninteresting_times 阈值后触发 stall breaking
```

## 4. LLM 调用通用机制

所有 LLM 调用都经过 `ChatAFL/chat-llm.c::chat_with_llm()`。

### 4.1 API 与模型配置

运行时从环境变量读取：

| 环境变量 | 用途 |
|----------|------|
| `LLM_URL` | OpenAI-compatible / MiniMax-compatible chat endpoint |
| `LLM_TOKEN` | API token |
| `LLM_MODEL` | 模型名 |

如果未设置，代码中有默认 MiniMax endpoint、token 和模型名。

### 4.2 请求上下文格式

`chat_with_llm()` 会把 prompt 解析为 JSON messages 数组。如果传入的 prompt 不是合法 messages 数组，就退化包装成：

```json
[
  {"role": "user", "content": "..."}
]
```

最终请求体包含：

```json
{
  "model": "<LLM_MODEL>",
  "messages": [...],
  "max_tokens": 4096,
  "temperature": 0.5
}
```

### 4.3 通用重试

`chat_with_llm(prompt, model, tries, temperature)` 的 `tries` 是 API/解析层重试，不是验证失败反馈重试。

当前主要次数：

| 阶段 | 宏 | 次数 | temperature |
|------|----|------|-------------|
| 格式/文法生成 | `GRAMMAR_RETRIES` | 5 | 0.5 |
| 种子扩展 | `ENRICHMENT_RETRIES` | 5 | 0.5 |
| 突破停滞 | `STALL_RETRIES` | 2 | 1.5 |
| 验证失败反馈 | `AFL_LLM_FEEDBACK_MAX_RETRIES` | 默认 3 | 0.7 |

### 4.4 输出清洗

`clean_llm_response()` 会做：

1. 拒答关键词过滤，如 `sorry`、`As an AI`、`policy`。
2. 如果输出看起来包含 JSON object/array，则优先截取 `{...}` 或 `[...]`，服务于 grammar extraction。
3. 对 raw protocol 输出，去掉 markdown fence、首尾空白和反引号。
4. 将裸 `\n` 转成 `\r\n`，适配 RTSP/HTTP 等协议。

## 5. 组成部分一：格式/文法生成

代码入口：

```text
afl-fuzz.c::setup_llm_grammars()
chat-llm.c::construct_prompt_for_templates()
chat-llm.c::construct_prompt_for_remaining_templates()
chat-llm.c::extract_message_grammars()
chat-llm.c::extract_message_pattern()
```

### 5.1 设计目标

让 LLM 为当前协议生成客户端请求模板，并转成 AFLNet 可用于结构感知变异的 PCRE pattern。

产物：

```text
out_dir/protocol-grammars/llm-grammar-output-0..4
out_dir/protocol-grammars/pattern-0..N
protocol_patterns
message_types_set
```

其中：

| 数据结构 | 用途 |
|----------|------|
| `protocol_patterns` | 保存编译后的 header/body PCRE pattern，供后续 mutation 找可变字段 |
| `message_types_set` | 保存 LLM 识别出的消息类型，供种子扩展判断缺失消息 |

### 5.2 Prompt 上下文如何构造

第一轮 prompt：

1. system：`You are a helpful assistant.`
2. user：给出 RTSP DESCRIBE 和 HTTP GET 的模板示例，然后要求输出当前协议所有客户端请求模板。
3. 强约束：`Output ONLY strictly valid JSON. NO markdown, NO code blocks, NO explanations.`

第二轮 prompt：

1. system：同上。
2. user：第一轮问题。
3. assistant：第一轮回答。
4. user：`For the <protocol> protocol, other templates of client requests are:`

这让第二轮 LLM 在第一轮结果基础上补充遗漏模板。

### 5.3 尝试次数与自洽性

外层循环：

```c
TEMPLATE_CONSISTENCY_COUNT = 5
```

每一轮包含两次 LLM 调用：

```text
第一次：生成主要模板，最多 GRAMMAR_RETRIES=5 次 API/解析重试
第二次：基于第一次问答补充 remaining templates，最多 GRAMMAR_RETRIES=5 次
```

因此启动期 grammar 阶段理论上最多：

```text
5 * 2 * 5 = 50 次 API 尝试
```

注意这里的 50 是最坏情况下的 API/解析重试上限；正常每轮成功则是 10 次 LLM 调用。

### 5.4 如何控制输出格式正确

代码有三层控制：

1. Prompt 层：要求严格 JSON、无 markdown、无解释。
2. 清洗层：`clean_llm_response()` 会优先截取 JSON 数组。
3. 解析层：`extract_message_grammars()` 在输出中寻找 `[...]`，用 `json_tokener_parse()` 解析，解析失败的片段跳过。

随后 `extract_message_pattern()` 把数组模板转成 PCRE pattern，写入 `pattern-*` 文件，并把 pattern 放进 `protocol_patterns`。

### 5.5 如何确保可用并加入候选集

在 `AFL_LLM_VALIDATION_STRICT=1` 时，grammar 会做更严格校验：

| 校验 | 代码 | 不通过结果 |
|------|------|------------|
| 消息类型是否合法 | `validate_grammar_pattern()` | `LLM_VALID_GRAMMAR_FAIL` |
| 必需字段是否存在 | `validate_grammar_required_fields()` | `GRAMMAR_FAIL` 或 `CONTEXT_FAIL` |

例如 RTSP：

1. 所有请求必须有 `CSeq`。
2. `SETUP` 必须有 `Transport`。
3. `PLAY/PAUSE/TEARDOWN` 必须有 `Session`，否则属于上下文依赖失败。

如果校验通过，或处于 `AFL_LLM_VALIDATION_PERMISSIVE=1` 宽松记录模式，则：

```text
kh_put(message_types_set, message_type)
kl_push(protocol_patterns, patterns)
```

这就是 grammar 候选集进入后续 fuzzing 的位置。

## 6. 组成部分二：种子扩展

代码入口：

```text
afl-fuzz.c::enrich_testcases()
afl-fuzz.c::get_seeds_with_messsage_types()
chat-llm.c::enrich_sequence_with_prompt()
chat-llm.c::write_new_seeds()
```

### 6.1 设计目标

启动 fuzzing 前，扫描原始 `in_dir` 种子，找出每个 seed 缺失的协议消息类型，让 LLM 把缺失请求插入到合适位置，形成 enriched seeds。

这些 enriched seeds 会写回输入目录，然后由 `read_testcases()` 一并读入 AFL 队列。

### 6.2 上下文如何构造

对每个初始种子：

1. 使用协议解析器 `extract_requests()` 切出请求 region。
2. 从每个请求 region 提取首 token 作为已有消息类型。
3. 用 `message_types_set` 减去已有消息类型，得到缺失集合。
4. 如果缺失集合过大，限制到 `MAX_ENRICHMENT_CORPUS_SIZE=10`。
5. 生成大小不超过 `MAX_ENRICHMENT_MESSAGE_TYPES=2` 的缺失消息组合。
6. 对每个组合调用 LLM。

Prompt 结构：

```text
The following is one sequence of client requests:
<原始 seed 序列，受 MAX_TOKENS 裁剪>
Please add the <missing types> client requests in the proper locations,
and the modified sequence of client requests is:
(System constraint: Output ONLY the raw protocol commands...)
```

`MAX_TOKENS=4096`，代码会按 prompt 模板长度和缺失消息名长度裁剪 seed 内容，避免上下文过长。

### 6.3 尝试次数

每个缺失消息组合调用：

```text
chat_with_llm(..., ENRICHMENT_RETRIES=5, temperature=0.5)
```

若开启反馈重试，验证失败后额外最多：

```text
AFL_LLM_FEEDBACK_MAX_RETRIES=3
```

每次反馈重试内部 `chat_with_llm(..., tries=1, temperature=0.7)`。

### 6.4 输出格式控制

种子扩展使用 raw protocol 输出，不要求 JSON。控制点如下：

1. Prompt 强制：只输出 raw protocol commands，不要 markdown、解释、intro text。
2. `clean_llm_response()` 清洗 markdown、换行。
3. `enrich_sequence_with_prompt()` 如果发现缺少 `\r\n\r\n`，会追加终止符。
4. `unescape_string()` 处理转义。
5. `format_request_message()` 规范化请求。
6. 与原 seed 规范化后比较，如果完全相同则跳过。

### 6.5 如何确保可用并加入候选集

如果开启 `AFL_LLM_VALIDATION=1`：

```text
validate_llm_sequence_with_mode(protocol_name, LLM_STAGE_ENRICHMENT, candidate, ctx, mode)
```

其中 mode 由环境变量决定：

| mode | 条件 |
|------|------|
| `LLM_VALIDATE_DISABLED` | `AFL_LLM_VALIDATION=0` |
| `LLM_VALIDATE_FORMAT_ONLY` | `AFL_LLM_VALIDATION=1` 且 `AFL_LLM_VALIDATION_STRICT=0` |
| `LLM_VALIDATE_FULL` | `AFL_LLM_VALIDATION=1` 且 `AFL_LLM_VALIDATION_STRICT=1` |

如果校验失败：

1. `AFL_LLM_VALIDATION_PERMISSIVE=1`：记录失败，但继续接受。
2. `AFL_LLM_FEEDBACK=1`：构造失败反馈 prompt，最多重试 3 次。
3. 无 permissive 且修复失败：丢弃该候选。

如果最终通过，则：

```text
write_new_seeds(in_dir/enriched_<i>_<original_name>, candidate)
```

随后主流程调用 `read_testcases()`，这些 enriched 文件被 `add_to_queue()` 加入初始 fuzzing 队列。

## 7. 组成部分三：突破瓶颈

代码入口：

```text
afl-fuzz.c::fuzz_one()
chat-llm.c::construct_prompt_stall()
chat-llm.c::extract_stalled_message()
chat-llm.c::format_request_message()
afl-fuzz.c::common_fuzz_stuff()
afl-fuzz.c::save_if_interesting()
```

### 7.1 触发条件

每次常规 fuzz 执行后，`common_fuzz_stuff()` 调用 `save_if_interesting()` 判断是否产生新覆盖/崩溃/状态相关收益：

```text
interesting -> uninteresting_times = 0
not interesting -> uninteresting_times++
```

当满足：

```text
uninteresting_times >= UNINTERESTING_THRESHOLD
chat_times < CHATTING_THRESHOLD
```

就触发 LLM stall breaking。

当前阈值：

```c
UNINTERESTING_THRESHOLD = 512
CHATTING_THRESHOLD = 512
```

也就是说，连续 512 次不产生 interesting 输入后，最多触发 512 次 LLM 突破停滞。

### 7.2 上下文如何构造

stall breaking 会从当前 queue seed 和响应历史中构造 prompt：

1. 读取 `out_dir/responses-ipsm/id:<queue_name>`。
2. 遍历当前消息序列与响应序列。
3. 构造 examples：
   使用前一个请求作为 `Request-1` 和 `Request-2` 示例。
4. 构造 history：
   拼接当前请求/响应历史，并把不可打印字符替换为空格。
5. 限制上下文长度：
   `HISTORY_PROMPT_LENGTH=4000`，`EXAMPLES_PROMPT_LENGTH=2000`。

Prompt 模板大意：

```text
In the <protocol> protocol, the communication history between the client and server is as follows.
The next proper client request that can affect the server's state are:

Desired format of real client requests:
<examples>
Communication History:
"""
<history>
"""
(System constraint: Output exactly ONE complete client request message.
MUST include headers and MUST end with \r\n\r\n.
NO markdown, NO formatting, NO explanations.)
```

这说明 stall 阶段每次只让 LLM 生成一个“下一条可能推进状态的客户端请求”。

### 7.3 尝试次数

主 LLM 调用：

```text
chat_with_llm(stall_prompt, "turbo", STALL_RETRIES=2, temperature=1.5)
```

验证失败反馈：

```text
llm_feedback_retry_stall(..., max_retries=AFL_LLM_FEEDBACK_MAX_RETRIES=3)
```

反馈重试每次只要求 LLM 修正上一条失败消息，不重新带完整 history。

### 7.4 输出格式控制

stall 输出控制链：

1. Prompt 要求“一条完整 client request”，无 markdown，无解释。
2. `clean_llm_response()` 清洗。
3. `extract_stalled_message()` 从响应中抽取请求消息。
4. `format_request_message()` 规范化。
5. `validate_llm_message_with_mode()` 做格式/完整校验。

### 7.5 如何确保可用并进入候选/队列

stall 生成的候选不会先写入 `in_dir`，而是直接执行：

```text
common_fuzz_stuff(argv, stall_message, strlen(stall_message))
```

执行后：

1. `run_target()` 得到 coverage、响应、fault。
2. `save_if_interesting()` 判断是否有新覆盖、新状态转移、crash/hang。
3. 如果 interesting：
   - `save_kl_messages_to_file()` 保存完整消息序列到 `out_dir/queue/id:...`
   - `add_to_queue()` 加入 AFL 队列
   - `update_state_aware_variables()` 更新状态相关信息
   - 保存 replayable 输入到 `out_dir/replayable-queue/`

因此 stall 候选进入主候选集的门槛是执行反馈，而不是仅仅通过格式校验。

## 8. 四类校验与成本感知

代码中的验证结果：

```c
typedef enum {
  LLM_VALID_OK = 0,
  LLM_VALID_FORMAT_FAIL,
  LLM_VALID_GRAMMAR_FAIL,
  LLM_VALID_CONTEXT_FAIL,
  LLM_VALID_NO_GAIN
} llm_validation_result_t;
```

### 8.1 格式校验

控制：

```text
AFL_LLM_VALIDATION=1
AFL_LLM_VALIDATION_STRICT=0
```

检查内容：

| 协议 | 格式要求 |
|------|----------|
| RTSP | 非空、包含 `\r\n\r\n`、可打印字符、请求行可解析、header 行含冒号 |
| FTP | 每行以 `\r\n` 结束、命令非空、可打印字符 |
| HTTP | 非空、包含 `\r\n\r\n`、请求行可解析、header 行含冒号 |

### 8.2 内容/语法校验

控制：

```text
AFL_LLM_VALIDATION=1
AFL_LLM_VALIDATION_STRICT=1
```

检查内容：

| 协议 | 内容/语法要求 |
|------|---------------|
| RTSP | 方法必须属于 RTSP 方法集合，版本为 `RTSP/1.0`，URI 合法，必需 header 存在 |
| FTP | 命令必须属于 FTP 命令集合 |
| HTTP | 方法必须属于 HTTP 方法集合，版本为 `HTTP/1.0` 或 `HTTP/1.1`，`Content-Length` 与 body 长度一致 |

### 8.3 上下文校验

同样由 strict mode 控制，检查跨消息或状态依赖：

| 协议 | 上下文依赖 |
|------|------------|
| RTSP | `PLAY/PAUSE/TEARDOWN` 需要 `Session`，通常来自先前 `SETUP` |
| FTP | `PASS` 需要先有 `USER`；`RETR/STOR/LIST` 需要先认证 |
| HTTP | 当前实现主要记录 `Host` 和 `Content-Length` 上下文，实际强制较少 |

对于 sequence，`validate_llm_sequence_with_mode()` 会先用协议解析器切分多个 request，再按顺序复用同一个 `protocol_context_t` 验证，因此 FTP 的 `USER -> PASS -> LIST` 这类顺序依赖可以被捕获。

### 8.4 成本感知 / 收益归因

控制：

```text
AFL_LLM_POST_GAIN=1
```

当前主要作用于 stall 阶段。执行 LLM 生成的消息前记录：

```text
queued_before
state_before
edge_before
```

执行后在 `fill_post_execution_record()` 中记录：

| 字段 | 来源 |
|------|------|
| `has_new_cov` | `queued_paths > queued_before` 或 `last_llm_exec_interesting` |
| `has_new_state` | `state_ids_count > state_before` |
| `has_new_transition` | `agnedges(ipsm) > edge_before` |
| `fault` | `last_llm_exec_fault` |
| `exec_us` | 本次执行耗时 |
| `response_code_seq` | `extract_response_codes()` 解析响应 |

如果没有新覆盖、新状态、新转移：

```text
classify_llm_execution_gain() -> LLM_VALID_NO_GAIN
```

这不是执行前拦截，而是成本/收益日志，用来回答“这次 LLM 调用是否值得”。

### 8.5 日志产物

开启 `AFL_LLM_VALIDATION=1` 后会创建：

```text
out_dir/llm-validation/grammar.csv
out_dir/llm-validation/enrichment.csv
out_dir/llm-validation/stall.csv
```

CSV 字段：

```text
time,stage,protocol,seed_id,llm_call_id,result,reason,input_bytes,
normalized_bytes,region_count,state_count,response_code_seq,
new_cov,new_state,new_transition,fault,exec_us
```

这些文件可用于统计：

1. LLM 输出接受率。
2. format / grammar / context 失败比例。
3. 每次 LLM 调用带来的 coverage/state/transition 收益。
4. 单位 LLM 调用成本下的收益。

## 9. 错误反馈重复执行上下文

反馈重试只用于：

| 阶段 | 函数 |
|------|------|
| 种子扩展 | `llm_feedback_retry_enrichment()` |
| 突破停滞 | `llm_feedback_retry_stall()` |

grammar 阶段当前没有反馈重试，只做记录/过滤。

### 9.1 反馈 prompt 的上下文

反馈 prompt 不重新拼完整原始上下文，而是给 LLM 三部分：

1. 上一次失败的 message 或 sequence。
2. 具体错误解释，由 `get_validation_error_detail()` 生成。
3. 重新生成约束：只输出修正后的 raw protocol message/sequence。

stall 反馈大意：

```text
The following <protocol> client request message was generated but FAILED validation:
--- BEGIN FAILED MESSAGE ---
<failed_message>
--- END FAILED MESSAGE ---
Validation error: <error_detail>
Please generate a CORRECTED <protocol> client request...
Output exactly ONE complete client request message...
```

enrichment 反馈大意：

```text
The following <protocol> message sequence was generated but FAILED validation:
--- BEGIN FAILED SEQUENCE ---
<failed_sequence>
--- END FAILED SEQUENCE ---
Validation error: <error_detail>
Please generate a CORRECTED <protocol> message sequence...
```

### 9.2 错误解释如何生成

`get_validation_error_detail()` 会根据错误类型和协议生成自然语言反馈：

| 错误 | 示例反馈 |
|------|----------|
| `FORMAT_FAIL` | 缺少 `\r\n\r\n`、请求行 malformed、非打印字符 |
| `GRAMMAR_FAIL` | 方法/命令不属于协议合法集合，或缺少必需 header |
| `CONTEXT_FAIL` | RTSP 的 Session 依赖、FTP 的 USER/PASS/认证顺序依赖 |

### 9.3 修复成功后的处理

每次反馈重试：

1. 调 LLM 一次。
2. 清洗/提取/格式化。
3. 重新调用相同 mode 的 validator。
4. 成功则返回修复后的候选。
5. 失败则继续下一次，直到 `max_retries` 耗尽。

如果修复成功：

| 阶段 | 后续处理 |
|------|----------|
| enrichment | 替换原候选，写入 `in_dir/enriched_*` |
| stall | 替换原候选，进入 `common_fuzz_stuff()` 执行 |

如果修复失败：

| 模式 | 行为 |
|------|------|
| permissive | 记录失败但继续接受原候选 |
| 非 permissive | 丢弃候选 |

## 10. 当前消融实验配置

当前仓库根目录有四个主要变体：

| 变体 | `env.sh` 默认设置 | 实际含义 |
|------|-------------------|----------|
| `chatafl-v0` | `VALIDATION=0 STRICT=0 POST_GAIN=0 FEEDBACK=0` | 原始 LLM 流程，无验证、无反馈、无收益归因 |
| `chatafl-v1` | `VALIDATION=1 STRICT=0 POST_GAIN=0` | 格式校验；注意代码会自动启用 feedback，除非显式 `AFL_LLM_FEEDBACK=0` |
| `chatafl-v2` | `VALIDATION=1 STRICT=1 POST_GAIN=0` | 完整验证；注意代码会自动启用 feedback，除非显式 `AFL_LLM_FEEDBACK=0` |
| `chatafl` | `VALIDATION=1 STRICT=1 POST_GAIN=1 FEEDBACK=1 MAX_RETRIES=3` | 完整验证 + 反馈重试 + stall 后执行收益归因 |

代码事实需要特别注意：

```c
if (getenv("AFL_LLM_FEEDBACK")) {
  afl_llm_feedback = env_flag_enabled("AFL_LLM_FEEDBACK");
} else if (afl_llm_validation) {
  afl_llm_feedback = 1;
}
```

因此，只要 `AFL_LLM_VALIDATION=1` 且没有显式设置 `AFL_LLM_FEEDBACK=0`，反馈重试就会自动打开。

如果论文需要严格区分“验证”和“反馈”，建议额外跑一组：

```bash
AFL_LLM_FEEDBACK=0 ./run.sh ... chatafl-v1 ...
AFL_LLM_FEEDBACK=0 ./run.sh ... chatafl-v2 ...
```

否则当前 `v1/v2` 衡量的是“验证 + 自动反馈”的组合效果。

## 11. 结果目录与后处理

单个容器内的 `out_dir` 典型包含：

```text
queue/
replayable-queue/
replayable-crashes/
replayable-hangs/
protocol-grammars/
stall-interactions/
enrichment-interactions/
llm-validation/
ipsm.dot
plot_data
fuzzer_stats
cov_over_time.csv
cov_html/
```

关键文件：

| 文件/目录 | 含义 |
|-----------|------|
| `protocol-grammars/llm-grammar-output-*` | LLM 原始 grammar 输出 |
| `protocol-grammars/pattern-*` | 编译前的 grammar pattern |
| `enrichment-interactions/` | seed enrichment 的 prompt/response/candidate |
| `stall-interactions/` | stall breaking 的 prompt/response |
| `llm-validation/*.csv` | 验证与收益归因日志 |
| `plot_data` | AFL 运行时序数据，包含 `chat_times` |
| `ipsm.dot` | 推断状态机 |
| `queue/` | 被 AFL 接受的 interesting 输入 |
| `replayable-queue/` | 可重放形式保存的输入 |

外层实验结束后，结果被打包为：

```text
benchmark/results-<target>-<experiment_id>/<outdir>_<rep>.tar.gz
```

分析脚本：

```text
benchmark/scripts/analysis/profuzzbench_generate_csv.sh
benchmark/scripts/analysis/profuzzbench_generate_all.sh
benchmark/scripts/analysis/profuzzbench_plot.py
benchmark/scripts/analysis/profuzzbench_state.py
```

其中 `plot_data` 原始列包含：

```text
unix_time, cycles_done, cur_path, paths_total, pending_total,
pending_favs, map_size, unique_crashes, unique_hangs, max_depth,
execs_per_sec, n_nodes, n_edges, chat_times
```

## 12. 一句话总结目标

当前系统的目标是：在 AFLNet 的状态反馈 fuzzing 主循环外，用 LLM 在启动期生成协议格式知识、补齐初始种子的消息类型多样性，并在 fuzzing 停滞时生成下一条可能推进状态的请求；同时通过格式校验、内容/语法校验、上下文校验和执行后收益归因，过滤或修复无效 LLM 输出，并把真正有效的候选输入加入 AFL 队列或初始种子集合，从而以可控成本提升状态覆盖、转移覆盖和代码覆盖。

