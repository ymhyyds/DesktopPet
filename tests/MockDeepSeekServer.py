#!/usr/bin/env python3
"""
MockDeepSeekServer.py — 模拟 DeepSeek Chat Completion API，供网络超时/错误恢复测试使用。

控制端点（复用 MockServer 协议格式）：
  GET /_set_error?code=500              → 后续请求返回 HTTP 500
  GET /_set_delay?ms=15000              → 后续请求延迟指定毫秒
  GET /_set_response?type=TYPE          → 切换响应类型
    类型:
      streaming_ok      → 默认：完整流式 SSE 回复（Default）
      nonstreaming_ok   → 非流式 JSON 回复（用于摘要）
      streaming_error   → 流式中途返回 API error
      empty_choices     → choices 为空数组
      timeout           → 永不回复（用于超时测试）
      invalid_json      → 返回非 JSON 响应体

标准流式响应（POST /chat/completions）模拟 DeepSeek API 格式。
用完之后发 STOP\n 到 stdin 关闭服务器。
"""

import http.server
import json
import sys
import threading
import time
import urllib.parse

# ── 完整的模拟回复数据 ──────────────────────────────────────────────────────
STREAMING_CHUNKS = [
    'data: {"id":"mock-cmpl-01","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"你好"}}]}\n\n',
    'data: {"id":"mock-cmpl-01","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"，我是桌宠"}}]}\n\n',
    'data: {"id":"mock-cmpl-01","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"，今天过得怎么样"}}]}\n\n',
    'data: {"id":"mock-cmpl-01","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"？"}}]}\n\n',
    'data: [DONE]\n\n',
]

NONSTREAMING_BODY = {
    "id": "mock-cmpl-summary-01",
    "object": "chat.completion",
    "choices": [
        {
            "index": 0,
            "message": {
                "role": "assistant",
                "content": '{"user_facts":[],"confirmed_decisions":[],"preferences":[],"unconfirmed_assistant_suggestions":[],"open_questions":[]}'
            },
            "finish_reason": "stop"
        }
    ],
    "usage": {"prompt_tokens": 50, "completion_tokens": 10, "total_tokens": 60}
}

STREAMING_ERROR_CHUNKS = [
    'data: {"id":"mock-cmpl-err","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"你好"}}]}\n\n',
    'data: {"id":"mock-cmpl-err","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"，我是桌宠"}}]}\n\n',
    'data: {"error":{"message":"余额不足","type":"insufficient_quota","code":"insufficient_quota"}}\n\n',
]

EMPTY_CHOICES_BODY = {
    "id": "mock-cmpl-empty",
    "object": "chat.completion",
    "choices": [],
    "usage": {"prompt_tokens": 10, "completion_tokens": 0, "total_tokens": 10}
}

# ── 可变配置（线程安全锁） ─────────────────────────────────────────────────
_config = {
    "error_code": 0,
    "response_type": "streaming_ok",
    "delay_ms": 0,
}
_lock = threading.Lock()


class DeepSeekHandler(http.server.BaseHTTPRequestHandler):
    """HTTP 请求处理"""

    def do_GET(self):
        self.close_connection = True  # 每个请求后关闭连接，避免 keep-alive 状态混淆
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)

        with _lock:
            ec = _config["error_code"]
            rt = _config["response_type"]

        # ---- 控制端点 ----
        if parsed.path == "/_set_error":
            with _lock:
                _config["error_code"] = int(params.get("code", [0])[0])
            self._respond(200, b"OK")
            return

        if parsed.path == "/_set_delay":
            with _lock:
                _config["delay_ms"] = int(params.get("ms", [0])[0])
            self._respond(200, b"OK")
            return

        if parsed.path == "/_set_response":
            with _lock:
                _config["response_type"] = params.get("type", ["streaming_ok"])[0]
            self._respond(200, b"OK")
            return

        self._respond(404, b"Not Found")

    def do_POST(self):
        self.close_connection = True  # 每个请求后关闭连接
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)

        # 控制端点在 POST 下也可用（MockServerHelper 使用 POST）
        if parsed.path == "/_set_error":
            with _lock:
                _config["error_code"] = int(params.get("code", ["0"])[0])
            self._respond(200, b"OK")
            return
        if parsed.path == "/_set_delay":
            with _lock:
                _config["delay_ms"] = int(params.get("ms", ["0"])[0])
            self._respond(200, b"OK")
            return
        if parsed.path == "/_set_response":
            with _lock:
                _config["response_type"] = params.get("type", ["streaming_ok"])[0]
            self._respond(200, b"OK")
            return

        with _lock:
            ec = _config["error_code"]
            rt = _config["response_type"]
            dl = _config["delay_ms"]

        if parsed.path != "/chat/completions":
            self._respond(404, b"Not Found")
            return

        # 模拟延迟
        if dl > 0 or rt == "timeout":
            if rt == "timeout":
                # 永不回复，等待客户端断开
                import socket
                self.request.settimeout(0.5)
                try:
                    while True:
                        try:
                            data = self.rfile.read(1)
                            if not data:
                                break
                        except socket.timeout:
                            continue
                        except (ConnectionError, BrokenPipeError, OSError):
                            break
                except (ConnectionError, BrokenPipeError, OSError):
                    pass
                self.close_connection = True
                return
            time.sleep(dl / 1000.0)

        if ec != 0:
            self._respond(ec, json.dumps({"error": {"message": "Mock server error"}}).encode("utf-8"))
            return

        if rt == "streaming_ok":
            self._send_streaming(STREAMING_CHUNKS)
            return

        if rt == "streaming_error":
            self._send_streaming(STREAMING_ERROR_CHUNKS)
            return

        if rt == "nonstreaming_ok":
            self._respond(200, json.dumps(NONSTREAMING_BODY).encode("utf-8"))
            return

        if rt == "empty_choices":
            self._respond(200, json.dumps(EMPTY_CHOICES_BODY).encode("utf-8"))
            return

        if rt == "invalid_json":
            self._respond(200, b"this is not json")
            return

        self._respond(200, json.dumps(NONSTREAMING_BODY).encode("utf-8"))

    def _send_streaming(self, chunks):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()
        for chunk in chunks:
            try:
                self.wfile.write(chunk.encode("utf-8"))
                self.wfile.flush()
            except (ConnectionError, BrokenPipeError, OSError):
                break

    def _respond(self, code, body):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(body)
            self.wfile.flush()
        except (ConnectionError, BrokenPipeError, OSError):
            pass

    def log_message(self, fmt, *args):
        sys.stderr.write("[MockDeepSeekServer] %s\n" % (fmt % args))
        sys.stderr.flush()


# ═══════════════════════════════════════════════════════════════════════════
# 启动
# ═══════════════════════════════════════════════════════════════════════════

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    server = http.server.HTTPServer(("127.0.0.1", port), DeepSeekHandler)
    port = server.server_address[1]
    sys.stdout.write("PORT:%d\n" % port)
    sys.stdout.flush()

    # 从 stdin 读取 STOP 命令
    stop_event = threading.Event()
    def monitor_stdin():
        while not stop_event.is_set():
            line = sys.stdin.readline()
            if not line or line.strip() == "STOP":
                break
        stop_event.set()
        server.shutdown()

    t = threading.Thread(target=monitor_stdin, daemon=True)
    t.start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        server.server_close()


if __name__ == "__main__":
    main()
