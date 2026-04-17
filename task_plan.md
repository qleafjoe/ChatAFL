# 任务计划：LLM JSON 响应格式日志记录

## 目标
在代码的各个 LLM 调用阶段，添加日志输出调用大模型返回的 JSON 类型，并判断返回格式是否符合项目要求。

## 背景
MiniMax 模型可能返回 markdown 包装的 JSON（```json...```），导致 `json_tokener_parse` 解析失败。已为所有 Prompt 添加了严格的 JSON 输出指令，但需要验证效果。

## 阶段

### Phase 1: 分析各阶段 LLM 响应解析方式
**状态**: completed
**内容**:
- [x] 分析 `chat_with_llm()` 返回后的处理逻辑
- [x] 确认各阶段期望的 JSON 格式
- [x] 确定需要验证的调用点

### Phase 2: 设计格式验证函数
**状态**: completed
**内容**:
- [x] 创建 `validate_llm_response_format()` 函数
- [x] 检测 markdown 包装（```json...```）
- [x] 检测纯 JSON 格式
- [x] 返回验证结果（是否合规）

### Phase 3: 在 chat_with_llm() 中集成日志
**状态**: completed
**内容**:
- [x] 在 `chat_with_llm()` 返回前调用验证函数
- [x] 输出阶段名称、验证结果、响应摘要
- [x] 区分不同阶段：Grammar/S_B/S_C

### Phase 4: 测试验证
**状态**: completed
**内容**:
- [x] 编译验证
- [x] 确认日志输出格式清晰

## 已完成的修改

### chat-llm.c
- 添加全局变量 `g_current_llm_stage` 追踪调用阶段
- 添加 `validate_llm_response_format()` 函数检测 markdown 包装
- 添加 `set_llm_stage()` 函数设置阶段名称
- 在 `chat_with_llm()` 提取 content 后调用验证

### chat-llm.h
- 声明 `set_llm_stage()` 函数

### afl-fuzz.c
- Grammar-S_A-1: `setup_llm_grammars()` 第一次调用
- Grammar-S_A-2: `setup_llm_grammars()` 第二次调用
- Stall-S_C: `fuzz_one()` stall 处理

### chat-llm.c (内部调用)
- MessageTypes-S_B: `get_protocol_message_types()`
- Enrichment-S_B: `enrich_sequence()`

## 日志输出格式

```
[LLM FORMAT] Stage=Grammar-S_A-1: VALID (pure JSON)
[LLM FORMAT] Stage=Stall-S_C: INVALID (markdown detected) | Preview: ```json
{"USER": ...
```

## 各阶段 LLM 调用分析

| 阶段 | 函数 | 期望 JSON 格式 | 验证重点 |
|------|------|---------------|----------|
| S_A: Grammar | `extract_message_grammars()` | `[...]` 数组格式 | 检测 `[` 开头 |
| S_B: 消息类型 | `get_protocol_message_types()` | 逗号分隔字符串 | 检测是否为纯文本 |
| S_B: 富化 | `enrich_sequence()` | 协议消息序列 | 检测 `\r\n` 格式 |
| S_C: Stall | `extract_stalled_message()` | 单条消息文本 | 检测消息前缀 |

## 注意事项
- 不破坏现有代码逻辑
- 日志输出到 stdout
- 便于调试和问题排查
