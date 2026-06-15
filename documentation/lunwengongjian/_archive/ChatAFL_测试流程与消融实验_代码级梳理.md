# ChatAFL 测试流程与消融实验 — 代码级梳理

> 适用范围：本文基于当前仓库中的 `ChatAFL/`、`ChatAFL-V0/V1/V2/`、`benchmark/` 实现，描述的是在基准论文 ChatAFL 思路上加入验证层、反馈重试、post-gain 归因后的工程版本。验证层、反馈重试、MiniMax/OpenAI-compatible LLM endpoint 适配等内容属于当前仓库扩展，不应直接表述为基准论文原始机制。

## 目录

1. [启动流程与实验组织](#1-启动流程与实验组织)
2. [格式生成（Grammar Setup）](#2-格式生成grammar-setup)
3. [种子扩展（Seed Enrichment）](#3-种子扩展seed-enrichment)
4. [突破瓶颈（Stall Breaking）](#4-突破瓶颈stall-breaking)
5. [LLM 上下文控制设计](#5-llm-上下文控制设计)
6. [校验框架设计](#6-校验框架设计)
7. [重试与反馈机制](#7-重试与反馈机制)
8. [消融实验设计](#8-消融实验设计)
9. [数据流总览](#9-数据流总览)

---

## 1. 启动流程与实验组织

### 1.1 入口：`./run.sh`

**文件**: [run.sh](run.sh)

```bash
./run.sh NUM_CONTAINERS TIMEOUT TARGET FUZZER [EXPERIMENT_ID]
# 示例: ./run.sh 10 60 live555 chatafl mytest
```

**执行流程**:

```
./run.sh
  │
  ├─ 1. 参数解析 (run.sh:3-14)
  │     NUM_CONTAINERS=$1, TIMEOUT=$2*60(分钟→秒), TARGET=$3, FUZZER=$4, EXPERIMENT_ID=$5
  │     SKIPCOUNT=${SKIPCOUNT:-1}, TEST_TIMEOUT=${TEST_TIMEOUT:-5000}
  │
  ├─ 2. 环境变量透传 (run.sh:22)
  │     LLM_URL, LLM_TOKEN, LLM_MODEL
  │     AFL_LLM_VALIDATION, AFL_LLM_VALIDATION_PERMISSIVE, AFL_LLM_VALIDATION_STRICT
  │     AFL_LLM_POST_GAIN, AFL_LLM_FEEDBACK, AFL_LLM_FEEDBACK_MAX_RETRIES
  │     AFL_LLM_SKIP_STARTUP
  │
  └─ 3. 调用 profuzzbench_exec_all.sh ${TARGET} ${FUZZER}
```

### 1.2 三层脚本调用链

```
run.sh                                              # 第1层：参数解析、环境变量透传
  └─ profuzzbench_exec_all.sh                       # 第2层：目标/模糊器组合匹配
       └─ profuzzbench_exec.sh                      # 第3层：Docker容器编排
            └─ docker run ... ". env.sh && run ..." # 容器内：source env.sh → 启动fuzzer
```

**profuzzbench_exec_all.sh** ([benchmark/scripts/execution/profuzzbench_exec_all.sh](benchmark/scripts/execution/profuzzbench_exec_all.sh)):
- 遍历 TARGET_LIST 和 FUZZER_LIST 的所有组合
- 为每个组合调用 `profuzzbench_exec.sh`，传递 AFL 参数（如 `-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none`）

**profuzzbench_exec.sh** ([benchmark/scripts/execution/profuzzbench_exec.sh](benchmark/scripts/execution/profuzzbench_exec.sh)):
- 创建 NUM_CONTAINERS 个 Docker 容器（每个限制 1 CPU）
- 容器内执行：`cd /home/ubuntu/experiments && . /home/ubuntu/experiments/${FUZZER}/env.sh && run ${FUZZER} ${OUTDIR} '${OPTIONS}' ${TIMEOUT} ${SKIPCOUNT}`
- `env.sh` 在容器内被 source，设置所有 LLM 相关环境变量

### 1.3 afl-fuzz main() 初始化序列

**文件**: [ChatAFL/afl-fuzz.c:10696](ChatAFL/afl-fuzz.c#L10696)

```
main()
  │
  ├─ 1. 选项解析 (10697-11087)
  │     关键标志: -P(协议) -E(状态感知) -q(状态选择) -s(种子选择) -R(区域变异)
  │
  ├─ 2. LLM 标志加载 (11137-11156)
  │     AFL_LLM_VALIDATION       → afl_llm_validation
  │     AFL_LLM_VALIDATION_STRICT → afl_llm_validation_strict
  │     AFL_LLM_POST_GAIN        → afl_llm_post_gain
  │     AFL_LLM_FEEDBACK         → afl_llm_feedback (validation开启时自动启用)
  │     AFL_LLM_FEEDBACK_MAX_RETRIES → afl_llm_feedback_max_retries
  │     AFL_LLM_SKIP_STARTUP     → afl_llm_skip_startup
  │
  ├─ 3. 核心设置 (11108-11196)
  │     信号处理、SHM设置、IPSM(协议状态机)设置、目录/FD设置
  │
  ├─ 4. LLM 初始化 (11200-11216)  ← 三大组件的入口
  │     if (protocol_selected) {
  │       if (afl_llm_validation) init_validation_log(out_dir);
  │       if (!afl_llm_skip_startup) {
  │         setup_llm_grammars();    ← 格式生成
  │         enrich_testcases();      ← 种子扩展
  │       } else {
  │         load_cached_grammars();  ← 从磁盘加载缓存的语法
  │       }
  │     }
  │
  ├─ 5. 测试用例加载 (11217-11220)
  │     read_testcases() → load_auto() → pivot_inputs()
  │
  ├─ 6. 干跑 + 主循环 (11242-11406)
  │     perform_dry_run() → cull_queue() → show_init_stats()
  │     while(1) {
  │       target_state_id = choose_target_state();  ← 状态选择
  │       cull_queue();
  │       selected_seed = choose_seed();            ← 种子选择
  │       fuzz_one(use_argv);                       ← 核心变异循环
  │     }
  │
  └─ 7. 收尾：收集覆盖率，打包结果
```

### 1.4 项目目录结构

```
ChatAFL/
├── ChatAFL/                    # 完整版（所有LLM特性开启）
│   ├── afl-fuzz.c              # 主模糊器
│   ├── chat-llm.c              # LLM交互层
│   ├── chat-llm.h              # LLM接口头文件
│   ├── llm-validator.c         # 校验框架
│   ├── llm-validator.h         # 校验头文件
│   ├── config.h                # 关键常量定义
│   ├── env.sh                  # 环境变量配置
│   └── Makefile                # 构建系统
├── ChatAFL-V0/                 # 消融变体V0：无校验基线
│   ├── afl-fuzz.c              # 旧版源码（无校验/反馈/post-gain代码）
│   └── env.sh                  # VALIDATION=0, FEEDBACK=0
├── ChatAFL-V1/                 # 消融变体V1：格式校验 + 自动反馈
│   ├── afl-fuzz.c              # 与ChatAFL相同
│   └── env.sh                  # VALIDATION=1, STRICT=0
├── ChatAFL-V2/                 # 消融变体V2：完整校验
│   ├── afl-fuzz.c              # 与ChatAFL相同
│   └── env.sh                  # VALIDATION=1, STRICT=1
├── aflnet/                     # 基线AFLNet（无LLM）
├── benchmark/                  # 基准测试基础设施
│   ├── scripts/execution/      # 执行脚本
│   ├── scripts/analysis/       # 分析脚本
│   └── subjects/               # 测试目标（RTSP/Live555, FTP/PureFTPD等）
└── run.sh                      # 顶层入口
```

---

## 2. 格式生成（Grammar Setup）

### 2.1 概述

格式生成是 ChatAFL 的第一个 LLM 调用点，在启动阶段执行。其目标是让 LLM 学习目标协议的消息模板，生成 PCRE2 正则表达式模式，用于后续的语法引导变异。

**入口**: [afl-fuzz.c:695](ChatAFL/afl-fuzz.c#L695) `setup_llm_grammars()`

### 2.2 完整流程

```
setup_llm_grammars()
  │
  ├─ 1. 构造 Few-shot Prompt
  │     construct_prompt_for_templates(protocol_name, &first_question)
  │     [chat-llm.c:316]
  │     ├─ 提供2个示例：RTSP DESCRIBE + HTTP GET
  │     └─ 要求LLM输出目标协议的所有客户端请求模板（JSON数组格式）
  │
  ├─ 2. 一致性过滤循环 × 5次 (TEMPLATE_CONSISTENCY_COUNT=5)
  │     for (iter = 0; iter < 5; iter++) {
  │       │
  │       ├─ 2a. 首次提问
  │       │     templates_answer = chat_with_llm(prompt, "turbo", GRAMMAR_RETRIES=5, 0.5)
  │       │
  │       ├─ 2b. 多轮补充
  │       │     remaining_prompt = construct_prompt_for_remaining_templates()
  │       │     [chat-llm.c:359]  ← 4条消息：system + user + assistant(首次回答) + user(追问)
  │       │     remaining_templates = chat_with_llm(remaining_prompt, "turbo", 5, 0.5)
  │       │
  │       ├─ 2c. 合并结果
  │       │     combined = templates_answer + remaining_templates
  │       │
  │       ├─ 2d. 解析JSON模板
  │       │     extract_message_grammars(combined, grammar_list)
  │       │     [chat-llm.c:559]  ← 提取 [...] 格式的JSON数组
  │       │
  │       └─ 2e. 更新一致性表
  │             consistency_table[header][field]++  ← 统计每个字段出现次数
  │     }
  │
  ├─ 3. 提取PCRE2模式
  │     for (each unique header in consistency_table) {
  │       extract_message_pattern(header_str, field_table, patterns)
  │       [chat-llm.c:647]
  │       ├─ 构建 header_pattern（匹配消息头）
  │       ├─ 构建 fields_pattern（匹配可变字段）
  │       └─ 过滤：字段出现次数 >= TEMPLATE_CONSISTENCY_COUNT/2 + 1 = 3
  │     }
  │
  ├─ 4. 校验模式（如果 AFL_LLM_VALIDATION_STRICT 开启）
  │     validate_grammar_pattern(message_type, protocol)
  │     [llm-validator.c:521]
  │     └─ 检查消息类型是否在协议有效方法列表中
  │
  ├─ 5. 存储到 protocol_patterns（klist of pcre2_code** pairs）
  │     存储消息类型名到 message_types_set
  │
  └─ 6. 缓存到 protocol-grammars/pattern-* 文件
```

### 2.3 Prompt 构造细节

**首次提问** (`construct_prompt_for_templates`, [chat-llm.c:316](ChatAFL/chat-llm.c#L316)):

```json
[
  {"role": "system", "content": "You are a helpful assistant."},
  {"role": "user", "content": "For the RTSP protocol, the DESCRIBE client request template is:\n
   \"DESCRIBE\": [\"DESCRIBE <<VALUE>>\\r\\n\", \"CSeq: <<VALUE>>\\r\\n\", ...]\n
   For the HTTP protocol, the GET client request template is:\n
   \"GET\": [\"GET <<VALUE>>\\r\\n\"]\n
   For the <PROTOCOL> protocol, output ALL client request templates.
   Output ONLY strictly valid JSON. NO markdown, NO code blocks, NO explanations."}
]
```

**多轮补充** (`construct_prompt_for_remaining_templates`, [chat-llm.c:359](ChatAFL/chat-llm.c#L359)):

```json
[
  {"role": "system", "content": "You are a helpful assistant."},
  {"role": "user", "content": "<首次提问>"},
  {"role": "assistant", "content": "<首次回答>"},
  {"role": "user", "content": "For the <PROTOCOL> protocol, other templates of client requests are:"}
]
```

### 2.4 关键常量

| 常量 | 值 | 文件:行号 | 说明 |
|------|-----|-----------|------|
| `TEMPLATE_CONSISTENCY_COUNT` | 5 | chat-llm.h:24 | 一致性过滤轮数 |
| `GRAMMAR_RETRIES` | 5 | chat-llm.h:30 | 每轮LLM调用重试次数 |
| `MAX_TOKENS` | 4096 | chat-llm.c:16 | 每次LLM调用的最大token数 |

---

## 3. 种子扩展（Seed Enrichment）

### 3.1 概述

种子扩展是 ChatAFL 的第二个 LLM 调用点，在格式生成之后、主模糊循环之前执行。其目标是识别现有种子中缺失的消息类型，并让 LLM 将缺失类型插入到合适位置。

**入口**: [afl-fuzz.c:3172](ChatAFL/afl-fuzz.c#L3172) `enrich_testcases()`

### 3.2 完整流程

```
enrich_testcases()
  │
  └─ get_seeds_with_messsage_types(in_dir, message_types_set)
     [afl-fuzz.c:2956]
     │
     ├─ 1. 遍历输入目录的所有种子文件（跳过已enriched的文件）
     │
     ├─ 2. 对每个种子文件：
     │     ├─ 读取文件内容
     │     ├─ 解析为区域序列：extract_requests() → regions[]
     │     ├─ 识别已有的消息类型（提取每个region的header）
     │     └─ 计算缺失类型：messages = message_types_set - 已有类型
     │
     ├─ 3. 限制缺失类型数量
     │     while (kh_size(messages) > MAX_ENRICHMENT_CORPUS_SIZE=10)
     │       随机删除多余的类型
     │
     ├─ 4. 生成类型组合
     │     message_combinations(messages, MAX_ENRICHMENT_MESSAGE_TYPES=2)
     │     [chat-llm.c:1064]  ← 生成所有 C(n,1) + C(n,2) 的组合
     │
     ├─ 5. 对每个组合调用LLM
     │     for (each subset) {
     │       │
     │       ├─ 5a. 构造Prompt
     │       │     enrich_sequence_with_prompt(content, subset, &prompt)
     │       │     [chat-llm.c:1102]
     │       │     prompt = "The following is one sequence of client requests:\n
     │       │               <sequence>\n
     │       │               Please add the <missing_types> client requests
     │       │               in the proper locations..."
     │       │     ※ Token预算：allowed_tokens = MAX_TOKENS - template_len - missing_fields_len
     │       │     ※ 序列长度超过预算时截断
     │       │
     │       ├─ 5b. 调用LLM
     │       │     response = chat_with_llm(prompt, "instruct", ENRICHMENT_RETRIES=5, 0.5)
     │       │
     │       ├─ 5c. 后处理
     │       │     if (response缺少\r\n\r\n) 自动追加
     │       │
     │       ├─ 5d. 去重检查
     │       │     if (enriched内容 == 原始内容) 跳过
     │       │
     │       ├─ 5e. 格式化
     │       │     unescape_string(response) → format_request_message()
     │       │
     │       ├─ 5f. 校验（如果 AFL_LLM_VALIDATION 开启）
     │       │     validate_llm_sequence_with_mode(protocol, ENRICHMENT, msg, ctx, mode)
     │       │     [llm-validator.c:434]
     │       │     if (校验失败 && AFL_LLM_FEEDBACK) {
     │       │       recovered = llm_feedback_retry_enrichment(...)
     │       │       [chat-llm.c:1315]
     │       │     }
     │       │
     │       └─ 5g. 写入种子文件
     │             write_new_seeds("enriched_{i}_{filename}", content)
     │     }
     │
     └─ 6. 清理：销毁hash表和组合列表
```

### 3.3 Prompt 构造细节

**`enrich_sequence_with_prompt()`** ([chat-llm.c:1102](ChatAFL/chat-llm.c#L1102)):

```json
[
  {"role": "user", "content": "The following is one sequence of client requests:\n
   <JSON转义的序列>\n
   Please add the SETUP, PLAY client requests in the proper locations, and the
   modified sequence of client requests is:
   (System constraint: Output ONLY the raw protocol commands. NO markdown code blocks,
   NO explanations, NO intro text. ONLY output the raw TCP sequence.)"}
]
```

**Token预算管理**:
```c
int allowed_tokens = (MAX_TOKENS - strlen(prompt_template) - missing_fields_len);
if (sequence_len > allowed_tokens) {
    sequence_len = allowed_tokens;  // 截断序列以适应token预算
}
```

### 3.4 关键常量

| 常量 | 值 | 文件:行号 | 说明 |
|------|-----|-----------|------|
| `MAX_ENRICHMENT_MESSAGE_TYPES` | 2 | chat-llm.h:42 | 每次插入的最大消息类型数 |
| `MAX_ENRICHMENT_CORPUS_SIZE` | 10 | chat-llm.h:45 | 缺失类型的最大数量 |
| `ENRICHMENT_RETRIES` | 5 | chat-llm.h:36 | 每次LLM调用的重试次数 |

---

## 4. 突破瓶颈（Stall Breaking）

### 4.1 概述

突破瓶颈是 ChatAFL 的第三个 LLM 调用点，在主模糊循环中动态触发。当模糊器连续多次执行未发现新的覆盖率时，判定为"停滞"，此时调用 LLM 生成新的协议消息以突破瓶颈。

**触发条件检测**: [afl-fuzz.c:7255](ChatAFL/afl-fuzz.c#L7255)

### 4.2 触发机制

```c
// 两个计数器（afl-fuzz.c:402-404）
static u32 uninteresting_times = 0;  // 连续无新发现的执行次数
static u32 chat_times = 0;           // LLM调用总次数

// 每次 common_fuzz_stuff() 返回时更新
if (new_coverage_found) {
    uninteresting_times = 0;  // 重置
} else {
    uninteresting_times++;    // 递增
}

// 触发条件（afl-fuzz.c:7255）
if (uninteresting_times >= UNINTERESTING_THRESHOLD &&  // 512
    chat_times < CHATTING_THRESHOLD)                   // 512
{
    // 调用LLM突破瓶颈
}
```

| 常量 | 值 | 文件:行号 | 说明 |
|------|-----|-----------|------|
| `UNINTERESTING_THRESHOLD` | 512 | config.h:76 | 触发LLM调用的阈值 |
| `CHATTING_THRESHOLD` | 512 | config.h:77 | LLM调用总次数上限 |

### 4.3 完整流程

```
fuzz_one() [afl-fuzz.c:7079]
  │
  ├─ 1. M1/M2/M3 区域选择 (7154-7230)
  │     每个种子是协议消息序列，分解为：
  │     M1: 前缀消息（发送但不变异）
  │     M2: 候选子序列（变异目标）
  │     M3: 后缀消息
  │
  │     if (state_aware_mode) {
  │       基于 target_state_id 选择M2
  │       遍历regions找到匹配target_state_id的区域
  │     } else {
  │       随机选择M2
  │     }
  │
  ├─ 2. 瓶颈检测与LLM干预 (7255-7516)
  │     if (uninteresting_times >= 512 && chat_times < 512) {
  │       │
  │       ├─ 2a. 加载服务器响应
  │       │     responses = get_responses_from_file("responses-ipsm/id:xxx")
  │       │
  │       ├─ 2b. 构建通信历史
  │       │     for (each request-response pair before M2) {
  │       │       history += request_json + response_json
  │       │     }
  │       │
  │       ├─ 2c. 裁剪历史以适应token预算
  │       │     if (history_len > HISTORY_PROMPT_LENGTH=4000)
  │       │       截断到最新4000字符
  │       │     if (examples_len > EXAMPLES_PROMPT_LENGTH=2000)
  │       │       截断到最新2000字符
  │       │
  │       ├─ 2d. 构造Prompt
  │       │     construct_prompt_stall(protocol_name, examples, history)
  │       │     [chat-llm.c:286]
  │       │
  │       ├─ 2e. 调用LLM
  │       │     stall_response = chat_with_llm(prompt, "turbo", STALL_RETRIES=2, 1.5)
  │       │     ※ 温度1.5（高于格式生成的0.5），增加多样性
  │       │
  │       ├─ 2f. 提取消息
  │       │     extract_stalled_message(response)  [chat-llm.c:401]
  │       │     format_request_message(message)     [chat-llm.c:425]
  │       │     └─ 确保\r\n结尾，添加\r\n\r\n终止符
  │       │
  │       ├─ 2g. 校验（如果 AFL_LLM_VALIDATION 开启）
  │       │     validate_llm_message_with_mode(protocol, STALL, msg, ctx, mode)
  │       │     [llm-validator.c:395]
  │       │
  │       ├─ 2h. 反馈重试（如果校验失败且 AFL_LLM_FEEDBACK 开启）
  │       │     recovered = llm_feedback_retry_stall(protocol, msg, error, mode, max_retries)
  │       │     [chat-llm.c:1260]
  │       │
  │       ├─ 2i. 执行消息
  │       │     common_fuzz_stuff(argv, stall_message, strlen(stall_message))
  │       │
  │       └─ 2j. 记录交互
  │             保存prompt和response到 stall-interactions/ 目录
  │     }
  │
  ├─ 3. 缓冲区构造 (7518-7550)
  │     提取M2区域数据到 in_buf/out_buf
  │
  ├─ 4. 确定性变异 (7562-8605)
  │     标准AFL的bitflip、arithmetic、dictionary阶段
  │
  ├─ 5. Havoc阶段 (8610-9292)
  │     ┌─ Epsilon-greedy策略 (8654-8675)
  │     │  epsilon = random(0, 1)
  │     │  if (epsilon < 0.5) → 探索模式：整个缓冲区可变异
  │     │  else               → 利用模式：使用LLM学习的语法范围
  │     │
  │     └─ 25种变异操作 (8695-9243)
  │        case 0-14:  标准AFL变异（bitflip、interesting values、arithmetic）
  │        case 15-16: 字典变异（extras、a_extras）
  │        case 17-18: 区域替换（用其他种子的区域替换当前区域）
  │        case 19-20: 区域插入（在当前区域前插入）
  │        case 21-22: 区域插入（在当前区域后插入）
  │        case 23-24: 区域复制（复制当前区域）
  │        ※ case 17-24 仅在 -R 标志开启时可用
  │
  └─ 6. 拼接 (9294-9397)
        最后手段：将当前输入与随机队列条目拼接，然后重新进入havoc
```

### 4.4 Prompt 构造细节

**`construct_prompt_stall()`** ([chat-llm.c:286](ChatAFL/chat-llm.c#L286)):

```json
[
  {"role": "system", "content": "You are a network protocol expert assistant. Output ONLY the raw required protocol command."},
  {"role": "user", "content": "In the <PROTOCOL> protocol, the communication history between the <PROTOCOL> client and the <PROTOCOL> server is as follows.
   The next proper client request that can affect the server's state are:\n\n
   Desired format of real client requests:\n<examples>
   Communication History:\n\"\"\"<history>\"\"\"\n
   (System constraint: Output exactly ONE complete client request message. MUST include headers and MUST end with \\r\\n\\r\\n. NO markdown, NO formatting, NO explanations.)"}
]
```

**历史裁剪策略**:
```c
// afl-fuzz.c:7330-7341
if (history_len > HISTORY_PROMPT_LENGTH) {
    int offset = history_len - HISTORY_PROMPT_LENGTH;
    if (history[offset - 1] == '\\') offset++;  // 避免截断转义序列
    history = ck_strdup(history + offset);       // 保留最新的部分
}
```

---

## 5. LLM 上下文控制设计

### 5.1 五种 Prompt 构造函数

| 函数 | 文件:行号 | 上下文策略 | 温度 | 用途 |
|------|-----------|-----------|------|------|
| `construct_prompt_for_templates()` | chat-llm.c:316 | Few-shot（2个示例） | 0.5 | 语法模板提取 |
| `construct_prompt_for_remaining_templates()` | chat-llm.c:359 | 多轮对话（4条消息） | 0.5 | 补充遗漏模板 |
| `construct_prompt_for_protocol_message_types()` | chat-llm.c:480 | 单轮（instruct模型） | - | 获取消息类型列表 |
| `enrich_sequence_with_prompt()` | chat-llm.c:1102 | 单轮 + 序列注入 | 0.5 | 种子扩展 |
| `construct_prompt_stall()` | chat-llm.c:286 | system + user + 历史注入 | 1.5 | 突破瓶颈 |

### 5.2 Token 预算管理

| 常量 | 值 | 文件:行号 | 说明 |
|------|-----|-----------|------|
| `MAX_TOKENS` | 4096 | chat-llm.c:16 | 每次LLM调用的最大输出token |
| `MAX_PROMPT_LENGTH` | 8192 | chat-llm.h:19 | prompt最大长度 |
| `EXAMPLES_PROMPT_LENGTH` | 2000 | chat-llm.h:20 | 示例请求最大长度 |
| `HISTORY_PROMPT_LENGTH` | 4000 | chat-llm.h:21 | 通信历史最大长度 |
| `EXAMPLE_SEQUENCE_PROMPT_LENGTH` | 4000 | chat-llm.h:22 | 示例序列最大长度 |

### 5.3 上下文裁剪策略

**通信历史裁剪** (afl-fuzz.c:7330-7341):
- 保留最新的 `HISTORY_PROMPT_LENGTH` (4000) 字符
- 截断时检查转义序列边界（避免截断 `\\`）

**示例裁剪** (afl-fuzz.c:7343-7354):
- 保留最新的 `EXAMPLES_PROMPT_LENGTH` (2000) 字符

**种子序列裁剪** (chat-llm.c:1146-1150):
- `allowed_tokens = MAX_TOKENS - template_len - missing_fields_len`
- 超出时截断序列

### 5.4 温度参数选择

| 场景 | 温度 | 理由 |
|------|------|------|
| 语法模板提取 | 0.5 | 需要稳定、一致的输出 |
| 种子扩展 | 0.5 | 需要准确的消息插入 |
| 突破瓶颈 | 1.5 | 需要多样性，探索未知状态 |
| 反馈重试 | 0.7 | 平衡多样性与准确性 |

---

## 6. 校验框架设计

### 6.1 三级校验模式

**定义**: [llm-validator.h:31-35](ChatAFL/llm-validator.h#L31)

```c
typedef enum {
  LLM_VALIDATE_DISABLED = 0,    // 跳过所有校验
  LLM_VALIDATE_FORMAT_ONLY,     // 仅检查结构格式
  LLM_VALIDATE_FULL             // 格式 + 语法 + 上下文
} llm_validation_mode_t;
```

**模式选择逻辑** (运行时):
```c
if (!afl_llm_validation)           → LLM_VALIDATE_DISABLED
else if (!afl_llm_validation_strict) → LLM_VALIDATE_FORMAT_ONLY
else                                 → LLM_VALIDATE_FULL
```

### 6.2 四种校验结果

**定义**: [llm-validator.h:15-21](ChatAFL/llm-validator.h#L15)

```c
typedef enum {
  LLM_VALID_OK = 0,           // 通过
  LLM_VALID_FORMAT_FAIL,      // 结构问题：缺少CRLF、非打印字符、请求行格式错误
  LLM_VALID_GRAMMAR_FAIL,     // 语法问题：无效方法/命令、缺少必需头字段
  LLM_VALID_CONTEXT_FAIL,     // 上下文问题：协议状态机违规
  LLM_VALID_NO_GAIN           // 有效但无新覆盖率（仅用于执行结果分类）
} llm_validation_result_t;
```

### 6.3 校验流程

**单消息校验** (`validate_llm_message_with_mode`, [llm-validator.c:395](ChatAFL/llm-validator.c#L395)):

```
validate_llm_message_with_mode(protocol, stage, msg, ctx, mode)
  │
  ├─ mode == DISABLED → 直接返回 OK
  │
  ├─ 根据协议分发：
  │   ├─ RTSP → validate_rtsp_format_only() 或 validate_rtsp_full()
  │   ├─ FTP  → validate_ftp_format_only()  或 validate_ftp_full()
  │   └─ HTTP → validate_http_format_only() 或 validate_http_full()
  │
  └─ 返回校验结果
```

**序列校验** (`validate_llm_sequence_with_mode`, [llm-validator.c:434](ChatAFL/llm-validator.c#L434)):

```
validate_llm_sequence_with_mode(protocol, stage, seq, ctx, mode)
  │
  ├─ 使用协议特定的区域提取器拆分序列
  │   RTSP → extract_requests_rtsp()
  │   FTP  → extract_requests_ftp()
  │   HTTP → extract_requests_http()
  │
  ├─ 对每个区域独立校验
  │   for (each region) {
  │     result = validate_llm_message_with_mode(...)
  │     if (result != OK) return result;
  │   }
  │
  └─ 全部通过 → 返回 OK
```

### 6.4 协议特定校验规则

#### RTSP 校验 (`validate_rtsp_full`, [llm-validator.c:239](ChatAFL/llm-validator.c#L239))

| 校验层级 | 检查项 | 失败类型 |
|----------|--------|----------|
| Format | 必须包含 `\r\n\r\n` 终止符 | FORMAT_FAIL |
| Format | 仅包含可打印字符 | FORMAT_FAIL |
| Format | 请求行格式：`METHOD URI VERSION` | FORMAT_FAIL |
| Format | 头字段格式：`Key: Value` | FORMAT_FAIL |
| Grammar | 版本必须是 `RTSP/1.0` | GRAMMAR_FAIL |
| Grammar | URI 必须是 `rtsp://`、`rtsps://` 或 `*` | GRAMMAR_FAIL |
| Grammar | 方法必须在有效列表中 | GRAMMAR_FAIL |
| Grammar | 必须包含 `CSeq` 头 | GRAMMAR_FAIL |
| Grammar | SETUP 必须包含 `Transport` 头 | GRAMMAR_FAIL |
| Context | PLAY/PAUSE/TEARDOWN 必须包含 `Session` 头 | CONTEXT_FAIL |

**有效RTSP方法**: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, ANNOUNCE, RECORD, GET_PARAMETER, SET_PARAMETER, REDIRECT

#### FTP 校验 (`validate_ftp_full`, [llm-validator.c:288](ChatAFL/llm-validator.c#L288))

| 校验层级 | 检查项 | 失败类型 |
|----------|--------|----------|
| Format | 每行必须以 `\r\n` 结尾 | FORMAT_FAIL |
| Format | 仅包含可打印字符 | FORMAT_FAIL |
| Grammar | 命令必须在有效列表中 | GRAMMAR_FAIL |
| Context | PASS 必须在 USER 之后 | CONTEXT_FAIL |
| Context | RETR/STOR/LIST 必须在认证之后 | CONTEXT_FAIL |

**有效FTP命令**: USER, PASS, PWD, CWD, CDUP, LIST, NLST, RETR, STOR, APPE, DELE, RNFR, RNTO, MKD, RMD, SITE, SYST, STAT, HELP, NOOP, QUIT, PASV, PORT, TYPE, MODE, STRU, REST

**FTP状态机**:
```
USER → has_user=1
PASS → if (has_user) is_authed=1
RETR/STOR/LIST → requires is_authed=1
```

#### HTTP 校验 (`validate_http_full`, [llm-validator.c:340](ChatAFL/llm-validator.c#L340))

| 校验层级 | 检查项 | 失败类型 |
|----------|--------|----------|
| Format | 必须包含 `\r\n\r\n` 终止符 | FORMAT_FAIL |
| Format | 仅包含可打印字符 | FORMAT_FAIL |
| Format | 请求行格式：`METHOD URI VERSION` | FORMAT_FAIL |
| Grammar | 方法必须在有效列表中 | GRAMMAR_FAIL |
| Grammar | 版本必须是 HTTP/1.0 或 HTTP/1.1 | GRAMMAR_FAIL |
| Grammar | Content-Length 必须与实际body长度匹配 | GRAMMAR_FAIL |

**有效HTTP方法**: GET, POST, PUT, DELETE, HEAD, OPTIONS, PATCH, TRACE, CONNECT

### 6.5 校验日志

**记录结构** (`llm_validation_record_t`, [llm-validator.h:70-87](ChatAFL/llm-validator.h#L70)):

```c
typedef struct {
  llm_generation_stage_t stage;      // GRAMMAR / ENRICHMENT / STALL
  llm_validation_result_t result;    // OK / FORMAT_FAIL / GRAMMAR_FAIL / CONTEXT_FAIL
  char reason[128];                  // 失败原因描述
  u32 protocol_type;                 // RTSP / FTP / HTTP
  u32 seed_id;                       // 种子ID
  u32 llm_call_id;                   // LLM调用ID
  u32 input_bytes;                   // 输入字节数
  u32 normalized_bytes;              // 归一化后字节数
  u32 region_count;                  // 区域数量
  u32 state_count;                   // 状态数量
  char response_code_seq[128];       // 响应码序列
  u8 has_new_cov;                    // 是否有新覆盖率（post-execution）
  u8 has_new_state;                  // 是否有新状态（post-execution）
  u8 has_new_transition;             // 是否有新转换（post-execution）
  u8 fault;                          // 是否有故障
  u64 exec_us;                       // 执行耗时（微秒）
} llm_validation_record_t;
```

**日志文件**: `llm-validation/grammar.csv`, `llm-validation/enrichment.csv`, `llm-validation/stall.csv`

---

## 7. 重试与反馈机制

### 7.1 三层重试架构

```
┌─────────────────────────────────────────────────────────────┐
│ 第3层：调用方重试（chat_with_llm 的 tries 参数）             │
│   GRAMMAR_RETRIES=5, STALL_RETRIES=2, ENRICHMENT_RETRIES=5  │
│   └─ HTTP请求失败或返回NULL时重试                             │
│                                                              │
│ ┌───────────────────────────────────────────────────────────┐│
│ │ 第2层：反馈重试（llm_feedback_retry_*）                    ││
│ │   LLM_FEEDBACK_MAX_RETRIES_DEFAULT=3                      ││
│ │   └─ 校验失败时，构造包含错误信息的feedback prompt重试      ││
│ │                                                           ││
│ │ ┌────────────────────────────────────────────────────────┐││
│ │ │ 第1层：HTTP级重试（chat_with_llm 内部 do-while）       │││
│ │ │   └─ curl请求失败或JSON解析失败时重试                   │││
│ │ │      失败时 sleep(1-2秒)                               │││
│ │ └────────────────────────────────────────────────────────┘││
│ └───────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

### 7.2 第1层：HTTP级重试

**位置**: [chat-llm.c:109-187](ChatAFL/chat-llm.c#L109)

```c
do {
    // 执行curl请求
    res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        // 解析JSON响应
        answer = clean_llm_response(data_str);
        if (!answer) sleep(1);      // 解析失败，等1秒
    } else {
        sleep(2);                    // 请求失败，等2秒
    }
} while ((res != CURLE_OK || answer == NULL) && (--tries > 0));
```

### 7.3 第2层：反馈重试

**Stall反馈重试** (`llm_feedback_retry_stall`, [chat-llm.c:1260](ChatAFL/chat-llm.c#L1260)):

```
llm_feedback_retry_stall(protocol, failed_message, error, mode, max_retries=3)
  │
  ├─ 获取错误详情
  │   error_detail = get_validation_error_detail(protocol, error, failed_message)
  │   [llm-validator.c:548]
  │
  └─ 重试循环 (最多3次)
      for (attempt = 0; attempt < max_retries; attempt++) {
        │
        ├─ 构造反馈Prompt
        │   construct_feedback_prompt_stall(protocol, failed_message, error_detail)
        │   [chat-llm.c:1182]
        │
        ├─ 调用LLM（温度0.7，单次尝试）
        │   response = chat_with_llm(feedback_prompt, "instruct", 1, 0.7)
        │
        ├─ 提取 + 格式化 + 校验
        │   extract_stalled_message() → format_request_message() → validate_llm_message_with_mode()
        │
        └─ if (校验通过) return recovered_message;
      }
      return NULL;  // 所有重试失败
```

**Enrichment反馈重试** (`llm_feedback_retry_enrichment`, [chat-llm.c:1315](ChatAFL/chat-llm.c#L1315)):

```
llm_feedback_retry_enrichment(protocol, failed_message, error, mode, max_retries=3)
  │
  └─ 重试循环 (最多3次)
      for (attempt = 0; attempt < max_retries; attempt++) {
        │
        ├─ 构造反馈Prompt
        │   construct_feedback_prompt_enrichment(protocol, failed_message, error_detail)
        │   [chat-llm.c:1221]
        │
        ├─ 调用LLM（温度0.7，单次尝试）
        │   response = chat_with_llm(feedback_prompt, "instruct", 1, 0.7)
        │
        ├─ 处理 + 校验
        │   unescape_string() → format_request_message() → validate_llm_sequence_with_mode()
        │
        └─ if (校验通过) return recovered_message;
      }
      return NULL;
```

### 7.4 反馈 Prompt 构造

**Stall反馈** (`construct_feedback_prompt_stall`, [chat-llm.c:1182](ChatAFL/chat-llm.c#L1182)):

```json
[
  {"role": "system", "content": "You are a network protocol expert assistant. The previous message failed validation. Fix the described issue and output ONLY the raw corrected protocol message."},
  {"role": "user", "content": "The following <PROTOCOL> client request message was generated but FAILED validation:\n
   --- BEGIN FAILED MESSAGE ---\n<failed_message>\n--- END FAILED MESSAGE ---\n
   Validation error: <error_detail>\n
   Please generate a CORRECTED <PROTOCOL> client request that fixes the above error.
   Output exactly ONE complete client request message.
   MUST include proper headers and MUST end with \\r\\n\\r\\n.
   NO markdown, NO formatting, NO explanations."}
]
```

**Enrichment反馈** (`construct_feedback_prompt_enrichment`, [chat-llm.c:1221](ChatAFL/chat-llm.c#L1221)):

```json
[
  {"role": "system", "content": "You are a network protocol expert assistant. The previous message sequence failed validation. Fix the described issue and output ONLY the raw corrected protocol messages."},
  {"role": "user", "content": "The following <PROTOCOL> message sequence was generated but FAILED validation:\n
   --- BEGIN FAILED SEQUENCE ---\n<failed_message>\n--- END FAILED SEQUENCE ---\n
   Validation error: <error_detail>\n
   Please generate a CORRECTED <PROTOCOL> message sequence that fixes the above error.
   Each message MUST end with \\r\\n\\r\\n.
   NO markdown, NO formatting, NO explanations."}
]
```

### 7.5 错误详情生成

**`get_validation_error_detail()`** ([llm-validator.c:548](ChatAFL/llm-validator.c#L548)):

根据协议和失败类型生成人类可读的错误描述：

| 失败类型 | RTSP 示例 | FTP 示例 | HTTP 示例 |
|----------|-----------|----------|-----------|
| FORMAT_FAIL | "Message missing CRLF CRLF terminator" | "Message format invalid" | "Message missing CRLF CRLF terminator" |
| GRAMMAR_FAIL | "Method 'X' is not valid for RTSP. Valid methods: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, ..." | "Command 'X' is not valid for FTP. Valid commands: USER, PASS, PWD, ..." | "Method 'X' is not valid for HTTP. Valid methods: GET, POST, PUT, ..." |
| CONTEXT_FAIL | "PLAY/PAUSE/TEARDOWN require a Session header obtained from a prior SETUP request" | "PASS requires prior USER command; RETR/STOR/LIST require prior authentication" | "request requires Host header" |

---

## 8. 消融实验设计

### 8.1 四个变体配置对比

| 特性 | V0 | V1 | V2 | ChatAFL |
|------|----|----|----|---------|
| **源码** | 独立旧版源码 | 与ChatAFL主体相同 | 与ChatAFL主体相同 | 完整版 |
| `AFL_LLM_VALIDATION` | 0 | 1 | 1 | 1 |
| `AFL_LLM_VALIDATION_STRICT` | 0 | 0 | 1 | 1 |
| `AFL_LLM_POST_GAIN` | 0 | 0 | 0 | 1 |
| `AFL_LLM_FEEDBACK` | 0 | (自动启用) | (自动启用) | 1 |
| `AFL_LLM_FEEDBACK_MAX_RETRIES` | - | 3(默认) | 3(默认) | 3 |
| **校验模式** | DISABLED | FORMAT_ONLY | FULL | FULL |
| **反馈重试** | 禁用 | 启用 | 启用 | 启用 |
| **Post-gain归因** | 禁用 | 禁用 | 禁用 | 启用 |

### 8.2 各变体的 env.sh 配置

**V0** ([ChatAFL-V0/env.sh](ChatAFL-V0/env.sh)):
```bash
AFL_LLM_VALIDATION=0
AFL_LLM_VALIDATION_STRICT=0
AFL_LLM_POST_GAIN=0
AFL_LLM_FEEDBACK=0
```

**V1** ([ChatAFL-V1/env.sh](ChatAFL-V1/env.sh)):
```bash
AFL_LLM_VALIDATION=1
AFL_LLM_VALIDATION_STRICT=0
AFL_LLM_POST_GAIN=0
AFL_LLM_SKIP_STARTUP=0
```

注意：V1 的 `env.sh` 没有显式设置 `AFL_LLM_FEEDBACK=1`，但 `afl-fuzz.c` 会在 `AFL_LLM_VALIDATION=1` 且外部没有显式设置 `AFL_LLM_FEEDBACK=0` 时自动开启 feedback retry。

**V2** ([ChatAFL-V2/env.sh](ChatAFL-V2/env.sh)):
```bash
AFL_LLM_VALIDATION=1
AFL_LLM_VALIDATION_STRICT=1
AFL_LLM_POST_GAIN=0
```

注意：V2 同样依赖代码中的自动开启逻辑启用 feedback retry；因此 V2 的默认行为是“完整验证 + 自动反馈”，不是“只有完整验证”。

**ChatAFL** ([ChatAFL/env.sh](ChatAFL/env.sh)):
```bash
AFL_LLM_VALIDATION=1
AFL_LLM_VALIDATION_STRICT=1
AFL_LLM_POST_GAIN=1
AFL_LLM_FEEDBACK=1
AFL_LLM_FEEDBACK_MAX_RETRIES=3
AFL_LLM_SKIP_STARTUP=0
```

### 8.3 运行时行为差异

| 行为 | V0 | V1 | V2 | ChatAFL |
|------|----|----|----|----|
| **LLM输出处理** | 直接使用 | 校验后使用 | 校验后使用 | 校验后使用 |
| **格式校验** | 跳过 | 执行 | 执行 | 执行 |
| **语法校验** | 跳过 | 跳过 | 执行 | 执行 |
| **上下文校验** | 跳过 | 跳过 | 执行 | 执行 |
| **校验失败处理** | 忽略，直接执行 | 反馈重试 | 反馈重试 | 反馈重试 |
| **重试耗尽后** | N/A | 丢弃 | 丢弃 | 丢弃 |
| **执行后归因** | 跳过 | 跳过 | 跳过 | 记录new_cov/new_state/new_transition |

### 8.4 代码差异分析

**V0 vs ChatAFL**:
- V0 的 `afl-fuzz.c` 是独立旧版代码，缺少 `#include "llm-validator.h"`、验证日志、反馈重试、post-gain 归因等当前扩展逻辑
- V0 的运行配置显式关闭 `AFL_LLM_VALIDATION`、`AFL_LLM_FEEDBACK` 和 `AFL_LLM_POST_GAIN`

**V1/V2 vs ChatAFL**:
- V1/V2 与 ChatAFL 主体代码接近，核心行为主要通过 `env.sh` 和外部环境变量控制
- 校验代码编译进二进制，但通过 `AFL_LLM_VALIDATION`、`AFL_LLM_VALIDATION_STRICT`、`AFL_LLM_POST_GAIN`、`AFL_LLM_FEEDBACK` 在运行时控制
- 若外部显式设置 `AFL_LLM_FEEDBACK=0`，会覆盖自动开启逻辑

### 8.5 消融实验的设计目标

| 变体 | 消融目标 | 对比关系 |
|------|----------|----------|
| **V0** | LLM无校验/无反馈基线 | V0 vs V1 → 格式验证与自动反馈的组合效果 |
| **V1** | 格式验证 + 自动反馈 | V1 vs V2 → 语法+上下文验证在格式验证基础上的增量效果 |
| **V2** | 完整验证 + 自动反馈（无post-gain） | V2 vs ChatAFL → 主要衡量 post-gain 归因与相关日志/收益分类 |
| **ChatAFL** | 完整系统 | 完整验证 + 自动/显式反馈 + post-gain |

### 8.6 当前消融实验不能直接证明的结论

- 不能单独证明“feedback retry 的独立贡献”，因为 V1/V2 在默认条件下会随 validation 自动启用 feedback。
- 不能把 `V0 vs V1` 简化为“格式验证贡献”；它同时包含格式验证和反馈修复。
- 不能把 `V2 vs ChatAFL` 描述为“feedback 贡献”；两者默认都启用 feedback，差异主要是 `AFL_LLM_POST_GAIN=1`。
- `LLM_VALID_OK` 只表示执行前验证通过，不代表一定产生新覆盖或新状态。
- `LLM_VALID_NO_GAIN` 属于执行后收益分类，不表示 LLM 输出非法。

如需严格拆分验证与反馈，应额外运行：

```bash
AFL_LLM_FEEDBACK=0 ./run.sh ... chatafl-v1 ...
AFL_LLM_FEEDBACK=0 ./run.sh ... chatafl-v2 ...
```

---

## 9. 数据流总览

### 9.1 启动阶段数据流

```
┌─────────────────────────────────────────────────────────────────┐
│                        启动阶段                                  │
│                                                                  │
│  LLM API ──→ construct_prompt_for_templates()                   │
│      │         [chat-llm.c:316]                                  │
│      │                                                           │
│      ▼                                                           │
│  chat_with_llm() × 5轮 × 2次/轮 = 10次LLM调用                  │
│      │         [chat-llm.c:45]                                   │
│      │                                                           │
│      ▼                                                           │
│  clean_llm_response()                                            │
│      │   [chat-llm.c:202]                                        │
│      │   ├─ 拒绝检测（sorry, As an AI, ...）                     │
│      │   ├─ JSON提取                                             │
│      │   ├─ Markdown剥离                                         │ │
│      │   └─ LF→CRLF转换                                          │
│      │                                                           │
│      ▼                                                           │
│  extract_message_grammars()                                      │
│      │   [chat-llm.c:559]                                        │
│      │                                                           │
│      ▼                                                           │
│  consistency_table（一致性过滤）                                  │
│      │   字段出现次数 >= 3 次才保留                               │
│      │                                                           │
│      ▼                                                           │
│  extract_message_pattern()                                       │
│      │   [chat-llm.c:647]                                        │
│      │   └─ 生成PCRE2正则模式                                    │
│      │                                                           │
│      ▼                                                           │
│  protocol_patterns（语法模式库）                                  │
│  message_types_set（消息类型集合）                                │
│                                                                  │
│  ─────────────────────────────────────────────────────────────── │
│                                                                  │
│  LLM API ──→ enrich_sequence_with_prompt()                       │
│      │         [chat-llm.c:1102]                                  │
│      │         for each seed × each missing_type_combination     │
│      │                                                           │
│      ▼                                                           │
│  chat_with_llm() × N次                                           │
│      │                                                           │
│      ▼                                                           │
│  validate_llm_sequence_with_mode()                               │
│      │   [llm-validator.c:434]                                    │
│      │                                                           │
│      ▼ (如果校验失败且feedback开启)                               │
│  llm_feedback_retry_enrichment() × 最多3次                       │
│      │   [chat-llm.c:1315]                                        │
│      │                                                           │
│      ▼                                                           │
│  enriched_* 文件 → 输入目录                                       │
└─────────────────────────────────────────────────────────────────┘
```

### 9.2 运行时数据流（每次 fuzz_one 调用）

```
┌─────────────────────────────────────────────────────────────────┐
│                     运行时（fuzz_one）                            │
│                                                                  │
│  choose_target_state() → choose_seed() → fuzz_one()              │
│      │                                                           │
│      ▼                                                           │
│  M1/M2/M3 区域选择                                               │
│      │                                                           │
│      ▼                                                           │
│  uninteresting_times >= 512?                                     │
│      │                                                           │
│      ├─ YES ──→ 构建通信历史                                     │
│      │          │                                                │
│      │          ▼                                                │
│      │     construct_prompt_stall()                              │
│      │          │   [chat-llm.c:286]                              │
│      │          ▼                                                │
│      │     chat_with_llm(prompt, "turbo", 2, 1.5)                │
│      │          │                                                │
│      │          ▼                                                │
│      │     clean_llm_response()                                  │
│      │          │                                                │
│      │          ▼                                                │
│      │     extract_stalled_message() + format_request_message()  │
│      │          │                                                │
│      │          ▼                                                │
│      │     validate_llm_message_with_mode()                      │
│      │          │                                                │
│      │          ├─ OK ──→ common_fuzz_stuff() 执行               │
│      │          │                                                │
│      │          └─ FAIL ──→ llm_feedback_retry_stall() × 3次     │
│      │                           │                               │
│      │                           ├─ 恢复成功 ──→ 执行            │
│      │                           └─ 耗尽 ──→ 丢弃                │
│      │                                                           │
│      └─ NO ──→ 继续标准变异                                      │
│                 │                                                │
│                 ▼                                                │
│            Epsilon-greedy (50%探索 / 50%利用)                     │
│                 │                                                │
│                 ▼                                                │
│            Havoc (25种变异操作)                                   │
│                 │                                                │
│                 ▼                                                │
│            common_fuzz_stuff() 执行                              │
│                 │                                                │
│                 ▼                                                │
│            save_if_interesting() → add_to_queue()                │
└─────────────────────────────────────────────────────────────────┘
```

### 9.3 关键常量汇总

| 常量 | 值 | 文件:行号 | 说明 |
|------|-----|-----------|------|
| `MAX_TOKENS` | 4096 | chat-llm.c:16 | LLM最大输出token |
| `CONFIDENT_TIMES` | 3 | chat-llm.c:17 | 消息类型一致性查询次数 |
| `MAX_PROMPT_LENGTH` | 8192 | chat-llm.h:19 | prompt最大长度 |
| `EXAMPLES_PROMPT_LENGTH` | 2000 | chat-llm.h:20 | 示例最大长度 |
| `HISTORY_PROMPT_LENGTH` | 4000 | chat-llm.h:21 | 历史最大长度 |
| `EXAMPLE_SEQUENCE_PROMPT_LENGTH` | 4000 | chat-llm.h:22 | 示例序列最大长度 |
| `TEMPLATE_CONSISTENCY_COUNT` | 5 | chat-llm.h:24 | 一致性过滤轮数 |
| `STALL_RETRIES` | 2 | chat-llm.h:27 | 突破瓶颈重试次数 |
| `GRAMMAR_RETRIES` | 5 | chat-llm.h:30 | 语法提取重试次数 |
| `MESSAGE_TYPE_RETRIES` | 5 | chat-llm.h:33 | 消息类型重试次数 |
| `ENRICHMENT_RETRIES` | 5 | chat-llm.h:36 | 种子扩展重试次数 |
| `LLM_FEEDBACK_MAX_RETRIES_DEFAULT` | 3 | chat-llm.h:39 | 反馈重试默认次数 |
| `MAX_ENRICHMENT_MESSAGE_TYPES` | 2 | chat-llm.h:42 | 每次插入的最大类型数 |
| `MAX_ENRICHMENT_CORPUS_SIZE` | 10 | chat-llm.h:45 | 缺失类型最大数量 |
| `EPSILON_CHOICE` | 0.5 | config.h:75 | 探索/利用阈值 |
| `UNINTERESTING_THRESHOLD` | 512 | config.h:76 | 触发LLM的阈值 |
| `CHATTING_THRESHOLD` | 512 | config.h:77 | LLM调用总次数上限 |

### 9.4 三个流程的联系与扩展

```
格式生成 ──→ 种子扩展 ──→ 突破瓶颈
   │            │            │
   │            │            │
   ▼            ▼            ▼
protocol_    enriched_*    新消息执行
patterns     种子文件      + 覆盖率反馈
   │            │            │
   │            │            │
   └────────────┼────────────┘
                │
                ▼
          Havoc阶段的利用模式
          parse_buffer() 使用 protocol_patterns
          识别可变区域 vs 固定头字段
```

**数据依赖关系**:
1. **格式生成** 产出 `protocol_patterns` 和 `message_types_set`
2. **种子扩展** 依赖 `message_types_set` 来识别缺失类型，依赖 `extract_requests` 来解析种子
3. **突破瓶颈** 依赖 `protocol_patterns` 进行语法引导变异（havoc exploit模式）
4. **运行时** 三个流程共享 `protocol_patterns`，havoc阶段的exploit模式直接使用格式生成阶段学习的正则模式

---

## 附录：文件索引

| 文件 | 说明 |
|------|------|
| [run.sh](run.sh) | 顶层入口脚本 |
| [benchmark/scripts/execution/profuzzbench_exec_all.sh](benchmark/scripts/execution/profuzzbench_exec_all.sh) | 目标/模糊器分发 |
| [benchmark/scripts/execution/profuzzbench_exec.sh](benchmark/scripts/execution/profuzzbench_exec.sh) | Docker容器编排 |
| [ChatAFL/afl-fuzz.c](ChatAFL/afl-fuzz.c) | 主模糊器 (main@10696, setup_llm_grammars@695, enrich_testcases@3172, fuzz_one@7079) |
| [ChatAFL/chat-llm.c](ChatAFL/chat-llm.c) | LLM交互层 (chat_with_llm@45, clean_llm_response@202, construct_prompt_stall@286, construct_prompt_for_templates@316, enrich_sequence_with_prompt@1102, llm_feedback_retry_stall@1260, llm_feedback_retry_enrichment@1315) |
| [ChatAFL/chat-llm.h](ChatAFL/chat-llm.h) | LLM接口头文件（常量定义、函数声明） |
| [ChatAFL/llm-validator.c](ChatAFL/llm-validator.c) | 校验框架 (validate_rtsp_full@239, validate_ftp_full@288, validate_http_full@340, validate_llm_message_with_mode@395, validate_llm_sequence_with_mode@434, get_validation_error_detail@548) |
| [ChatAFL/llm-validator.h](ChatAFL/llm-validator.h) | 校验头文件（枚举、结构体定义） |
| [ChatAFL/config.h](ChatAFL/config.h) | 关键常量 (EPSILON_CHOICE@75, UNINTERESTING_THRESHOLD@76, CHATTING_THRESHOLD@77) |
| [ChatAFL/env.sh](ChatAFL/env.sh) | 完整版环境变量配置 |
| [ChatAFL-V0/env.sh](ChatAFL-V0/env.sh) | V0环境变量配置 |
| [ChatAFL-V1/env.sh](ChatAFL-V1/env.sh) | V1环境变量配置 |
| [ChatAFL-V2/env.sh](ChatAFL-V2/env.sh) | V2环境变量配置 |
