# 发现与研究

## 项目概述

**ChatAFL** — 基于 AFLNet 的 LLM 引导协议模糊测试工具
**论文名**：LLMProFuzz (LPF) — 发表于《信息网络安全》2025年第12期

### 核心组件
- **aflnet/** — 修改版 AFLNet，输出状态和状态转换
- **ChatAFL/** — 完整实现（语法提取 + 种子富集 + 结构感知变异）
- **ChatAFL-CL1/** — 仅结构感知变异
- **ChatAFL-CL2/** — 结构感知变异 + 种子富集
- **benchmark/** — ProfuzzBench 修改版

### 三个 LLM 策略（论文贡献点）
1. **语法提取**：少样本提示工程自动提取协议语法模板
2. **种子富集**：基于历史漏洞特征生成高价值初始用例
3. **结构感知变异**： plateau 突破（阈值 Max=300）

### 依赖项
- Docker
- Bash
- Python3 + pandas + matplotlib
- MiniMax API Key（已适配）

### 核心脚本
| 脚本 | 功能 |
|------|------|
| setup.sh | 构建 Docker 镜像 |
| run.sh | 运行模糊测试（创建容器执行 fuzzing） |
| analyze.sh | 分析结果并生成图表 |
| clean.sh | 清理环境 |

### LLM 集成（已适配 MiniMax）

**核心文件：** `ChatAFL/chat-llm.c` 中的 `chat_with_llm()` 函数

**当前配置（MiniMax）：**
```c
// API Endpoint
url = "https://api.minimax.io/v1/completions";  // instruct 模型
url = "https://api.minimax.io/v1/chat/completions";  // chat 模型

// API Key 从环境变量读取
char *api_key = getenv("MINIMAX_API_KEY");

// 模型名称
"{\"model\": \"MiniMax-M2.7\", ...}"
```

### 论文 ProFuzzBench 评估协议
- ProFTPD (FTP)
- Live555 (RTSP)
- Lighttpd (HTTP)
- Exim (SMTP)
- Kamailio (SIP)

### 论文实验结果（12小时测试）
| 对比基准 | 代码覆盖率提升 | 状态覆盖率提升 |
|----------|----------------|----------------|
| vs AFLNet | +4.59% | +19.78% |
| vs StateAFL | +5.44% | +3.32% |

## 运行环境分析

### 当前环境
- Windows + WSL2 + Docker Desktop
- 已成功构建：PureFTPD, Live555, Kamailio 镜像

### 关键参数
- **Plateau 阈值**：Max = 300（论文验证值）
- **测试时长**：论文每协议 12 小时 × 3 次
