# drlog-search

English | [简体中文](README_cn.md)

A high-performance distributed log search system designed for scalable log indexing and querying across multiple agents.

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [Requirements](#requirements)
- [Building](#building)
- [Configuration](#configuration)
- [Deployment](#deployment)
- [Usage](#usage)
- [API Reference](#api-reference)

## Overview

**drlog-search** (Distributed Raw Log Search) is a lightweight, high-performance distributed log search solution designed for scenarios where traditional log collection systems are impractical or unnecessary. The name "drlog" emphasizes its focus on searching raw log files directly on distributed servers without requiring centralized log aggregation infrastructure.

### Why drlog-search?

**drlog-search** is ideal for:

- **Environments without log collection infrastructure**: Servers or services where deploying ELK, Splunk, or similar centralized logging systems is too complex, costly, or unnecessary
- **AI Agent Integration**: Provides a simple, efficient log retrieval tool that can be easily wrapped as an MCP (Model Context Protocol) server, enabling AI agents and LLMs to query distributed logs seamlessly
- **Lightweight deployments**: Minimal resource footprint with no need for data pipelines, message queues, or centralized storage
- **Direct raw log access**: Search logs in their original format and location without transformation or ingestion delays
- **Quick troubleshooting**: Rapidly search across multiple servers without waiting for log shipping or indexing pipelines

### Key Components

- **Gateway Server**: Central coordinator managing agent registration and distributing search requests
- **Agent Servers**: Distributed workers indexing and searching raw log files locally on their filesystems
- **Web Interface**: Browser-based search UI with advanced query capabilities
- **MCP-Ready Architecture**: Simple HTTP API design that can be easily wrapped as an MCP tool for AI agent integration

## Features

- **Distributed Architecture**: Scalable design with automatic agent registration and health monitoring
- **Multiple Search Types**:
  - Simple keyword search
  - Boolean logic search (AND/OR/NOT operators)
  - Regular expression pattern matching
- **Real-time Indexing**: Automatic file scanning with configurable intervals
- **Compression Support**: Native gzip handling for compressed log files
- **High Performance**: 
  - Coroutine-based async I/O
  - Multi-threaded request processing
  - Intel ISA-L compression acceleration
- **Web UI**: Interactive search interface with result visualization
- **Flexible Configuration**: JSON-based configuration with runtime parameters

## Architecture

### System Design

```
┌─────────────┐
│   Client    │
│ (Web Browser)│
└──────┬──────┘
       │ HTTP
       ▼
┌─────────────────┐
│  Gateway Server │
│  - Agent Mgmt   │
│  - Load Balance │
│  - Aggregation  │
└────────┬────────┘
         │ HTTP (parallel)
    ┌────┴────┬────────┐
    ▼         ▼        ▼
┌────────┐ ┌────────┐ ┌────────┐
│ Agent 1│ │ Agent 2│ │ Agent N│
│ Indexer│ │ Indexer│ │ Indexer│
│Searcher│ │Searcher│ │Searcher│
└────┬───┘ └────┬───┘ └────┬───┘
     ▼          ▼          ▼
  [Logs]     [Logs]     [Logs]
```

### Request Flow

1. Client submits search query to Gateway (`/log/search`)
2. Gateway distributes request to all registered Agents in parallel
3. Each Agent:
   - Indexes local log files (if needed)
   - Executes search using appropriate searcher
   - Returns matching results
4. Gateway aggregates results and returns to Client

### Module Structure

#### Agent Module (`src/agent/`)
- **Indexer**: File system scanning and log file indexing
- **Searchers**: Multiple search algorithm implementations
  - `SimpleSearcher`: Keyword-based search
  - `BooleanSearcher`: Boolean logic (AND/OR/NOT)
  - `RegexSearcher`: Regular expression matching
- **AgentHandler**: HTTP request handlers for search operations

#### Gateway Module (`src/gateway/`)
- **AgentManager**: Agent registration, health checks, load balancing
- **GatewayHandler**: Client request handling and result aggregation

#### Utilities (`src/util/`)
- Configuration loading
- Compression/decompression
- Common utilities (URL encoding, string operations)

## Requirements

### Build Dependencies

- **CMake** 3.16 or higher
- **C++ Compiler** with C++20 support (GCC 10+, Clang 11+, MSVC 2019+)
- **Boost Libraries** 1.74 or higher:
  - System
  - Filesystem
  - Regex
- **Additional Libraries**:
  - nlohmann/json
  - spdlog
  - ZLIB

### Runtime Requirements

- Linux operating system (Windows is not currently supported)
- Network connectivity between gateway and agents
- Sufficient disk space for log indexing

## Building

### Linux

```bash
# Clone the repository
git clone <repository-url>
cd drlog-search

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build
make -j$(nproc)

# Executables will be in build/bin/
# - drlog-gateway
# - drlog-agent
```

### Build Options

```bash
# Specify build type
cmake -DCMAKE_BUILD_TYPE=Release ..

# Custom install prefix
cmake -DCMAKE_INSTALL_PREFIX=/opt/drlog ..

# Verbose build
make VERBOSE=1
```

**Note**: Windows builds are not currently supported. The project focuses on Linux server environments where distributed log search is most commonly needed.

## Configuration

### Gateway Configuration (`config_gateway.json`)

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

**Parameters:**
- `server_address`: Gateway listening address (0.0.0.0 for all interfaces)
- `server_port`: Gateway HTTP port
- `thread_count`: Number of worker threads
- `log_path`: Path to gateway log file
- `log_level`: Logging level (trace, debug, info, warn, error)
- `agent_health_check_interval`: Agent health check interval (seconds)
- `agent_timeout`: Agent request timeout (seconds)

### Agent Configuration (`config_agent.json`)

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

**Parameters:**
- `server_address`: Agent listening address
- `server_port`: Agent HTTP port
- `thread_count`: Number of worker threads
- `log_path`: Path to agent log file
- `log_level`: Logging level
- `gateway_address`: Gateway server URL for registration
- `index_paths`: Array of log file path patterns (supports wildcards and regex)
- `index_interval`: File re-indexing interval (seconds)
- `max_file_size`: Maximum file size to index (bytes)

## Deployment

### Single-Host Development Setup

```bash
# Terminal 1: Start Gateway
cd drlog-search
./build/bin/drlog-gateway config_gateway.json

# Terminal 2: Start Agent
./build/bin/drlog-agent config_agent.json

# Access web interface
# Open browser to http://localhost:8080
```

### Multi-Host Production Deployment

#### Gateway Server

```bash
# 1. Configure gateway
cat > config_gateway.json <<EOF
{
  "server_address": "0.0.0.0",
  "server_port": 8080,
  "thread_count": 8,
  "log_path": "/var/log/drlog/gateway.log",
  "log_level": "info"
}
EOF

# 2. Start gateway service
./drlog-gateway config_gateway.json

# 3. (Optional) Set up as systemd service
sudo systemctl enable drlog-gateway
sudo systemctl start drlog-gateway
```

#### Agent Servers (on each log server)

```bash
# 1. Configure agent with gateway address
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

# 2. Start agent service
./drlog-agent config_agent.json

# 3. Verify registration with gateway
curl http://gateway-server:8080/agents
```

### Docker Deployment (Example)

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
# Build images
docker build -f Dockerfile.gateway -t drlog-gateway .
docker build -f Dockerfile.agent -t drlog-agent .

# Run gateway
docker run -d -p 8080:8080 --name gateway drlog-gateway

# Run agents
docker run -d -p 8081:8081 \
  -v /var/log:/var/log:ro \
  --name agent1 \
  -e GATEWAY_ADDRESS=http://gateway:8080 \
  drlog-agent
```

## Usage

### Web Interface

1. Open browser to gateway address (e.g., `http://localhost:8080`)
2. Enter search query in the search box
3. Select search type:
   - **Simple**: Keyword search
   - **Boolean**: Use AND, OR, NOT operators
   - **Regex**: Regular expression patterns
4. View results with file paths and matching lines

### Search Examples

**Simple Search:**
```
error
```

**Boolean Search:**
```
error AND database
error OR warning
error NOT timeout
(error OR warning) AND database
```

**Regex Search:**
```
\d{4}-\d{2}-\d{2}.*ERROR
user_id:\s*\d+
```

## API Reference

### Gateway Endpoints

#### Search Logs
```
POST /log/search
Content-Type: application/json

{
  "query": "error",
  "search_type": "simple"
}

Response:
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

#### List Agents
```
GET /agents

Response:
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

### Agent Endpoints

#### Search Local Logs
```
POST /search
Content-Type: application/json

{
  "query": "error",
  "search_type": "simple"
}
```

#### Health Check
```
GET /health

Response:
{
  "status": "ok",
  "indexed_files": 42,
  "last_index_time": "2024-01-29T10:25:00Z"
}
```

#### Register with Gateway
```
POST /register
Content-Type: application/json

{
  "agent_address": "http://192.168.1.10:8081"
}
```

## Troubleshooting

### Agent Not Registering

- Check network connectivity between agent and gateway
- Verify `gateway_address` in agent configuration
- Check gateway logs for registration errors

### No Search Results

- Verify log files match `index_paths` patterns
- Check agent logs for indexing errors
- Ensure file permissions allow reading log files
- Verify search query syntax

### Performance Issues

- Increase `thread_count` in configuration
- Reduce `index_interval` for more frequent updates
- Add more agent servers to distribute load
- Check disk I/O and network bandwidth

## License

This project is licensed under the MIT License.

```
MIT License

Copyright (c) 2024 drlog-search

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Contact

For questions, issues, or contributions, please contact:

- Email: zmysysz@163.com

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request or open an Issue for bugs, feature requests, or improvements.
