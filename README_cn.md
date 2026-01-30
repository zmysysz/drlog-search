# drlog-search

[English](README.md) | 简体中文

一个高性能的分布式日志搜索系统，专为跨多个代理的可扩展日志索引和查询而设计。

## 目录

- [概述](#概述)
- [功能特性](#功能特性)
- [架构设计](#架构设计)
- [系统要求](#系统要求)
- [编译构建](#编译构建)
- [配置说明](#配置说明)
- [部署指南](#部署指南)
- [使用方法](#使用方法)
- [API 参考](#api-参考)

## 概述

**drlog-search**（分布式原始日志搜索，Distributed Raw Log Search）是一个轻量级、高性能的分布式日志搜索解决方案，专为传统日志收集系统不实用或不必要的场景而设计。"drlog"这个名称强调了它专注于直接在分布式服务器上搜索原始日志文件，无需集中式日志聚合基础设施。

### 为什么选择 drlog-search？

**drlog-search** 非常适合以下场景：

- **没有日志收集基础设施的环境**：在部署 ELK、Splunk 或类似集中式日志系统过于复杂、昂贵或不必要的服务器或业务场景
- **AI Agent 集成**：提供简单高效的日志检索工具，可以轻松封装为 MCP（模型上下文协议）服务器，使 AI 代理和大语言模型能够无缝查询分布式日志
- **轻量级部署**：最小的资源占用，无需数据管道、消息队列或集中式存储
- **直接访问原始日志**：在原始格式和位置搜索日志，无需转换或摄取延迟
- **快速故障排查**：无需等待日志传输或索引管道，即可快速跨多个服务器搜索

### 核心组件

- **网关服务器**：管理代理注册和分发搜索请求的中央协调器
- **代理服务器**：在本地文件系统上索引和搜索原始日志文件的分布式工作节点
- **Web 界面**：具有高级查询功能的基于浏览器的搜索用户界面
- **MCP 就绪架构**：简单的 HTTP API 设计，可以轻松封装为 MCP 工具，用于 AI 代理集成

## 功能特性

- **分布式架构**：具有自动代理注册和健康监控的可扩展设计
- **多种搜索类型**：
  - 简单关键字搜索
  - 布尔逻辑搜索（AND/OR/NOT 运算符）
  - 正则表达式模式匹配
- **实时索引**：可配置间隔的自动文件扫描
- **压缩支持**：原生 gzip 压缩日志文件处理
- **高性能**：
  - 基于协程的异步 I/O
  - 多线程请求处理
  - Intel ISA-L 压缩加速
- **Web 用户界面**：具有结果可视化的交互式搜索界面
- **灵活配置**：基于 JSON 的配置和运行时参数

## 架构设计

### 系统设计

```
┌─────────────┐
│   客户端    │
│  (浏览器)   │
└──────┬──────┘
       │ HTTP
       ▼
┌─────────────────┐
│   网关服务器    │
│  - 代理管理     │
│  - 负载均衡     │
│  - 结果聚合     │
└────────┬────────┘
         │ HTTP (并行)
    ┌────┴────┬────────┐
    ▼         ▼        ▼
┌────────┐ ┌────────┐ ┌────────┐
│ 代理 1 │ │ 代理 2 │ │ 代理 N │
│ 索引器 │ │ 索引器 │ │ 索引器 │
│ 搜索器 │ │ 搜索器 │ │ 搜索器 │
└────┬───┘ └────┬───┘ └────┬───┘
     ▼          ▼          ▼
  [日志]     [日志]     [日志]
```

### 请求流程

1. 客户端向网关提交搜索查询（`/log/search`）
2. 网关并行地将请求分发到所有已注册的代理
3. 每个代理：
   - 索引本地日志文件（如果需要）
   - 使用适当的搜索器执行搜索
   - 返回匹配结果
4. 网关聚合结果并返回给客户端

### 模块结构

#### 代理模块 (`src/agent/`)
- **索引器**：文件系统扫描和日志文件索引
- **搜索器**：多种搜索算法实现
  - `SimpleSearcher`：基于关键字的搜索
  - `BooleanSearcher`：布尔逻辑（AND/OR/NOT）
  - `RegexSearcher`：正则表达式匹配
- **AgentHandler**：搜索操作的 HTTP 请求处理器

#### 网关模块 (`src/gateway/`)
- **AgentManager**：代理注册、健康检查、负载均衡
- **GatewayHandler**：客户端请求处理和结果聚合

#### 工具模块 (`src/util/`)
- 配置加载
- 压缩/解压缩
- 通用工具（URL 编码、字符串操作）

## 系统要求

### 构建依赖

- **CMake** 3.16 或更高版本
- **C++ 编译器**，支持 C++20（GCC 10+、Clang 11+、MSVC 2019+）
- **Boost 库** 1.74 或更高版本：
  - System
  - Filesystem
  - Regex
- **其他库**：
  - nlohmann/json
  - spdlog
  - ZLIB

### 运行时要求

- Linux 操作系统（目前不支持 Windows）
- 网关和代理之间的网络连接
- 足够的磁盘空间用于日志索引

## 编译构建

### Linux

```bash
# 克隆仓库
git clone <repository-url>
cd drlog-search

# 创建构建目录
mkdir build && cd build

# 使用 CMake 配置
cmake ..

# 构建
make -j$(nproc)

# 可执行文件将位于 build/bin/
# - drlog-gateway
# - drlog-agent
```

### 构建选项

```bash
# 指定构建类型
cmake -DCMAKE_BUILD_TYPE=Release ..

# 自定义安装前缀
cmake -DCMAKE_INSTALL_PREFIX=/opt/drlog ..

# 详细构建
make VERBOSE=1
```

**注意**：目前不支持 Windows 构建。本项目专注于最常需要分布式日志搜索的 Linux 服务器环境。

## 配置说明

### 网关配置 (`config_gateway.json`)

```json
{
  "server_address": "0.0.0.0",
  "server_port": 8080,
  "thread_count": 4,
  "log_path": "./logs/gateway.log",
  "log_level": "info",
  "agent_health_check_interval": 30,
  "agent_timeout": 10
}
```

**参数说明：**
- `server_address`：网关监听地址（0.0.0.0 表示所有接口）
- `server_port`：网关 HTTP 端口
- `thread_count`：工作线程数
- `log_path`：网关日志文件路径
- `log_level`：日志级别（trace、debug、info、warn、error）
- `agent_health_check_interval`：代理健康检查间隔（秒）
- `agent_timeout`：代理请求超时时间（秒）

### 代理配置 (`config_agent.json`)

```json
{
  "server_address": "0.0.0.0",
  "server_port": 8081,
  "thread_count": 4,
  "log_path": "./logs/agent.log",
  "log_level": "info",
  "gateway_address": "http://gateway-host:8080",
  "index_paths": [
    "/var/log/application/*.log",
    "/var/log/application/*.log.gz"
  ],
  "index_interval": 300,
  "max_file_size": 104857600
}
```

**参数说明：**
- `server_address`：代理监听地址
- `server_port`：代理 HTTP 端口
- `thread_count`：工作线程数
- `log_path`：代理日志文件路径
- `log_level`：日志级别
- `gateway_address`：用于注册的网关服务器 URL
- `index_paths`：日志文件路径模式数组（支持通配符和正则表达式）
- `index_interval`：文件重新索引间隔（秒）
- `max_file_size`：要索引的最大文件大小（字节）

## 部署指南

### 单机开发环境设置

```bash
# 终端 1：启动网关
cd drlog-search
./build/bin/drlog-gateway config_gateway.json

# 终端 2：启动代理
./build/bin/drlog-agent config_agent.json

# 访问 Web 界面
# 在浏览器中打开 http://localhost:8080
```

### 多主机生产环境部署

#### 网关服务器

```bash
# 1. 配置网关
cat > config_gateway.json <<EOF
{
  "server_address": "0.0.0.0",
  "server_port": 8080,
  "thread_count": 8,
  "log_path": "/var/log/drlog/gateway.log",
  "log_level": "info"
}
EOF

# 2. 启动网关服务
./drlog-gateway config_gateway.json

# 3. （可选）设置为 systemd 服务
sudo systemctl enable drlog-gateway
sudo systemctl start drlog-gateway
```

#### 代理服务器（在每个日志服务器上）

```bash
# 1. 配置代理并指定网关地址
cat > config_agent.json <<EOF
{
  "server_address": "0.0.0.0",
  "server_port": 8081,
  "thread_count": 4,
  "log_path": "/var/log/drlog/agent.log",
  "log_level": "info",
  "gateway_address": "http://gateway-server:8080",
  "index_paths": [
    "/var/log/app/*.log",
    "/var/log/app/*.log.gz"
  ],
  "index_interval": 300
}
EOF

# 2. 启动代理服务
./drlog-agent config_agent.json

# 3. 验证与网关的注册
curl http://gateway-server:8080/agents
```

### Docker 部署（示例）

```dockerfile
# Dockerfile.gateway
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libboost-all-dev
COPY build/bin/drlog-gateway /usr/local/bin/
COPY config_gateway.json /etc/drlog/
EXPOSE 8080
CMD ["drlog-gateway", "/etc/drlog/config_gateway.json"]

# Dockerfile.agent
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libboost-all-dev
COPY build/bin/drlog-agent /usr/local/bin/
COPY config_agent.json /etc/drlog/
EXPOSE 8081
CMD ["drlog-agent", "/etc/drlog/config_agent.json"]
```

```bash
# 构建镜像
docker build -f Dockerfile.gateway -t drlog-gateway .
docker build -f Dockerfile.agent -t drlog-agent .

# 运行网关
docker run -d -p 8080:8080 --name gateway drlog-gateway

# 运行代理
docker run -d -p 8081:8081 \
  -v /var/log:/var/log:ro \
  --name agent1 \
  -e GATEWAY_ADDRESS=http://gateway:8080 \
  drlog-agent
```

## 使用方法

### Web 界面

1. 在浏览器中打开网关地址（例如：`http://localhost:8080`）
2. 在搜索框中输入搜索查询
3. 选择搜索类型：
   - **Simple**：关键字搜索
   - **Boolean**：使用 AND、OR、NOT 运算符
   - **Regex**：正则表达式模式
4. 查看包含文件路径和匹配行的结果

### 搜索示例

**简单搜索：**
```
error
```

**布尔搜索：**
```
error AND database
error OR warning
error NOT timeout
(error OR warning) AND database
```

**正则表达式搜索：**
```
\d{4}-\d{2}-\d{2}.*ERROR
user_id:\s*\d+
```

## API 参考

### 网关端点

#### 搜索日志
```
POST /log/search
Content-Type: application/json

{
  "query": "error",
  "search_type": "simple"
}

响应：
{
  "results": [
    {
      "agent": "agent1",
      "file": "/var/log/app.log",
      "line": 42,
      "content": "2024-01-29 ERROR: Connection failed"
    }
  ],
  "total": 1,
  "agents_queried": 3
}
```

#### 列出代理
```
GET /agents

响应：
{
  "agents": [
    {
      "address": "http://192.168.1.10:8081",
      "status": "healthy",
      "last_seen": "2024-01-29T10:30:00Z"
    }
  ]
}
```

### 代理端点

#### 搜索本地日志
```
POST /search
Content-Type: application/json

{
  "query": "error",
  "search_type": "simple"
}
```

#### 健康检查
```
GET /health

响应：
{
  "status": "ok",
  "indexed_files": 42,
  "last_index_time": "2024-01-29T10:25:00Z"
}
```

#### 向网关注册
```
POST /register
Content-Type: application/json

{
  "agent_address": "http://192.168.1.10:8081"
}
```

## 故障排除

### 代理无法注册

- 检查代理和网关之间的网络连接
- 验证代理配置中的 `gateway_address`
- 检查网关日志中的注册错误

### 无搜索结果

- 验证日志文件是否匹配 `index_paths` 模式
- 检查代理日志中的索引错误
- 确保文件权限允许读取日志文件
- 验证搜索查询语法

### 性能问题

- 增加配置中的 `thread_count`
- 减少 `index_interval` 以进行更频繁的更新
- 添加更多代理服务器以分散负载
- 检查磁盘 I/O 和网络带宽

## 许可证

本项目采用 MIT 许可证。

```
MIT License

Copyright (c) 2024 drlog-search

特此免费授予任何获得本软件及相关文档文件（"软件"）副本的人不受限制地处理
本软件的权利，包括但不限于使用、复制、修改、合并、发布、分发、再许可和/或
销售软件副本的权利，以及允许获得本软件的人这样做，但须符合以下条件：

上述版权声明和本许可声明应包含在本软件的所有副本或主要部分中。

本软件按"原样"提供，不提供任何形式的明示或暗示保证，包括但不限于对适销性、
特定用途适用性和非侵权性的保证。在任何情况下，作者或版权持有人均不对任何
索赔、损害或其他责任负责，无论是在合同诉讼、侵权行为还是其他方面，由软件
或软件的使用或其他交易引起、产生或与之相关。
```

## 联系方式

如有问题、反馈或贡献，请联系：

- 邮箱：zmysysz@163.com

## 贡献

欢迎贡献！请随时提交 Pull Request 或创建 Issue 来报告错误、请求功能或提出改进建议。
