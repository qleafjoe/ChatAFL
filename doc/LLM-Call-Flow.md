# ChatAFL LLM 调用完整流程图

## 总览

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Fuzzing 开始前（一次性）                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ S_A: Grammar 提取 (setup_llm_grammars)                          │   │
│  │ 文件: afl-fuzz.c:434                                             │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         Fuzzing 运行中                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ S_B: 种子富化 (get_seeds_with_messsage_types)                    │   │
│  │ 文件: afl-fuzz.c:2625                                             │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ S_C: Stall 处理 (fuzz_one stall block)                           │   │
│  │ 文件: afl-fuzz.c:6846                                             │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 详细调用关系

### 1. S_A: Grammar 提取 (Fuzzing 开始前)

```
setup_llm_grammars()
    │
    ├─ for iter = 0 to TEMPLATE_CONSISTENCY_COUNT(5)-1
    │   │
    │   ├─[1] chat_with_llm() ─→ Grammar-S_A-1
    │   │      问: "这个协议的主要模板有哪些？"
    │   │
    │   └─[2] chat_with_llm() ─→ Grammar-S_A-2
    │          问: "还有什么其他模板？" (含上一轮上下文)
    │
    └─ extract_message_grammars()  // 一致性过滤

LLM 调用次数: 5次迭代 × 2轮 = 10次
```

### 2. S_B: 种子富化 (Fuzzing 开始前)

```
get_seeds_with_messsage_types()
    │
    ├─ get_protocol_message_types()  ─┐
    │      │
    │      └─ for i = 0 to CONFIDENT_TIMES(2)-1
    │             └─[3] chat_with_llm() ─→ MessageTypes-S_B
    │                     问: "这个协议有哪些消息类型？"
    │
    └─ enrich_testcases()
           │
           ├─ for each seed (最多 MAX_ENRICHMENT_CORPUS_SIZE=10)
           │   │
           │   └─[4] chat_with_llm() ─→ Enrichment-S_B
           │          问: "在合适位置添加缺失的消息类型"
           │
           └─ write_new_seeds()  // 写入富化后的种子

LLM 调用次数: 2次(消息类型) + 10次(富化) = 最多12次
```

### 3. S_C: Stall 处理 (Fuzzing 运行中)

```
fuzz_one()
    │
    └─ if (uninteresting_times >= UNINTERESTING_THRESHOLD(512)
    │       && chat_times < CHATTING_THRESHOLD(64))
    │   │
    │   └─[5] chat_with_llm() ─→ Stall-S_C
    │          问: "基于通信历史，生成可能突破 Plateau 的消息"
    │          温度: 1.5 (更高，更随机)
    │          重试: STALL_RETRIES=2
    │
    └─ 正常变异继续...

LLM 调用次数: 最多 CHATTING_THRESHOLD=64 次 (全程累计)
```

---

## LLM 调用汇总表

| Stage | 调用位置 | 触发时机 | 调用次数 | 重试次数 |
|-------|----------|----------|----------|----------|
| Grammar-S_A-1 | afl-fuzz.c:448 | Fuzzing开始前 | 5次 | GRAMMAR_RETRIES=5 |
| Grammar-S_A-2 | afl-fuzz.c:456 | Fuzzing开始前 | 5次 | GRAMMAR_RETRIES=5 |
| MessageTypes-S_B | chat-llm.c:880 | Fuzzing开始前 | 2次 | MESSAGE_TYPE_RETRIES=5 |
| Enrichment-S_B | chat-llm.c:1068 | Fuzzing开始前 | ≤10次 | ENRICHMENT_RETRIES=5 |
| Stall-S_C | afl-fuzz.c:6952 | Fuzzing运行中 | ≤64次 | STALL_RETRIES=2 |

**总计**: 最多约 86 次 LLM 调用

---

## 日志中看到的 Stage 标记

```
[LLM FORMAT] Stage=Grammar-S_A-1: VALID (pure JSON)      # 5次
[LLM FORMAT] Stage=Grammar-S_A-2: VALID (pure JSON)      # 5次
[LLM FORMAT] Stage=MessageTypes-S_B: VALID (non-JSON)    # 2次
[LLM FORMAT] Stage=Enrichment-S_B: VALID (pure JSON)     # ≤10次
[LLM FORMAT] Stage=Stall-S_C: VALID (non-JSON text)       # ≤64次
```
