#!/usr/bin/env python3
"""
MockWeatherServer.py — 模拟 wttr.in j1 JSON 接口，供 WeatherManager 测试使用。

控制端点（复用 MockServer 协议格式）：
  GET /_set_error?code=500      → 后续请求返回 HTTP 500
  GET /_set_delay?ms=15000      → 后续请求延迟指定毫秒
  GET /_set_response?target=weather&type=TYPE → 切换响应 payload 类型
    类型:
      json          → 默认完整 j1 JSON（Default）
      invalid_json  → 非 JSON 文本
      missing_cc    → 缺少 current_condition 数组
      missing_wd    → weatherDesc 为空
      empty_body    → 空响应体

标准响应（GET /）返回符合 wttr.in j1 格式的 JSON。
用完之后发 STOP\n 到 stdin 关闭服务器。
"""

import http.server
import json
import sys
import threading
import time
import urllib.parse

# ── 默认 j1 天气数据 ──────────────────────────────────────────────────────
DEFAULT_WEATHER = {
    "current_condition": [
        {
            "FeelsLikeC": "32",
            "humidity": "89",
            "localObsDateTime": "2026-07-05 14:30",
            "temp_C": "28",
            "weatherDesc": [{"value": "多云"}]
        }
    ],
    "nearest_area": [
        {
            "areaName": [{"value": "台北"}],
            "country": [{"value": "台湾"}],
            "region": [{"value": "台北市"}]
        }
    ]
}

# ── 可变配置（线程安全锁） ─────────────────────────────────────────────────
_config = {
    "error_code": 0,
    "response_type": "json",
    "delay_ms": 0,
}
_lock = threading.Lock()


class WeatherHandler(http.server.BaseHTTPRequestHandler):
    """HTTP 请求处理"""

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)

        with _lock:
            ec = _config["error_code"]
            rt = _config["response_type"]
            dl = _config["delay_ms"]

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
            t = params.get("type", ["json"])[0]
            with _lock:
                _config["response_type"] = t
            self._respond(200, b"OK")
            return

        # ---- 天气端点 ----
        if dl > 0:
            time.sleep(dl / 1000.0)

        if ec != 0:
            self._respond(ec, b"Error")
            return

        if rt == "invalid_json":
            self._respond(200, b"This is not JSON at all")
        elif rt == "missing_cc":
            data = {
                "nearest_area": DEFAULT_WEATHER["nearest_area"]
            }
            self._respond_json(data)
        elif rt == "missing_wd":
            data = {
                "current_condition": [
                    {
                        "FeelsLikeC": "32",
                        "temp_C": "28",
                        "humidity": "89",
                        "localObsDateTime": "2026-07-05 14:30",
                        "weatherDesc": []
                    }
                ],
                "nearest_area": DEFAULT_WEATHER["nearest_area"]
            }
            self._respond_json(data)
        elif rt == "empty_body":
            self._respond(200, b"")
        else:
            self._respond_json(DEFAULT_WEATHER)

    def do_POST(self):
        # MockServerHelper.cpp 通过 POST 发送控制指令，委托给 do_GET 处理
        return self.do_GET()

    def _respond(self, code, body):
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _respond_json(self, data):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self._respond(200, body)

    def log_message(self, fmt, *args):
        pass  # 抑制日志输出


def _stdin_reader(server):
    """后台线程：读取 stdin，收到 STOP 时关闭服务器"""
    try:
        while True:
            line = sys.stdin.readline()
            if not line:
                break
            if line.strip() == "STOP":
                server.shutdown()
                break
    except (EOFError, OSError):
        pass


def main():
    server = http.server.HTTPServer(("127.0.0.1", 0), WeatherHandler)
    port = server.server_address[1]
    print(f"PORT:{port}", flush=True)

    daemon = threading.Thread(target=_stdin_reader, args=(server,), daemon=True)
    daemon.start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
