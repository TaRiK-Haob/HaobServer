# HaoBServer
一个丑陋的webserver实现。

TODO: HTTP的完整解析和响应 以及更高级的优化方法 CGI(FastCGI)

## 目录结构
``` bash
.
├── CMakeLists.txt
├── README.md
├── root                server根目录
│   └── index.html
└── src
    ├── connection.cpp      连接类实现
    ├── connection.h        连接类的头文件
    ├── main.cpp            主函数/入口
    ├── thread_pool.cpp     线程池实现
    ├── thread_pool.h       线程池头
    ├── webserver.cpp       对webserver进行封装
    └── webserver.h
```

## 工作流程/原理
简单来说，webserver的任务就是三个 建立通信连接、接收和解析请求、返回响应。

### 1. 建立通信连接
- Webserver通常通过基于TCP的HTTP提供服务
- 启动时，server会：
  1. 通过`socket()`创建监听套接字，并通过调用`bind()`绑定提供服务的IP地址和端口
  2. 调用`listen()`：进入监听状态，等待客户端发起连接
- 当客户端发起连接时：
  1. 下层TCP透明地完成三次握手建立TCP连接
  2. 内核将新连接放入连接就绪队列（`listen()`维护的两个队列（Accept队列、SYN队列））
>  `listen()`维护的两个队列（Accept队列、SYN队列）  
SYN Flood 攻击的就是listen这里的SYN队列，通过建立大量的半连接，挤占满SYN队列，达到拒绝服务的效果  
现在Linux内核默认开启SYN-Cookies，当SYN队列满，就启用  
可以通过`sudo sysctl -w net.ipv4.tcp_syncookies=0`关闭，记得`sudo sysctl -w net.ipv4.tcp_syncookies=1`重新开启  
并使用`hping3 -S -p 8080 --flood --rand-source 127.0.0.1`发起SYN-Flood攻击
  3. Server通过`accept()`，从Accept队列取出连接，并创建一个新的已连接的套接字（返回fd）
      - 后续数据交互（请求/响应）都通过这个新套接字进行
      - 原始的监听套接字继续监听其他新连接，不参与数据传输

### 2. 接收和解析请求

- 连接建立后（即 `accept()` 成功返回新 socket fd），Web Server 通过 `read()` / `recv()` 系统调用**从已连接套接字中读取客户端发来的原始字节流**。
- HTTP 协议是**基于文本的请求-响应协议**，请求格式如下：
  ```http
  GET /index.html HTTP/1.1\r\n
  Host: localhost:8080\r\n
  User-Agent: curl/7.68.0\r\n
  \r\n
  ```
  （对于 POST 请求，还可能包含请求体）

- **解析过程通常包括**：
  1. **按行分割**：以 `\r\n` 为分隔符，第一行为**请求行**（方法、路径、协议版本）。
  2. **解析请求头**：后续每行是 `Key: Value` 格式的头部字段，直到遇到空行（`\r\n\r\n`）。
  3. **处理请求体**（如 POST/PUT）：
     - 根据 `Content-Length` 或 `Transfer-Encoding: chunked` 决定读取多少字节。
  4. **校验合法性**：
     - 方法是否支持（GET/POST 等）
     - 路径是否合法（防止目录穿越，如 `../`）
     - Host 头是否匹配（虚拟主机）

- **注意**：
  - 网络数据可能**分多次到达**（TCP 是流式协议），需循环读取直到完整请求到达。
  - 为防阻塞，生产级 Server 通常使用 **非阻塞 I/O + 事件循环**（如 epoll）或 **多线程/协程**。

> ✅ 关键点：**“接收”是 I/O 操作，“解析”是协议处理，两者解耦**。

---

### 3. 返回响应

- 根据解析后的请求（如路径 `/index.html`），Server **生成对应的 HTTP 响应**，并通过 `write()` / `send()` 写回客户端。
- HTTP 响应格式如下：
  ```http
  HTTP/1.1 200 OK\r\n
  Content-Type: text/html\r\n
  Content-Length: 123\r\n
  \r\n
  <html>...</html>
  ```

- **响应构建步骤**：
  1. **确定状态码**：
     - `200 OK`（成功）
     - `404 Not Found`（文件不存在）
     - `500 Internal Server Error`（服务端错误）
  2. **设置响应头**：
     - `Content-Type`：根据文件后缀（如 `.html` → `text/html`）
     - `Content-Length`：响应体字节数（或使用 `Transfer-Encoding: chunked`）
     - 其他：`Server`、`Date`、`Connection: keep-alive` 等
  3. **读取并发送响应体**：
     - 静态文件：从磁盘读取（注意大文件需分块读，避免内存溢出）
     - 动态内容：由程序生成（如 CGI、FastCGI、内嵌逻辑）

- **发送优化**：
  - 使用 `sendfile()`（零拷贝）高效传输静态文件。
  - 支持 **持久连接（Keep-Alive）**：不立即关闭 socket，可复用处理后续请求。
  - 支持 **HTTP/1.1 Pipeline**（较少用）或升级到 **HTTP/2**。

- **连接关闭策略**：
  - 若请求头含 `Connection: close`，或响应未声明 `keep-alive`，则发送完后**关闭连接**（`close(fd)`）。
  - 否则，**保持连接**，回到“接收请求”阶段，等待下一个请求。

> ✅ 关键点：响应必须符合 HTTP 协议规范，否则客户端（如浏览器）可能无法解析。