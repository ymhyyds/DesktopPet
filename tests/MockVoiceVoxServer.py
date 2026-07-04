#!/usr/bin/env python3
"""
Mock VoiceVox Engine HTTP server for unit testing.
Listens on a random port, prints PORT:<port> to stdout,
supports control endpoints for delay/error injection.
"""

import http.server
import json
import struct
import sys
import socket
import threading
import time
import urllib.parse

# ── Global state ──────────────────────────────────────────────────────────────
delay_ms = 0       # artificial delay before each response (milliseconds)
error_code = 0     # if set, next query_or_synthesis returns this HTTP status, then resets
error_target = ""  # "audio_query", "synthesis", or "" (both)

# One-shot response type overrides. Key: endpoint prefix, Value: type string.
# "invalid_json" → return non-JSON body on 200
# "empty"        → return empty body on 200
# "normal"       → default behavior (removes override)
response_overrides = {}

# ── Helper: build a minimal valid WAV (44-byte header + silence) ─────────────
def make_wav(sample_rate=24000, duration_sec=5.0):
    num_samples = int(sample_rate * duration_sec)
    data_size = num_samples * 2  # 16-bit mono
    header = struct.pack('<4sI4s', b'RIFF', 36 + data_size, b'WAVE')
    header += struct.pack('<4sIHHIIHH',
        b'fmt ', 16,          # chunk-id, chunk-size
        1,                    # PCM
        1,                    # mono
        sample_rate,
        sample_rate * 2,      # byte-rate
        2,                    # block-align
        16)                   # bits-per-sample
    header += struct.pack('<4sI', b'data', data_size)
    return header + bytes(num_samples * 2)  # silence

MOCK_QUERY_JSON = json.dumps({
    "accent_phrases": [],
    "speedScale": 1.0,
    "pitchScale": 0.0,
    "intonationScale": 1.0,
    "volumeScale": 1.0,
    "prePhonemeLength": 0.1,
    "postPhonemeLength": 0.1,
    "outputSamplingRate": 24000,
    "outputStereo": False,
    "kana": ""
}).encode('utf-8')

MOCK_WAV = make_wav()

# ── HTTP handler ──────────────────────────────────────────────────────────────
class MockHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == '/_port':
            self._respond_json(200, {"port": self.server.server_port})
        elif parsed.path == '/_status':
            self._respond_json(200, {"delay_ms": delay_ms, "error_code": error_code})
        else:
            self._respond(404, b'Not Found')

    def do_POST(self):
        global delay_ms, error_code, error_target

        parsed = urllib.parse.urlparse(self.path)
        qs = urllib.parse.parse_qs(parsed.query)

        # ── Control endpoints ──
        if parsed.path == '/_set_delay':
            try:
                delay_ms = int(qs.get('ms', [0])[0])
            except ValueError:
                delay_ms = 0
            self._respond_json(200, {"delay_ms": delay_ms})

        elif parsed.path == '/_set_error':
            try:
                error_code = int(qs.get('code', [0])[0])
                error_target = qs.get('target', ['both'])[0]
            except ValueError:
                error_code = 0
            self._respond_json(200, {"error_code": error_code})

        elif parsed.path == '/_set_delay_increment':
            # For latest-wins: first N requests delayed, subsequent immediate
            # Implemented as: after this call, the next N requests are delayed
            pass  # simplified — use _set_delay instead

        # ── Response override control ──
        elif parsed.path == '/_set_response':
            target = qs.get('target', ['both'])[0]
            rtype = qs.get('type', ['normal'])[0]
            if target == 'both':
                response_overrides['audio_query'] = rtype
                response_overrides['synthesis'] = rtype
            else:
                response_overrides[target] = rtype
            self._respond_json(200, {"ok": True, "target": target, "type": rtype})

        # ── VoiceVox endpoints ──
        elif parsed.path.startswith('/audio_query'):
            self._maybe_delay()

            # One-shot response override
            if 'audio_query' in response_overrides:
                rtype = response_overrides.pop('audio_query')
                if rtype == 'invalid_json':
                    self._respond(200, b'this is not valid json output', b'text/plain')
                    return
                elif rtype == 'empty':
                    self._respond(200, b'', b'application/json')
                    return
                # 'normal' falls through to standard handling

            # Check for forced error on audio_query
            if error_code and (not error_target or error_target == 'audio_query' or error_target == 'both'):
                code = error_code
                error_code = 0  # one-shot
                self._respond(code, b'{"error":"mock error"}')
                return

            self._respond_json(200, json.loads(MOCK_QUERY_JSON))

        elif parsed.path.startswith('/synthesis'):
            self._maybe_delay()

            # One-shot response override
            if 'synthesis' in response_overrides:
                rtype = response_overrides.pop('synthesis')
                if rtype == 'invalid_json':
                    self._respond(200, b'this is not wav data', b'audio/wav')
                    return
                elif rtype == 'empty':
                    self._respond(200, b'', b'audio/wav')
                    return

            # Check for forced error on synthesis
            if error_code and (not error_target or error_target == 'synthesis' or error_target == 'both'):
                code = error_code
                error_code = 0  # one-shot
                self._respond(code, b'{"error":"mock error"}')
                return

            self._respond_wav(200, MOCK_WAV)

        else:
            self._respond(404, b'Not Found')

    def _maybe_delay(self):
        if delay_ms > 0:
            time.sleep(delay_ms / 1000.0)

    def _respond(self, status, body, content_type=b'text/plain'):
        try:
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (ConnectionAbortedError, BrokenPipeError, ConnectionResetError):
            # 客户端主动取消连接（cancelPendingRequests 等），预期噪声，静默忽略
            pass

    def _respond_json(self, status, obj):
        data = json.dumps(obj).encode('utf-8')
        self._respond(status, data, b'application/json')

    def _respond_wav(self, status, wav_data):
        self._respond(status, wav_data, b'audio/wav')

    def log_message(self, fmt, *args):
        sys.stderr.write(f"[MockVV] {args}\n")
        sys.stderr.flush()


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    # Bind random port
    server = http.server.HTTPServer(('127.0.0.1', 0), MockHandler)
    port = server.server_address[1]
    print(f"PORT:{port}", flush=True)

    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    # Wait for "STOP" on stdin or EOF
    try:
        for line in sys.stdin:
            if line.strip() == 'STOP':
                break
    except (EOFError, SystemExit):
        pass
    finally:
        server.shutdown()


if __name__ == '__main__':
    main()
