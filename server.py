#!/usr/bin/env python3
"""
Невеликий HTTP-сервер для веб-візуалізації.
Віддає файли з web/ і на POST /api/compile запускає build/c2py
в усіх режимах (--tokens, --json, --py, --run) та виконує згенерований Python.

    python3 server.py            # http://localhost:5050
    python3 server.py 9000       # інший порт
"""
import json
import os
import subprocess
import sys
import tempfile
import threading
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(ROOT, "build", "c2py.exe" if os.name == "nt" else "c2py")
TIMEOUT = 5  # секунд — щоб нескінченний цикл не повісив сервер


def run(args, stdin=None):
    try:
        p = subprocess.run(args, capture_output=True, text=True, timeout=TIMEOUT, input=stdin)
        return {"stdout": p.stdout, "stderr": p.stderr, "code": p.returncode}
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode() if isinstance(e.stdout, bytes) else (e.stdout or "")
        return {"stdout": out, "stderr": f"\n[зупинено: довше {TIMEOUT} с]", "code": -1}


def compile_code(code):
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "program.c")
        with open(src, "w", encoding="utf-8") as f:
            f.write(code)

        res = {"errors": [], "tokens": [], "ast": None, "python": None, "run": None, "py": None}

        tok = run([BIN, "--tokens", src])
        for line in tok["stdout"].splitlines():
            parts = line.split(None, 2)
            if len(parts) >= 2:
                res["tokens"].append({"line": int(parts[0]), "token": parts[1],
                                      "text": parts[2] if len(parts) > 2 else ""})

        ast = run([BIN, "--json", src])
        res["errors"] = [l for l in ast["stderr"].splitlines() if l.startswith("line ")]
        if ast["stdout"].strip():
            res["ast"] = json.loads(ast["stdout"])
        if ast["code"] != 0:
            if not res["errors"]:
                res["errors"] = [l for l in ast["stderr"].splitlines() if l.strip()]
            return res

        py = run([BIN, "--py", src])
        res["python"] = py["stdout"]
        res["run"] = run([BIN, "--run", src])
        res["py"] = run([sys.executable, "-c", res["python"]])
        return res


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=os.path.join(ROOT, "web"), **kw)

    def log_message(self, fmt, *args):
        pass

    def send_json(self, obj):
        data = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/api/examples":
            items = []
            for folder in ("examples", "examples/errors", "tests/cases"):
                d = os.path.join(ROOT, folder)
                for name in sorted(os.listdir(d)):
                    if name.endswith(".c"):
                        with open(os.path.join(d, name), encoding="utf-8") as f:
                            rel = os.path.relpath(os.path.join(d, name), os.path.join(ROOT, "examples"))
                            items.append({"name": rel.replace("\\", "/").replace("../", ""), "code": f.read()})
            return self.send_json(items)
        return super().do_GET()

    def do_POST(self):
        if self.path != "/api/compile":
            self.send_error(404)
            return
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        code = json.loads(body or b"{}").get("code", "")
        self.send_json(compile_code(code))


def main():
    if not os.path.exists(BIN):
        sys.exit("Спочатку зберіть проєкт: make")
    port = next((int(a) for a in sys.argv[1:] if a.isdigit()), 8000)
    srv = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    url = f"http://localhost:{port}"
    print(f"AST viewer: {url}   (Ctrl+C — зупинити)")
    if "--no-browser" not in sys.argv:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
