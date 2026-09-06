"""stdio <-> streamable-HTTP bridge for the CLion MCP server.

Why this exists: CLion's MCP server binds Windows loopback only
(127.0.0.1:64362). This repo's Claude Code runs inside WSL2 in NAT mode, where
127.0.0.1 is WSL's own loopback -- a different network stack -- so the `sse` and
`streamable-http` transports can never reach it. WSL interop *can* run Windows
binaries, so we register this script as a plain stdio MCP server launched via
`python.exe` (same trick win_gui_server.py uses). It runs on the Windows side,
where 127.0.0.1 means what CLion thinks it means, and shuttles JSON-RPC between
Claude Code's stdin/stdout and CLion's HTTP endpoint.

Configuration is via CLI flags, NOT env vars: environment does not cross the
WSL->Windows interop boundary unless the name is listed in WSLENV, but argv
always does. Env vars are honoured only as a fallback for running this natively
on Windows.

  --url PATH            full endpoint; default http://127.0.0.1:<port>/stream
  --port N              port only; default 64362
  --project-path PATH   project to target, e.g. D:/GITHUB/structural_programming
  --timeout SECONDS     per-request timeout; default 300
  --debug               log to stderr
"""

import argparse
import json
import os
import re
import subprocess
import sys
import threading
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor

DEFAULT_PORT = 64362

# stdout is the MCP channel: nothing but framed JSON-RPC may ever land there.
_stdout = sys.stdout.buffer
_stdout_lock = threading.Lock()

_cfg = None
_session_id = None
_protocol_version = None


def log(msg):
    if _cfg and _cfg.debug:
        sys.stderr.write(f"[clion-bridge] {msg}\n")
        sys.stderr.flush()


def emit(obj):
    """Write one JSON-RPC message to stdout as a single UTF-8 line."""
    data = json.dumps(obj, ensure_ascii=False).encode("utf-8")
    with _stdout_lock:
        _stdout.write(data + b"\n")
        _stdout.flush()


def emit_all(messages):
    for msg in messages:
        emit(msg)


def headers():
    h = {
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
    }
    if _cfg.project_path:
        h["IJ_MCP_SERVER_PROJECT_PATH"] = _cfg.project_path
    if _session_id:
        h["mcp-session-id"] = _session_id
    if _protocol_version:
        h["MCP-Protocol-Version"] = _protocol_version
    return h


def parse_sse(response):
    """Yield JSON payloads from an SSE stream. Ignores comments/heartbeats."""
    for raw in response:
        line = raw.decode("utf-8", "replace").rstrip("\r\n")
        if not line or line.startswith(":") or not line.startswith("data:"):
            continue
        payload = line[5:].strip()
        if not payload:
            continue
        try:
            yield json.loads(payload)
        except json.JSONDecodeError:
            log(f"non-JSON SSE data: {payload[:120]}")


def post(endpoint, message, timeout=None):
    """POST one JSON-RPC message. Returns (list_of_replies, response_headers)."""
    body = json.dumps(message, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(endpoint, data=body, headers=headers(), method="POST")
    with urllib.request.urlopen(req, timeout=timeout or _cfg.timeout) as resp:
        ctype = (resp.headers.get("Content-Type") or "").lower()
        if "text/event-stream" in ctype:
            return list(parse_sse(resp)), resp.headers
        raw = resp.read().decode("utf-8", "replace").strip()
        if not raw or raw == "null":
            # 202 Accepted for notifications -- nothing to forward.
            return [], resp.headers
        parsed = json.loads(raw)
        return (parsed if isinstance(parsed, list) else [parsed]), resp.headers


def listen(endpoint):
    """Long-lived GET /stream for server->client messages (tools/list_changed)."""
    h = {k: v for k, v in headers().items() if k != "Content-Type"}
    h["Accept"] = "text/event-stream"
    try:
        req = urllib.request.Request(endpoint, headers=h, method="GET")
        with urllib.request.urlopen(req) as resp:
            log("server->client stream open")
            for msg in parse_sse(resp):
                emit(msg)
    except Exception as exc:  # noqa: BLE001 - listener must never kill the bridge
        log(f"server->client stream closed: {exc!r}")


def candidate_ports():
    """CLion's loopback LISTENING ports, so a moved MCP port is still found."""
    try:
        out = subprocess.run(
            ["netstat", "-ano", "-p", "TCP"],
            capture_output=True, text=True, timeout=15,
        ).stdout
        tasks = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq clion64.exe", "/FO", "CSV", "/NH"],
            capture_output=True, text=True, timeout=15,
        ).stdout
    except Exception as exc:  # noqa: BLE001
        log(f"discovery failed: {exc!r}")
        return []

    pids = set(re.findall(r'"clion64\.exe","(\d+)"', tasks))
    if not pids:
        return []

    ports = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 5 or parts[3] != "LISTENING" or parts[4] not in pids:
            continue
        m = re.match(r"^127\.0\.0\.1:(\d+)$", parts[1])
        if m:
            ports.append(int(m.group(1)))
    # Prefer higher ports: the IDE's built-in web server sits at 63342.
    return sorted(set(ports), reverse=True)


def try_initialize(endpoint, message):
    global _session_id, _protocol_version
    replies, resp_headers = post(endpoint, message, timeout=60)
    result = next(
        (r for r in replies if isinstance(r, dict) and r.get("id") == message.get("id")),
        None,
    )
    if result is None or "result" not in result:
        raise RuntimeError(f"no initialize result from {endpoint}")
    _session_id = resp_headers.get("mcp-session-id")
    _protocol_version = result["result"].get("protocolVersion")
    log(f"connected {endpoint} session={_session_id} "
        f"server={result['result'].get('serverInfo')}")
    return result


def resolve(message):
    """Run `initialize` against the first endpoint that answers. Returns it."""
    primary = _cfg.url or f"http://127.0.0.1:{_cfg.port}/stream"
    errors = []
    try:
        return primary, try_initialize(primary, message)
    except Exception as exc:  # noqa: BLE001
        errors.append(f"{primary}: {exc!r}")

    # Configured endpoint is dead -- CLion may have picked a different port.
    for port in candidate_ports():
        url = f"http://127.0.0.1:{port}/stream"
        if url == primary:
            continue
        try:
            return url, try_initialize(url, message)
        except Exception as exc:  # noqa: BLE001
            errors.append(f"{url}: {exc!r}")

    raise RuntimeError("; ".join(errors) or "no CLion MCP endpoint found")


def error_reply(message, exc):
    return {
        "jsonrpc": "2.0",
        "id": message.get("id"),
        "error": {"code": -32603, "message": f"clion-bridge: {exc}"},
    }


def forward(endpoint, message):
    """Relay one client message; always answer a request so nothing hangs."""
    try:
        replies, _ = post(endpoint, message)
        emit_all(replies)
    except Exception as exc:  # noqa: BLE001
        log(f"{message.get('method')} failed: {exc!r}")
        if message.get("id") is not None:
            emit(error_reply(message, exc))


def parse_args(argv):
    p = argparse.ArgumentParser(add_help=True)
    p.add_argument("--url", default=os.environ.get("CLION_MCP_URL", ""))
    p.add_argument(
        "--port", type=int,
        default=int(os.environ.get("CLION_MCP_PORT", DEFAULT_PORT)),
    )
    p.add_argument(
        "--project-path",
        default=os.environ.get("IJ_MCP_SERVER_PROJECT_PATH", ""),
    )
    p.add_argument(
        "--timeout", type=float,
        default=float(os.environ.get("CLION_BRIDGE_TIMEOUT", "300")),
    )
    p.add_argument(
        "--debug", action="store_true",
        default=os.environ.get("CLION_BRIDGE_DEBUG") == "1",
    )
    return p.parse_args(argv)


def main(argv=None):
    global _cfg
    _cfg = parse_args(argv if argv is not None else sys.argv[1:])
    log(f"starting; project={_cfg.project_path or '(default)'}")

    endpoint = None
    pool = ThreadPoolExecutor(max_workers=8, thread_name_prefix="clion-bridge")

    for raw in sys.stdin.buffer:
        line = raw.decode("utf-8", "replace").strip()
        if not line:
            continue
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            log(f"dropping malformed stdin line: {line[:120]}")
            continue

        # `initialize` runs inline: it establishes the session id every other
        # request depends on, so it must not race with them.
        if message.get("method") == "initialize":
            try:
                endpoint, result = resolve(message)
            except Exception as exc:  # noqa: BLE001
                log(f"initialize failed: {exc!r}")
                emit(error_reply(message, exc))
                continue
            emit(result)
            threading.Thread(target=listen, args=(endpoint,), daemon=True).start()
            continue

        if endpoint is None:
            log(f"ignoring {message.get('method')} before initialize")
            if message.get("id") is not None:
                emit(error_reply(message, "not initialized"))
            continue

        pool.submit(forward, endpoint, message)

    log("stdin closed, exiting")
    pool.shutdown(wait=False)


if __name__ == "__main__":
    main()
