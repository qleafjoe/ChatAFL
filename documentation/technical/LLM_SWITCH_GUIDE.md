# ChatAFL 国产大模型配置与接口切换指南

ChatAFL 在重构后彻底移除了对 OpenAI Token 的硬编码限制。现在，所有大模型的选择、URL 地址、凭据都支持在 **运行时通过环境变量 (Environment Variables)** 动态配置。

这使得您可以在不重新编译项目、不修改任何一行代码的情况下，无缝切换不同厂商（如 MiniMax、DeepSeek、阿里千问、智谱 GLM 等）的大语言模型服务。

---

## 环境变量说明

| 环境变量 | 说明 | 缺省值 (如果未设置) |
|---|---|---|
| `LLM_URL` | 大模型 API 的完整 URL 地址。请确保填写的是 `chat/completions` 或者兼容的 endpoint | `https://api.minimaxi.com/v1/text/chatcompletion_v2` |
| `LLM_TOKEN` | 您的 API 授权密钥（基于 TokenPlan 控制）。平台会在头部拼接 `Bearer ` 发送 | （空字符串，此时可能触发 API 报错） |
| `LLM_MODEL` | 传递给 API 的目标模型名称 (Model ID) | `MiniMax-M2.7` |

---

## 如何进行实切与运行

当执行 `./run.sh` 或者直接运行 Docker 镜像时，按照常规 Linux 环境变量的传递方式执行即可，例如：

### 1. 默认设置：运行 MiniMax (使用内置 TokenPlan API)
```bash
export LLM_URL="https://api.minimaxi.com/v1/text/chatcompletion_v2"
# 默认使用配置文件中的 Token Plan Key
export LLM_TOKEN="sk-cp-EK3rwNPjpttunXcODVKSpsvJh4dySqRdtbgbjcmLxdlSHRyoIuWJzFPXFUr8I8rponL4y-xwRMMcO3eodW7dwfO2hqL3G6cCQBtIufVHuRX11_JV1YK5YFs"
export LLM_MODEL="MiniMax-M2.7"
./run.sh 1 60 pure-ftpd chatafl
```

### 2. 切换至 DeepSeek
```bash
export LLM_URL="https://api.deepseek.com/chat/completions"
export LLM_TOKEN="sk-20748c87f93e41f39850a346b8ab5d00"
export LLM_MODEL="deepseek-chat"
./run.sh 1 60 pure-ftpd chatafl
```

### 3. 切换至阿里千问 (Qwen)
```bash
export LLM_URL="https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"
export LLM_TOKEN="sk-2dd29ef0ea7346fe814e8087fd593ef3"
export LLM_MODEL="Qwen3-Max"
./run.sh 1 60 pure-ftpd chatafl
```

> **注意：** 当通过 `./run.sh` 进行启动时，底层脚本现已配置会自动将宿主机的环境变量（如 `LLM_URL`）穿透传递进入 Docker 容器中执行的具体进程里面。您只需要如上文一般在宿主机预先 `export` 即可。
