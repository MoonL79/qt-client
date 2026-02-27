# WebSocket 架构设计说明

  > 覆盖范围：当前代码中的 WebSocket 客户端封装、登录连接流程、会话窗口收发流程，以及所有与 WebSocket 相关的函数与信号/槽。

  ## 1. 组件与职责

  ### 1.1 `websocketclient`（`src/network/websocketclient.h/.cpp`）
  **角色**：对 `QWebSocket` 的单例封装，提供统一的连接、发送、状态与事件分发。

  **成员**：
  - `QWebSocket m_socket`：底层 WebSocket 实例。
  - `QUrl m_url`：最后一次 `open()` 使用的 URL。

  **构造与单例**：
  - `static websocketclient *instance()`：返回进程级单例。
  - `explicit websocketclient(QObject *parent = nullptr)`：
    - 设置 `NoProxy`。
    - 绑定底层 `QWebSocket` 信号到自身槽：`connected`、`disconnected`、`textMessageReceived`、`binaryMessageReceived`、`pong`、`stateChanged`、`errorOccurred/error`。

  ### 1.2 `LoginWindow`（`src/ui/login/loginwindow.h/.cpp`）
  **角色**：在登录按钮点击时建立 WebSocket 连接，并在连接成功后进入主界面。

  ### 1.3 `SessionWindow`（`src/ui/session/sessionwindow.h/.cpp`）
  **角色**：会话窗口 UI，显示服务器响应、发送消息、展示连接状态。

---

  ## 2. 详细函数与行为

  ### 2.1 `websocketclient` 公共接口

  #### `static websocketclient *instance()`
  - **作用**：返回单例对象。
  - **流程**：
    1. 通过 `static websocketclient instance` 创建静态实例。
    2. 返回 `&instance`。

  #### `void open(const QUrl &url)`
  - **作用**：发起 WebSocket 连接。
  - **流程**：
    1. 若 `url.isValid()` 为 `false`，触发 `errorOccurred(UnsupportedSocketOperationError, "Invalid WebSocket URL")` 并返回。
    2. 记录 `m_url = url`。
    3. 调用 `m_socket.open(url)`。

  #### `void close(QWebSocketProtocol::CloseCode code, const QString &reason)`
  - **作用**：关闭 WebSocket 连接。
  - **流程**：调用 `m_socket.close(code, reason)`。

  #### `void sendTextMessage(const QString &message)`
  - **作用**：发送文本消息。
  - **流程**：
    1. 若未连接（`!isConnected()`），触发 `errorOccurred(OperationError, "WebSocket is not connected")` 并返回。
    2. 调用 `m_socket.sendTextMessage(message)`。

  #### `void sendBinaryMessage(const QByteArray &data)`
  - **作用**：发送二进制消息。
  - **流程**：
    1. 若未连接（`!isConnected()`），触发 `errorOccurred(OperationError, "WebSocket is not connected")` 并返回。
    2. 调用 `m_socket.sendBinaryMessage(data)`。

  #### `bool isConnected() const`
  - **作用**：判断是否已连接。
  - **实现**：`return m_socket.state() == QAbstractSocket::ConnectedState;`

  #### `QAbstractSocket::SocketState state() const`
  - **作用**：返回当前连接状态。
  - **实现**：`return m_socket.state();`

  #### `QUrl url() const`
  - **作用**：返回最近一次连接 URL。
  - **实现**：`return m_url;`

  ### 2.2 `websocketclient` 私有槽

  #### `void onConnected()`
  - **触发**：底层 `QWebSocket::connected`。
  - **行为**：`emit connected();`

  #### `void onDisconnected()`
  - **触发**：底层 `QWebSocket::disconnected`。
  - **行为**：`emit disconnected();`

  #### `void onTextMessageReceived(const QString &message)`
  - **触发**：底层 `QWebSocket::textMessageReceived`。
  - **行为**：`emit textMessageReceived(message);`

  #### `void onBinaryMessageReceived(const QByteArray &data)`
  - **触发**：底层 `QWebSocket::binaryMessageReceived`。
  - **行为**：`emit binaryMessageReceived(data);`

  #### `void onErrorOccurred(QAbstractSocket::SocketError error)`
  - **触发**：底层 `QWebSocket::errorOccurred`（Qt6.5+）或 `QWebSocket::error`（旧版）。
  - **行为**：`emit errorOccurred(error, m_socket.errorString());`

  #### `void onStateChanged(QAbstractSocket::SocketState state)`
  - **触发**：底层 `QWebSocket::stateChanged`。
  - **行为**：`emit stateChanged(state);`

  #### `void onPong(quint64 elapsedTime, const QByteArray &payload)`
  - **触发**：底层 `QWebSocket::pong`。
  - **行为**：`emit pongReceived(elapsedTime, payload);`

---

  ## 3. 通信流程

  ### 3.1 启动并建立连接（登录流程）
  **入口**：`LoginWindow::onLoginClicked()`

  **流程**：
  1. 读取用户名，空时默认 `"User"`。
  2. 禁用登录按钮、显示“连接中...”。
  3. 获取单例 `websocketclient::instance()`。
  4. 若未连接，则调用 `open(QUrl(kWebSocketUrl))`；否则直接调用 `onWebSocketConnected()`。

  **连接成功回调**：`LoginWindow::onWebSocketConnected()`
  - 恢复按钮状态。
  - 触发 `loginSuccess(m_pendingUsername)`，进入主界面。

  **连接失败回调**：`LoginWindow::onWebSocketError()`
  - 恢复按钮状态。
  - 弹窗显示 `message`。

  **相关信号绑定**（构造函数中）：
  - `websocketclient::connected -> LoginWindow::onWebSocketConnected`
  - `websocketclient::errorOccurred -> LoginWindow::onWebSocketError`

---

  ### 3.2 发送文本消息（会话窗口）
  **入口**：`SessionWindow::sendPendingMessage()`

  **流程**：
  1. 从 `m_pendingMessage` 或输入框获取文本。
  2. 若为空则返回。
  3. 在接收框追加一行 `"HH:mm:ss 发送: <message>"`。
  4. 调用 `m_websocket->sendTextMessage(message)`。
  5. 清空输入框。

  **触发方式**：
  - 点击发送按钮：`QPushButton::clicked -> SessionWindow::onSendClicked()`
  - 回车：`QLineEdit::returnPressed -> QPushButton::click -> sendPendingMessage()`

---

  ### 3.3 接收文本消息（服务器 -> 客户端）
  **入口信号**：`websocketclient::textMessageReceived`

  **处理函数（lambda）**：
  - 生成 `"HH:mm:ss 回显: <message>"`
  - 追加到 `m_receiveBox`
  - 记录调试输出 `qDebug()`

  **绑定位置**：`SessionWindow::initUI()`

---

  ### 3.4 接收二进制消息（服务器 -> 客户端）
  **入口信号**：`websocketclient::binaryMessageReceived`

  **处理函数（lambda）**：
  - 使用 `QString::fromUtf8(data)` 解析为 UTF-8 文本（常用于 JSON）。
  - 生成 `"HH:mm:ss 数据: <payload>"`
  - 追加到 `m_receiveBox`
  - 记录调试输出 `qDebug()`

  **绑定位置**：`SessionWindow::initUI()`

---

  ### 3.5 连接状态显示流程（会话窗口）
  **入口信号**：
  - `connected`
  - `disconnected`
  - `stateChanged`
  - `errorOccurred`

  **核心函数**：
  - `updateConnectionStatus(QAbstractSocket::SocketState state)`：将状态映射为中文文本并显示。
  - `appendStatusLine(const QString &message)`：在接收框追加状态提示。

  **流程**：
  - `connected`：更新状态为“已连接”+ 追加“已连接”。
  - `disconnected`：更新状态为“未连接”+ 追加“已断开”。
  - `stateChanged`：仅更新状态文本。
  - `errorOccurred`：追加“连接错误: <message>”并刷新状态。

---

  ## 4. 关键连接关系（信号/槽汇总）

  ### 4.1 `websocketclient` 内部信号转发
  - `QWebSocket::connected -> websocketclient::onConnected -> websocketclient::connected`
  - `QWebSocket::disconnected -> websocketclient::onDisconnected -> websocketclient::disconnected`
  - `QWebSocket::textMessageReceived -> websocketclient::onTextMessageReceived -> websocketclient::textMessageReceived`
  - `QWebSocket::binaryMessageReceived -> websocketclient::onBinaryMessageReceived -> websocketclient::binaryMessageReceived`
  - `QWebSocket::pong -> websocketclient::onPong -> websocketclient::pongReceived`
  - `QWebSocket::stateChanged -> websocketclient::onStateChanged -> websocketclient::stateChanged`
  - `QWebSocket::errorOccurred/error -> websocketclient::onErrorOccurred -> websocketclient::errorOccurred`

  ### 4.2 `LoginWindow` 外部信号绑定
  - `websocketclient::connected -> LoginWindow::onWebSocketConnected`
  - `websocketclient::errorOccurred -> LoginWindow::onWebSocketError`

  ### 4.3 `SessionWindow` 外部信号绑定
  - `websocketclient::textMessageReceived -> (lambda) append "回显"`
  - `websocketclient::binaryMessageReceived -> (lambda) append "数据"`
  - `websocketclient::connected -> updateConnectionStatus + appendStatusLine`
  - `websocketclient::disconnected -> updateConnectionStatus + appendStatusLine`
  - `websocketclient::stateChanged -> updateConnectionStatus`
  - `websocketclient::errorOccurred -> appendStatusLine + updateConnectionStatus`

---

  ## 5. 目前的通信类型与载荷约定

  - **文本消息**：通过 `sendTextMessage()` 发送，直接显示在会话窗口“回显”。
  - **二进制消息**：通过 `binaryMessageReceived` 接收并按 UTF-8 解析为文本展示（适用于 JSON 等文本协议）。
  - **Ping/Pong**：客户端接收 `pong` 信号并转发 `pongReceived`，当前 UI 未消费该信号。

---

  ## 6. 代码位置索引

  - `src/network/websocketclient.h/.cpp`：WebSocket 单例封装与信号转发。
  - `src/ui/login/loginwindow.h/.cpp`：连接建立与登录跳转流程。
  - `src/ui/session/sessionwindow.h/.cpp`：消息收发、状态显示、UI 绑定。
  - `doc/`：当前文档目录。