# ChatAFL 用户指南

> 本指南帮助你从零开始理解和使用 ChatAFL 项目
> 适用于：初次接触 ChatAFL 的研究者/开发者

---

## 目录

1. [项目概览](#1-项目概览)
2. [项目结构](#2-项目结构)
3. [快速开始](#3-快速开始)
4. [测试命令详解](#4-测试命令详解)
5. [测试结果解读](#5-测试结果解读)
6. [高级操作](#6-高级操作)
7. [附录](#7-附录)

---

## 1. 项目概览

### 1.1 ChatAFL 是什么？

ChatAFL 是一个**基于 LLM 引导的协议模糊测试工具**，由 AFLNet 扩展而来，发表于 NDSS 2024。

**核心能力：**
- 使用 LLM 提取协议的机器可读语法（结构感知变异）
- 使用 LLM 丰富初始种子（增加消息多样性）
- 使用 LLM 突破覆盖 plateau（生成新消息突破停滞）

### 1.2 与 AFLNet 的区别

| 特性 | AFLNet | ChatAFL |
|------|--------|---------|
| 结构感知变异 | ❌ | ✅ LLM 生成语法 |
| 种子丰富化 | ❌ | ✅ LLM 丰富消息 |
| 覆盖 plateau 突破 | ❌ | ✅ LLM 生成新消息 |
| LLM 调用 | ❌ | ✅ 支持 OpenAI/MiniMax 等 |

### 1.3 四个版本区别

| 版本 | 说明 | 使用场景 |
|------|------|----------|
| **aflnet** | 原始 AFLNet | 基线对比 |
| **ChatAFL** | 完整实现（3个策略全开） | 正式测试 |
| **ChatAFL-CL1** | 仅结构感知变异 | Ablation 实验 |
| **ChatAFL-CL2** | 结构感知 + 种子丰富化 | Ablation 实验 |

---

## 2. 项目结构

### 2.1 目录树

```
ChatAFL/
├── ChatAFL/              # 完整 ChatAFL 实现
│   ├── chat-llm.c       # LLM 调用逻辑
│   ├── chat-llm.h        # LLM 头文件
│   ├── afl-fuzz.c        # 修改后的模糊测试主逻辑
│   └── ...
│
├── ChatAFL-CL1/          # Ablation: 仅结构感知变异
├── ChatAFL-CL2/          # Ablation: 结构感知 + 种子丰富化
├── aflnet/               # 原始 AFLNet（基线）
│
├── benchmark/             # 测试框架
│   ├── subjects/         # 测试协议
│   │   ├── FTP/          # FTP 协议族
│   │   │   ├── PureFTPD/
│   │   │   ├── ProFTPD/
│   │   │   ├── BFTPD/
│   │   │   └── LightFTP/
│   │   ├── RTSP/         # RTSP 协议 (Live555)
│   │   ├── SIP/          # SIP 协议 (Kamailio)
│   │   ├── SMTP/         # SMTP 协议 (Exim)
│   │   ├── HTTP/         # HTTP 协议 (Lighttpd)
│   │   └── DAAP/         # DAAP 协议
│   │
│   └── scripts/          # 执行脚本
│       ├── execution/     # 运行相关
│       └── analysis/      # 分析相关
│
├── docs/                 # 文档
│   ├── SETUP-GUIDE.md    # 环境配置指南
│   └── USER-GUIDE.md    # 本文档
│
├── run.sh                # 模糊测试入口脚本
├── analyze.sh            # 结果分析脚本
├── setup.sh              # 环境准备脚本
└── clean.sh             # 清理脚本
```

### 2.2 各目录/文件作用

#### 根目录脚本

| 文件 | 作用 | 使用频率 |
|------|------|----------|
| `run.sh` | 运行模糊测试 | ⭐⭐⭐ 高 |
| `analyze.sh` | 分析测试结果 | ⭐⭐ 中 |
| `setup.sh` | 构建 Docker 镜像 | ⭐ 低 |
| `clean.sh` | 清理环境 | ⭐ 低 |

#### Fuzzer 版本目录

| 目录 | 作用 | 适用场景 |
|------|------|----------|
| `ChatAFL/` | 完整 LLM 引导 | 正式 fuzzing |
| `ChatAFL-CL1/` | 仅结构感知变异 | 对比实验 |
| `ChatAFL-CL2/` | 语法+种子丰富化 | 对比实验 |
| `aflnet/` | 无 LLM 基线 | 性能对比基准 |

#### 测试协议目录结构

每个协议（如 `PureFTPD/`）的结构：

```
PureFTPD/
├── Dockerfile           # Docker 构建文件
├── aflnet/             # AFLNet fuzzer
├── chatafl/            # ChatAFL fuzzer
├── chatafl-cl1/        # ChatAFL-CL1
├── chatafl-cl2/        # ChatAFL-CL2
├── fuzzing.patch       # Fuzzing 补丁
├── gcov.patch          # 覆盖率补丁
├── in-ftp/             # 初始种子输入
├── ftp.dict            # FTP 协议字典
├── run.sh              # 协议特定运行脚本
└── cov_script.sh       # 覆盖率计算脚本
```

### 2.3 可用测试协议

| 协议 | Subject | 说明 |
|------|---------|------|
| **FTP** | PureFTPD, ProFTPD, BFTPD, LightFTP | 文件传输协议 |
| **RTSP** | Live555 | 流媒体协议 |
| **SIP** | Kamailio | VoIP 信令协议 |
| **SMTP** | Exim | 邮件传输协议 |
| **HTTP** | Lighttpd | Web 服务器 |
| **DAAP** | forked-daapd | iTunes 兼容协议 |

---

## 3. 快速开始

### 3.1 环境要求

| 组件 | 要求 |
|------|------|
| 操作系统 | Linux / WSL2 (Windows) |
| Docker | 20.10+ |
| Python | 3.x + pandas + matplotlib |
| API Key | MiniMax / OpenAI API Key |

### 3.2 运行第一个测试

```bash
# 1. 进入项目目录
cd /mnt/e/lunwen/ChatAFL

# 2. 设置 API Key
export MINIMAX_API_KEY="你的APIKey"

# 3. 运行测试（1容器，5分钟，测试 pure-ftpd）
./run.sh 1 5 pure-ftpd chatafl

# 4. 查看结果
ls benchmark/results-pure-ftpd/
```

### 3.3 等待时间预估

| 测试时长 | 实际时间 | 说明 |
|----------|----------|------|
| 5 分钟 | ~5-6 分钟 | 快速验证 |
| 60 分钟 | ~1-1.5 小时 | 初步测试 |
| 240 分钟 (4小时) | ~4-5 小时 | 论文级测试 |
| 1440 分钟 (24小时) | ~24+ 小时 | 完整实验 |

---

## 4. 测试命令详解

### 4.1 run.sh 参数说明

```bash
./run.sh <容器数量> <超时(分钟)> <协议> <fuzzer>
```

| 参数 | 说明 | 示例值 |
|------|------|--------|
| 容器数量 | 并行运行的容器数 | 1, 5, 10 |
| 超时 | 单次测试时间（分钟） | 5, 60, 240 |
| 协议 | 测试目标 | pure-ftpd, live555, kamailio |
| Fuzzer | 使用哪个 fuzzer | chatafl, chatafl-cl1, chatafl-cl2, aflnet |

### 4.2 可用协议列表

```bash
# 查看可用协议
ls benchmark/subjects/
```

| 协议名 | Subject 目录 | 协议类型 |
|--------|-------------|----------|
| `pure-ftpd` | FTP/PureFTPD | FTP 服务器 |
| `proftpd` | FTP/ProFTPD | FTP 服务器 |
| `bftpd` | FTP/BFTPD | FTP 服务器 |
| `lightftp` | FTP/LightFTP | FTP 服务器 |
| `live555` | RTSP/Live555 | RTSP 流媒体 |
| `kamailio` | SIP/Kamailio | SIP 服务器 |
| `exim` | SMTP/Exim | SMTP 服务器 |
| `lighttpd1` | HTTP/Lighttpd1 | HTTP 服务器 |

### 4.3 可用 Fuzzer 列表

| Fuzzer | 说明 | 特点 |
|--------|------|------|
| `chatafl` | 完整 ChatAFL | 3个 LLM 策略全开，效果最好 |
| `chatafl-cl1` | Ablation 版 | 仅结构感知变异 |
| `chatafl-cl2` | Ablation 版 | 语法 + 种子丰富化 |
| `aflnet` | 基线 | 无 LLM，纯 AFLNet |

### 4.4 组合示例

```bash
# ========== 基本测试 ==========
# ChatAFL + PureFTPD，5分钟
./run.sh 1 5 pure-ftpd chatafl

# AFLNet + Live555，5分钟（基线对比）
./run.sh 1 5 live555 aflnet

# ========== Ablation 实验 ==========
# 仅语法变异
./run.sh 1 60 pure-ftpd chatafl-cl1

# 语法 + 种子
./run.sh 1 60 pure-ftpd chatafl-cl2

# 完整 ChatAFL
./run.sh 1 60 pure-ftpd chatafl

# ========== 多容器并行 ==========
# 5容器并行，60分钟
./run.sh 5 60 pure-ftpd chatafl

# ========== 多协议测试 ==========
# 同时测试多个协议（用 all）
./run.sh 1 60 all chatafl
```

### 4.5 analyze.sh 参数说明

```bash
./analyze.sh <协议列表> [超时分钟数]

# 示例
./analyze.sh pure-ftpd 5        # 分析 pure-ftpd 5分钟测试结果
./analyze.sh all 240            # 分析所有协议 4小时测试结果
```

---

## 5. 测试结果解读

### 5.1 结果目录结构

```
benchmark/results-<protocol>-<fuzzer>/
├── queue/                     # 测试用例队列
│   ├── id:000000,orig:...    # 初始种子
│   ├── id:000001,src:...    # 变异产生
│   └── ...
│
├── replayable-crashes/       # 可重放的崩溃样本
├── replayable-hangs/          # 可重放的挂起样本
├── replayable-new-ipsm-paths/ # 新发现的 IPSM 路径
├── replayable-queue/         # 可重放的队列
│
├── protocol-grammars/         # LLM 生成的协议语法
│   └── ...                    # JSON 格式的语法定义
│
├── stall-interactions/        # LLM 交互记录（突破 plateau 时）
│   ├── request-<id>          # 发送给 LLM 的请求
│   └── response-<id>         # LLM 的回复
│
├── responses-ipsm/           # IPSM 响应
├── regions/                  # 覆盖率区域
├── fuzz_bitmap              # AFL 覆盖率位图
├── plot_data                # 时序覆盖率数据
├── fuzzer_stats             # Fuzzer 统计信息
├── cov_over_time.csv        # 覆盖率时序 CSV
└── cov_html/                # HTML 覆盖率报告
    └── index.html           # 可在浏览器打开
```

### 5.2 关键文件含义

| 文件 | 含义 |
|------|------|
| `fuzzer_stats` | Fuzzer 运行统计（运行时间、路径数、崩溃数等） |
| `plot_data` | 覆盖率随时时间变化的数据 |
| `cov_over_time.csv` | CSV 格式的覆盖率时序 |
| `queue/` | 所有发现的测试用例 |
| `replayable-crashes/` | 发现的可重放崩溃 |
| `stall-interactions/` | LLM 生成的突破 plateau 的消息 |

### 5.3 如何判断测试成功/失败

#### 成功标志 ✅

```
# 命令正常完成，输出
CHATAFL: I am done!

# 结果目录存在且有内容
ls benchmark/results-pure-ftpd/  # 非空目录

# container exit code = 0
docker ps -a | grep <container>  # Exited (0)
```

#### 失败标志 ❌

```
# 命令输出错误
Error response from daemon: ...

# 容器异常退出
docker ps -a | grep <container>  # Exited (非0)

# 结果目录为空或不存在
ls benchmark/results-pure-ftpd/  # 空或不存在
```

### 5.4 覆盖率指标解释

#### AFL 统计指标

| 指标 | 含义 | 理想值 |
|------|------|--------|
| `cycles done` | 完成的轮次 | 越多越好 |
| `total paths` | 发现的总路径数 | 越多越好 |
| `uniq crashes` | 唯一崩溃数 | >0 表示发现崩溃 |
| `uniq hangs` | 唯一挂起数 | >0 表示发现挂起 |
| `exec speed` | 执行速度 | 越快越好 |
| `stability` | 稳定性 | 100% 最稳定 |
| `map density` | 地图密度 | 越高覆盖越广 |

#### 覆盖率解读

| 指标 | 低值含义 | 高值含义 |
|------|----------|----------|
| `total paths` | 变异探索不足 | 探索充分 |
| `uniq crashes` | 未发现崩溃 | 发现潜在漏洞 |
| `map density` | 覆盖范围窄 | 协议理解充分 |
| `edge coverage` | 基本覆盖 | 深度覆盖 |

### 5.5 快速查看结果

```bash
# 查看 Fuzzer 统计
cat benchmark/results-pure-ftpd/fuzzer_stats

# 查看覆盖率
cat benchmark/results-pure-ftpd/cov_over_time.csv

# 查看崩溃数
grep "uniq crashes" benchmark/results-pure-ftpd/fuzzer_stats

# 查看 HTML 覆盖率报告（在浏览器打开）
# file:///mnt/e/lunwen/ChatAFL/benchmark/results-pure-ftpd/cov_html/index.html
```

---

## 6. 高级操作

### 6.1 构建新协议镜像

以添加 DNS (dnsmasq) 为例：

```bash
# 1. 进入 benchmark 目录
cd benchmark

# 2. 创建 subject 目录（参考现有结构）
# 需要包含：Dockerfile, aflnet/, chatafl/, fuzzing.patch 等

# 3. 构建镜像
cd subjects/DNS/dnsmasq
docker build . -t dnsmasq \
    --build-arg MAKE_OPT="-j4" \
    --build-arg MINIMAX_API_KEY=$MINIMAX_API_KEY

# 4. 运行测试
cd /mnt/e/lunwen/ChatAFL
./run.sh 1 5 dnsmasq chatafl
```

### 6.2 删除协议镜像

```bash
# 查看当前镜像
docker images

# 删除指定镜像
docker rmi <image_name> -f

# 示例
docker rmi pure-ftpd -f
docker rmi live555 -f
docker rmi kamailio -f

# 清理未使用的镜像
docker image prune -a
```

### 6.3 调整 LLM 参数

LLM 参数位于以下文件：

| 文件 | 参数 | 说明 |
|------|------|------|
| `ChatAFL/config.h` | `UNINTERESTING_THRESHOLD` | plateau 判断阈值 |
| `ChatAFL/config.h` | `CHATTING_THRESHOLD` | LLM 调用触发次数 |
| `ChatAFL/chat-llm.h` | `STALL_RETRIES` | LLM 重试次数 |
| `ChatAFL/chat-llm.h` | `GRAMMAR_RETRIES` | 语法生成重试次数 |
| `ChatAFL/chat-llm.h` | `MAX_ENRICHMENT_MESSAGE_TYPES` | 最大丰富化消息数 |

修改后需重新构建镜像：

```bash
cd benchmark/subjects/FTP/PureFTPD
docker build . -t pure-ftpd --build-arg MINIMAX_API_KEY=$MINIMAX_API_KEY
```

---

## 7. 附录

### 7.1 快速命令速查表

```bash
# ========== 运行测试 ==========
# 基本运行
./run.sh 1 5 pure-ftpd chatafl

# Ablation 实验
./run.sh 1 60 pure-ftpd chatafl-cl1
./run.sh 1 60 pure-ftpd chatafl-cl2
./run.sh 1 60 pure-ftpd aflnet

# 多容器
./run.sh 5 60 pure-ftpd chatafl

# ========== 分析结果 ==========
./analyze.sh pure-ftpd 5
./analyze.sh all 240

# ========== 镜像操作 ==========
# 查看镜像
docker images

# 删除镜像
docker rmi <name> -f

# 清理
docker system prune -a
```

### 7.2 常见问题

**Q: 容器启动失败？**
```bash
# 检查 Docker 状态
docker ps -a

# 重启 Docker Desktop
```

**Q: API 调用失败？**
```bash
# 检查 API Key
echo $MINIMAX_API_KEY

# 检查容器内环境变量
docker run --rm -it <image> bash
echo $MINIMAX_API_KEY
```

**Q: 覆盖率很低？**
- 增加测试时间
- 检查 LLM 是否正常工作
- 查看 `stall-interactions/` 目录确认 LLM 有被调用

**Q: 如何判断 LLM 是否正常工作？**
```bash
# 查看 LLM 交互记录
ls benchmark/results-*/stall-interactions/

# 查看语法生成
ls benchmark/results-*/protocol-grammars/
```

### 7.3 相关文档

| 文档 | 内容 |
|------|------|
| `SETUP-GUIDE.md` | 环境配置详细指南 |
| `README.md` | 项目官方说明 |
| `progress.md` | 配置过程记录 |
| `findings.md` | 研究发现 |

---

## 修改记录

| 日期 | 修改内容 |
|------|----------|
| 2026-04-07 | 初始版本创建 |
