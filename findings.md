# 发现与研究

## 项目概述

**ChatAFL** — 基于 AFLNet 的 LLM 引导协议模糊测试工具，发表于 NDSS 2024

### 核心组件
- **aflnet/** — 修改版 AFLNet，输出状态和状态转换
- **ChatAFL/** — 完整实现（结构感知变异 + 种子丰富化 + 覆盖 plateau 突破）
- **ChatAFL-CL1/** — 仅结构感知变异
- **ChatAFL-CL2/** — 结构感知变异 + 种子丰富化
- **benchmark/** — ProfuzzBench 修改版（文本协议 + Lighttpd 1.4）

### 依赖项
- Docker
- Bash
- Python3 + pandas + matplotlib
- OPENAI_API_KEY（用于 LLM 调用）

### 核心脚本
| 脚本 | 功能 |
|------|------|
| deps.sh | 安装系统依赖 |
| setup.sh | 构建 Docker 镜像（使用 OPENAI_API_KEY） |
| run.sh | 运行模糊测试（创建容器执行 fuzzing） |
| analyze.sh | 分析结果并生成图表 |
| clean.sh | 清理环境 |

### LLM 集成分析

**核心文件：** `chat-llm.c` 中的 `chat_with_llm()` 函数

**当前硬编码问题：**
```c
// chat-llm.h line 15
#define OPENAI_TOKEN "1"  // ← API Key 硬编码为占位符 "1"！

// chat-llm.c lines 51-58
if (strcmp(model, "instruct") == 0) {
    url = "https://api.openai.com/v1/completions";
} else {
    url = "https://api.openai.com/v1/chat/completions";
}

// chat-llm.c lines 63-70
// 模型也是硬编码
"{\"model\": \"gpt-3.5-turbo-instruct\", ...}"
"{\"model\": \"gpt-3.5-turbo\", ...}"
```

**适配国内 LLM 需要修改：**
1. API Endpoint：
   - MiniMax: `https://api.minimax.chat/v1/...`
   - DeepSeek: `https://api.deepseek.com/v1/...`
   - 阿里云百炼: `https://dashscope.aliyuncs.com/v1/...`

2. API Key：从环境变量读取，而非硬编码

3. 模型名称：改为对应的模型 ID

### 支持的协议/Subjects
- FTP (ProFTPD, PureFTPD, BFTPD, LightFTP)
- SIP (Kamailio)
- RTSP (Live555)
- SMTP (Exim)
- HTTP (Lighttpd 1.4)
- DAAP (forked-daapd)
- DNS (dnsmasq)
- IPP (ippsample)
- DICOM (dcmqrscp)
- TinyDTLS

## 运行环境分析

### 挑战
1. **Windows 宿主机** — 需要 WSL 或 Git Bash 来运行 Bash 脚本
2. **Docker 依赖** — 镜像构建需要 OPENAI_API_KEY
3. **计算资源** — 完整实验需要大量 CPU 时间（论文中 180 compute-hours）

### 初步建议
- 在 WSL2 环境下运行，体验最佳
- 需要足够的磁盘空间存储 Docker 镜像
