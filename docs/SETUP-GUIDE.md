# ChatAFL Windows 环境配置指南

> 从 Git clone 到成功运行 Fuzzing 的完整流程总结
> 创建日期：2026-04-07
> 适用环境：Windows + WSL2 + Docker Desktop

---

## 目录

1. [环境概述](#1-环境概述)
2. [前置条件](#2-前置条件)
3. [配置步骤](#3-配置步骤)
4. [常见问题与解决](#4-常见问题与解决)
5. [运行测试](#5-运行测试)
6. [快速命令参考](#6-快速命令参考)

---

## 1. 环境概述

### 系统架构

```
Windows (WSL2)
├── Ubuntu Terminal
│   ├── ChatAFL 项目 (/mnt/e/lunwen/ChatAFL)
│   └── Docker CLI → Docker Desktop (Linux Container Mode)
│       └── pure-ftpd / live555 / kamailio 镜像
└── MiniMax API (api.minimax.io)
```

### 项目结构

```
ChatAFL/
├── ChatAFL/          # 完整实现（3个 LLM 策略）
├── ChatAFL-CL1/       # Ablation：仅结构感知变异
├── ChatAFL-CL2/       # Ablation：结构感知 + 种子丰富化
├── aflnet/            # 修改版 AFLNet
├── benchmark/         # ProfuzzBench + 测试 subjects
│   └── subjects/
│       ├── FTP/PureFTPD/    # Docker 镜像
│       ├── RTSP/Live555/    # Docker 镜像
│       └── SIP/Kamailio/    # Docker 镜像
└── *.sh              # 运行脚本
```

---

## 2. 前置条件

### 软件要求

| 软件 | 版本 | 说明 |
|------|------|------|
| Windows 10/11 | - | 需要支持 WSL2 |
| WSL2 | Ubuntu 20.04+ | Linux 环境 |
| Docker Desktop | 20.10+ | 需启用 WSL2 集成 |
| Git | - | 克隆代码 |
| MiniMax API Key | - | 用于 LLM 调用 |

### 环境检查

```bash
# 检查 WSL2
wsl --status

# 检查 Docker（在 Ubuntu 终端中）
docker --version

# 检查 Docker 是否为 Linux 容器模式
docker version --format '{{.Server.Os}}'
# 输出应为: linux
```

---

## 3. 配置步骤

### Step 1: 克隆项目

```bash
cd /mnt/e/lunwen  # 或你选择的目录
git clone https://github.com/ChatAFLndss/ChatAFL.git
cd ChatAFL
```

### Step 2: 修复 Shell 脚本行尾格式

**重要：** Windows Git 会将 shell 脚本转换为 CRLF 格式，必须转换为 Unix LF。

```bash
# 方法 1: 使用 dos2unix（推荐）
find . -name "*.sh" -exec dos2unix {} \;

# 方法 2: 使用 sed
find . -name "*.sh" -exec sed -i 's/\r$//' {} \;
```

### Step 3: 修复 Docker 镜像拉取问题（如有）

如果 `docker pull ubuntu:20.04` 失败：

```bash
docker system prune -a --volumes -f
docker pull ubuntu:20.04
```

### Step 4: 配置 MiniMax API

修改 `ChatAFL/chat-llm.c` 中的 API 配置：

```c
// 第 52-58 行
if (strcmp(model, "instruct") == 0)
{
    url = "https://api.minimax.io/v1/completions";  // MiniMax 端点
}
else
{
    url = "https://api.minimax.io/v1/chat/completions";
}

// 第 60-63 行：API Key 从环境变量读取
char *api_key = getenv("MINIMAX_API_KEY");
if (!api_key) api_key = "";
char *auth_header;
asprintf(&auth_header, "Authorization: Bearer %s", api_key);

// 第 67-74 行：模型名称
"{\"model\": \"MiniMax-M2.7\", ...}"
```

### Step 5: 修改 Dockerfile 支持 API Key 注入

在 `benchmark/subjects/<PROTOCOL>/<SUBJECT>/Dockerfile` 开头添加：

```dockerfile
FROM ubuntu:20.04

# MiniMax API Key (passed via --build-arg during docker build)
ARG MINIMAX_API_KEY
ENV MINIMAX_API_KEY=${MINIMAX_API_KEY}

# Install common dependencies
...
```

### Step 6: 构建 Docker 镜像

选择要构建的协议（建议先只构建 1-2 个节省时间）：

```bash
cd benchmark

# 设置 API Key
export MINIMAX_API_KEY="你的API Key"

# 构建单个协议（推荐）
cd subjects/FTP/PureFTPD
docker build . -t pure-ftpd \
    --build-arg MAKE_OPT="-j4" \
    --build-arg MINIMAX_API_KEY=$MINIMAX_API_KEY

# 或构建多个协议
cd subjects/RTSP/Live555
docker build . -t live555 \
    --build-arg MAKE_OPT="-j4" \
    --build-arg MINIMAX_API_KEY=$MINIMAX_API_KEY

cd subjects/SIP/Kamailio
docker build . -t kamailio \
    --build-arg MAKE_OPT="-j4" \
    --build-arg MINIMAX_API_KEY=$MINIMAX_API_KEY
```

### Step 7: 配置 Python 环境

使用 conda 虚拟环境 `netchat`：

```bash
# 检查环境
D:/important/anconda3/envs/netchat/python.exe -c "import pandas; import matplotlib"

# 如未安装
D:/important/anconda3/envs/netchat/python.exe -m pip install pandas matplotlib
```

---

## 4. 常见问题与解决

### 问题 1: Docker 镜像拉取失败

**错误信息：**
```
failed to resolve source metadata for docker.io/library/ubuntu:20.04
failed size validation: 7948 != 7620
```

**解决：**
```bash
docker system prune -a --volumes -f
docker pull ubuntu:20.04
```

---

### 问题 2: Windows Git Bash 路径不兼容

**错误信息：**
```
C:/Program Files/Git/usr/bin/bash: no such file or directory
```

**原因：** Docker 容器无法访问 Windows Git Bash 路径

**解决：** 在 WSL2 Ubuntu 终端中运行所有命令，不要使用 Git Bash

```bash
# 打开 WSL2 Ubuntu 终端（不是 Git Bash）
cd /mnt/e/lunwen/ChatAFL
```

---

### 问题 3: Shell 脚本 CRLF 行尾

**错误信息：**
```
/bin/bash^M: bad interpreter: No such file or directory
```

**解决：**
```bash
# 修复所有 shell 脚本
find . -name "*.sh" -exec dos2unix {} \;

# 验证
file run.sh
# 应输出: Bourne-Again shell script, ASCII text executable
# (不应包含 "CRLF line terminators")
```

---

### 问题 4: 重新构建镜像后仍报 CRLF 错误

**原因：** 镜像构建时复制了 CRLF 格式的脚本

**解决：** 确保脚本已转换为 LF 格式后，重新构建镜像：

```bash
docker rmi <image_name> -f
# 重新构建
docker build . -t <image_name> --build-arg MINIMAX_API_KEY=$MINIMAX_API_KEY
```

---

### 问题 5: API Key 获取失败

**检查：**
```bash
# 在容器内检查
docker run --rm -it <image_name> bash
echo $MINIMAX_API_KEY
```

---

## 5. 运行测试

### 标准测试命令

```bash
cd /mnt/e/lunwen/ChatAFL

# 设置 API Key
export KEY="你的MiniMax API Key"

# 运行测试（1容器，5分钟）
./run.sh 1 5 pure-ftpd chatafl
```

### 命令参数说明

```bash
./run.sh <容器数量> <超时(分钟)> <协议> <fuzzer>

# 示例
./run.sh 1 5 pure-ftpd chatafl      # pure-ftpd, 5分钟
./run.sh 1 5 live555 chatafl        # live555, 5分钟
./run.sh 1 5 kamailio chatafl       # kamailio, 5分钟
./run.sh 1 5 pure-ftpd aflnet       # 使用 AFLNet 而非 ChatAFL
```

### 查看结果

```bash
# 结果位置
ls benchmark/results-pure-ftpd/

# 关键文件
├── queue/                    # 测试用例队列
├── protocol-grammars/        # LLM 生成的语法
├── stall-interactions/        # LLM 交互记录
├── cov_html/                 # 覆盖率报告
├── plot_data                 # 覆盖率时序数据
└── fuzzer_stats             # 统计信息
```

---

## 6. 快速命令参考

```bash
# ========== 环境准备 ==========
# 1. 克隆项目
git clone https://github.com/ChatAFLndss/ChatAFL.git
cd ChatAFL

# 2. 修复 CRLF 问题
find . -name "*.sh" -exec dos2unix {} \;

# 3. 清理 Docker（如需要）
docker system prune -a --volumes -f

# ========== 构建镜像 ==========
export MINIMAX_API_KEY="你的APIKey"
cd benchmark/subjects/FTP/PureFTPD
docker build . -t pure-ftpd --build-arg MAKE_OPT="-j4" --build-arg MINIMAX_API_KEY=$MINIMAX_API_KEY

# ========== 运行测试 ==========
cd /mnt/e/lunwen/ChatAFL
export KEY="你的APIKey"
./run.sh 1 5 pure-ftpd chatafl

# ========== 查看结果 ==========
ls benchmark/results-pure-ftpd/
```

---

## 附录: 已验证的协议镜像大小

| 协议 | Subject | 镜像大小 |
|------|---------|----------|
| FTP | PureFTPD | 1.33 GB |
| RTSP | Live555 | 1.58 GB |
| SIP | Kamailio | 2.7 GB |

---

## 修改记录

| 日期 | 修改内容 |
|------|----------|
| 2026-04-07 | 初始文档创建 |
| 2026-04-07 | 添加 CRLF 问题解决方案 |
| 2026-04-07 | 添加 WSL2 运行说明 |
