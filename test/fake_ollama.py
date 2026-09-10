#!/usr/bin/env python3
"""Minimal stand-in for the Ollama HTTP API.

Used to exercise the Windows CE client's HTTP layer (status parsing, header
splitting, NDJSON streaming) on the build host without pulling a real model.

    python3 test/fake_ollama.py 18080
"""

import json
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

MODELS = ["llama3.2:latest", "qwen2.5:1.5b", "phi3:mini"]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, fmt, *args):
        sys.stderr.write("stub: %s - %s\n" % (self.address_string(), fmt % args))

    def do_GET(self):
        if self.path != "/api/tags":
            self.send_error(404)
            return
        body = json.dumps(
            {
                "models": [
                    {
                        "name": name,
                        "model": name,
                        "size": 1234,
                        "details": {"parent_model": "", "family": "llama"},
                    }
                    for name in MODELS
                ]
            }
        ).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/api/generate":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length") or 0)
        req = json.loads(self.rfile.read(length) or b"{}")
        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson")
        self.end_headers()
        for word in ["Hello", " from", " ", req.get("model", "?"), " on", " WinCE", "!\n"]:
            self.wfile.write(
                json.dumps({"model": req.get("model"), "response": word, "done": False}).encode()
                + b"\n"
            )
            self.wfile.flush()
            time.sleep(0.02)
        self.wfile.write(json.dumps({"response": "", "done": True}).encode() + b"\n")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 18080
    HTTPServer(("127.0.0.1", port), Handler).serve_forever()
