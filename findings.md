# LLM JSON 响应格式验证分析

## 当前 chat_with_llm() 处理流程

```
MiniMax API 响应 (JSON)
    ↓
json_tokener_parse() 解析外层 JSON
    ↓
提取 choices[0].message.content 字段
    ↓
返回 content 字符串（answer）
```

## 问题
content 字段可能包含 markdown 包装：
```
```json
{"USER": ["USER <<username>>\r\n"]}
```
```

外层 JSON 解析成功，但 content 内部是 markdown，无法被后续 `extract_message_grammars()` 等函数正确解析。

## 各阶段期望格式

| 阶段 | 函数 | 期望格式 | 解析方式 |
|------|------|----------|----------|
| S_A Grammar | `extract_message_grammars()` | `[...]` 数组 | `json_tokener_parse()` 解析 `[...]` |
| S_B 消息类型 | `get_protocol_message_types()` | 逗号分隔文本 | `strtok()` 按逗号分割 |
| S_B 富化 | `enrich_sequence()` | 协议消息序列 | 直接使用字符串 |
| S_C Stall | `extract_stalled_message()` | 单条消息 | PCRE2 正则提取 |

## 验证策略

需要检测 markdown 包装特征：
- `^```json\n` 开头
- `\n```$` 结尾

如果检测到 markdown，输出警告日志。
