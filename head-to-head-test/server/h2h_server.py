#!/usr/bin/env python3
"""The head-to-head test's target server, and the place its results meet.

Python 3, no dependencies, so it runs on this Mac or on any web host:

    python3 server/h2h_server.py --port 8080 --data results
    python3 server/h2h_server.py --data results --report      # text report, no server
    python3 server/h2h_server.py --version                    # which version this file is

Its version is --server-version (or $H2H_VERSION) when given, else the one
`make server-release` stamped into its copy, else the git commit of the
checkout it runs from (-dirty when this file differs from that commit). It
is in every reply's Server header, at /h2h/version, and on every page.

Under /h2h:
  /bytes/N      N bytes (up to 4 MB), byte i = (i ^ (i >> 8)) & 0xFF, so the
                Palm can check every byte. Content-Length, then the connection
                closes. The apps add ?r=<run>&t=<test>&k=<rep>&s=<stack>, which
                only label the request log.
  /h?...        a run's header, sent by the app's Send button
  /r?...        one row of a run's results (o = index of the first value in v)
  /n?...        one diagnostic note of a run (i = its number, m = the text)
                All three carry s (stack), id (the run) and w (the tester's
                name): a run is the tester's, the stack's and the id's.
  /report       each tester's latest run of each stack side by side (HTML);
                ?who=a,b keeps those testers, ?runs=k1,k2 picks runs by key
  /report.txt   the same as text
  /runs.json    everything stored
  /log.csv      every request served: when, who, what, and how long it took here
  /version      the server's version, as text

Runs are kept in <data>/runs.json, the request log in <data>/requests.csv.
"""
import argparse
import csv
import datetime
import html
import json
import os
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote, urlsplit

MAX_BYTES = 4 * 1024 * 1024

# Set in the copy make server-release writes (--stamp); empty in the checkout.
VERSION = ""


def git_version(path):
    """The commit of the git checkout holding path, when git tracks it there."""
    where, name = os.path.split(os.path.abspath(path))

    def git(*args):
        return subprocess.run(["git", "-C", where, *args], capture_output=True, text=True, timeout=5)
    try:
        if git("ls-files", "--error-unmatch", name).returncode != 0:
            return ""
        head = git("rev-parse", "--short=7", "HEAD")
        if head.returncode != 0:
            return ""
        dirty = git("diff", "--quiet", "HEAD", "--", name).returncode != 0
    except (OSError, subprocess.SubprocessError):
        return ""
    return head.stdout.strip() + ("-dirty" if dirty else "")


def server_version(override=""):
    return override or VERSION or git_version(__file__) or "unknown"


def stamp(out, version):
    """Write a copy of this file whose VERSION is version."""
    with open(os.path.abspath(__file__)) as f:
        src = f.read()
    old = 'VERSION = ""\n'
    if old not in src:
        raise SystemExit("this copy is already stamped")
    with open(out, "w") as f:
        f.write(src.replace(old, f"VERSION = {version!r}\n", 1))

# The order the Palm sends a row's values in (suite.c, suite_upload).
FIELDS = [
    "reps", "fails", "chunk", "bytes", "us_total", "us_min", "us_max", "us_open",
    "us_ready", "us_first", "us_body", "us_close", "us_verify", "exchanges", "empty",
    "reads", "bytes_out", "bytes_in", "us_send", "us_wait", "us_recv", "resends", "last_err",
]
HEADER_FIELDS = ["n", "clkns", "baud", "def", "max", "run", "fw", "dev"]

STACKS = {
    "classic-serial": "Classic firmware, cradle serial",
    "nio-serial": "NIO, cradle serial",
    "nio-springboard": "NIO, Springboard",
    "nio-springboard-ram": "NIO, Springboard (RAM)",
    "niolib-springboard": "NioLib, Springboard",
    "niolib-springboard-ram": "NioLib, Springboard (RAM)",
    "niolib-network": "NioLib, Network (NIO on the Palm)",
    "niolib-serial": "NioLib, cradle serial",
    "niolib-usb": "NioLib, USB",
}

_pattern_cache = {}


def pattern(n):
    """The body for /bytes/n."""
    if n not in _pattern_cache:
        if len(_pattern_cache) > 32:
            _pattern_cache.clear()
        _pattern_cache[n] = bytes((i ^ (i >> 8)) & 0xFF for i in range(n))
    return _pattern_cache[n]


class Store:
    """Runs and the request log, on disk under one directory."""

    def __init__(self, directory):
        self.dir = directory
        self.lock = threading.Lock()
        os.makedirs(directory, exist_ok=True)
        self.runs_path = os.path.join(directory, "runs.json")
        self.log_path = os.path.join(directory, "requests.csv")
        self.runs = {}
        if os.path.exists(self.runs_path):
            with open(self.runs_path) as f:
                self.runs = json.load(f)

    def _save(self):
        tmp = self.runs_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.runs, f, indent=1, sort_keys=True)
        os.replace(tmp, self.runs_path)

    def _run(self, q, client):
        stack, run_id, who = q["s"], q["id"], tester(q)
        key = f"{who}/{stack}:{run_id}" if who else f"{stack}:{run_id}"
        run = self.runs.get(key)
        if run is None:
            run = self.runs[key] = {
                "key": key, "stack": stack, "id": run_id, "who": who, "client": client,
                "received": now_iso(), "meta": {}, "rows": {}, "order": [],
            }
        return run

    def header(self, q, client):
        with self.lock:
            run = self._run(q, client)
            for f in HEADER_FIELDS:
                if f in q:
                    run["meta"][f] = q[f]
            run["received"] = now_iso()
            self._save()

    def row(self, q, client):
        values = [int(v) for v in q.get("v", "").split(",") if v != ""]
        offset = int(q.get("o", "0"))
        with self.lock:
            run = self._run(q, client)
            row = run["rows"].setdefault(q["t"], {})
            if q["t"] not in run["order"]:
                run["order"].append(q["t"])
            for i, v in enumerate(values):
                if 0 <= offset + i < len(FIELDS):
                    row[FIELDS[offset + i]] = v
            self._save()

    def note(self, q, client):
        with self.lock:
            run = self._run(q, client)
            notes = run.setdefault("notes", {})
            notes[str(int(q.get("i", len(notes))))] = q.get("m", "")[:200]
            if "of" in q:
                run["notes_total"] = int(q["of"])
            self._save()

    def log(self, entry):
        with self.lock:
            new = not os.path.exists(self.log_path)
            with open(self.log_path, "a", newline="") as f:
                w = csv.writer(f)
                if new:
                    w.writerow(LOG_COLUMNS)
                w.writerow([entry.get(c, "") for c in LOG_COLUMNS])


LOG_COLUMNS = ["time", "client", "path", "stack", "run", "test", "rep", "bytes",
               "ms_request", "ms_send", "status"]


def now_iso():
    return datetime.datetime.now().isoformat(timespec="seconds")


def tester(q):
    """The tester's name from w: printable, spaces collapsed, no commas (they
    separate names in ?who=), at most 40 characters."""
    name = "".join(c for c in q.get("w", "") if c.isprintable()).replace(",", " ")
    return " ".join(name.split())[:40]


def query(url):
    return {k: v[-1] for k, v in parse_qs(urlsplit(url).query, keep_blank_values=True).items()}


class Handler(BaseHTTPRequestHandler):
    version = server_version()  # main() sets both from its arguments
    server_version = "h2h/" + version
    protocol_version = "HTTP/1.1"
    store = None

    def version_string(self):
        return self.server_version

    def setup(self):
        self.t_accept = time.monotonic()
        super().setup()

    def log_message(self, fmt, *args):
        if self.server.verbose:
            sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))

    def _send(self, status, body, ctype="text/plain; charset=utf-8"):
        t_request = time.monotonic()
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        self.close_connection = True
        t_done = time.monotonic()
        q = query(self.path)
        self.store.log({
            "time": now_iso(), "client": self.client_address[0], "path": urlsplit(self.path).path,
            "stack": q.get("s", ""), "run": q.get("r", q.get("id", "")), "test": q.get("t", ""),
            "rep": q.get("k", ""), "bytes": len(body),
            "ms_request": round((t_request - self.t_accept) * 1000, 1),
            "ms_send": round((t_done - t_request) * 1000, 1), "status": status,
        })

    def do_GET(self):
        parts = urlsplit(self.path)
        path = parts.path.rstrip("/")
        if not path.startswith("/h2h"):
            return self._send(404, b"not found\n")
        path = path[len("/h2h"):]
        q = query(self.path)
        client = self.client_address[0]
        try:
            if path.startswith("/bytes/"):
                n = int(path[len("/bytes/"):])
                if not 0 <= n <= MAX_BYTES:
                    return self._send(400, b"size out of range\n")
                return self._send(200, pattern(n), "application/octet-stream")
            if path == "/h":
                self.store.header(q, client)
                return self._send(200, b"ok\n")
            if path == "/r":
                self.store.row(q, client)
                return self._send(200, b"ok\n")
            if path == "/n":
                self.store.note(q, client)
                return self._send(200, b"ok\n")
            if path == "/version":
                return self._send(200, (self.version + "\n").encode())
            if path == "/runs.json":
                return self._send(200, json.dumps(self.store.runs, indent=1).encode(), "application/json")
            if path == "/log.csv":
                data = open(self.store.log_path, "rb").read() if os.path.exists(self.store.log_path) else b""
                return self._send(200, data, "text/csv; charset=utf-8")
            if path in ("/report", "/report.txt"):
                runs = pick_runs(self.store.runs, q.get("runs", ""), q.get("who", ""))
                log = read_log(self.store.log_path)
                if path == "/report":
                    return self._send(200, html_report(runs, log, self.version).encode(),
                                      "text/html; charset=utf-8")
                return self._send(200, text_report(runs, log, self.version).encode())
            if path == "":
                return self._send(200, index_page(self.version, self.store.runs).encode(),
                                  "text/html; charset=utf-8")
        except (KeyError, ValueError) as e:
            return self._send(400, f"bad request: {e}\n".encode())
        return self._send(404, b"not found\n")


# ---------------------------------------------------------------- reporting

def pick_runs(runs, wanted, who=""):
    """The runs named in wanted (keys or ids), else each tester's latest run of
    each stack; who (names, comma-separated, any case) keeps those testers'."""
    runs = runs.values() if isinstance(runs, dict) else runs
    if who:
        names = {w.strip().lower() for w in who.split(",") if w.strip()}
        runs = [r for r in runs if r.get("who", "").lower() in names]
    if wanted:
        names = [w for w in wanted.split(",") if w]
        return [r for r in runs if r["key"] in names or r["id"] in names]
    latest = {}
    for r in runs:
        k = (r.get("who", ""), r["stack"])
        if k not in latest or r["received"] > latest[k]["received"]:
            latest[k] = r
    order = list(STACKS)
    return sorted(latest.values(), key=lambda r: (order.index(r["stack"]) if r["stack"] in order else 99,
                                                  r.get("who", "").lower()))


def read_log(path):
    if not os.path.exists(path):
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def server_side(log, run):
    """Per test: the server's mean time to send the body (ms), from the log."""
    out = {}
    for e in log:
        if e.get("stack") == run["stack"] and e.get("run") == run["id"] and e.get("test"):
            out.setdefault(e["test"], []).append(float(e["ms_send"] or 0))
    return {t: sum(v) / len(v) for t, v in out.items()}


def ms(us):
    if us is None:
        return "-"
    if us < 10000:
        return f"{us / 1000:.1f} ms"
    if us < 1000000:
        return f"{us / 1000:.0f} ms"
    return f"{us / 1000000:.2f} s"


def kbs(row, key="us_body"):
    us = row.get(key, 0)
    return row.get("bytes", 0) * 1e6 / us / 1024 if us else 0.0


def good(row):
    return row and row.get("reps", 0) > row.get("fails", 0)


def clock_res(run):
    ns = run["meta"].get("clkns")
    if ns is None:
        return "?"
    ns = int(ns)
    return f"{ns} ns" if ns < 1000 else f"{ns / 1000:.1f} us" if ns < 1000000 else f"{ns / 1000000:.0f} ms"


def stack_label(run):
    return STACKS.get(run["stack"], run["stack"])


def label(run):
    """The stack, and the tester when the run has one."""
    who = run.get("who", "")
    return stack_label(run) + (f" \u00b7 {who}" if who else "")


def gets(run):
    return [t for t in run["order"] if t.startswith("get") or t == "page"]


def sweeps(run):
    return sorted((t for t in run["order"] if t.startswith("sw")), key=lambda t: int(t[2:]))


def split(row):
    """Whether the link time was split: NioLib makes its exchanges inside its own
    code, where the app's probe cannot reach, so its runs count none."""
    return row.get("exchanges", 0) > 0


def breakdown(row):
    """Where a download's time went, in us: link send, wait, recv, and the Palm's own."""
    whole = sum(row.get(k, 0) for k in ("us_open", "us_ready", "us_body", "us_close"))
    send, wait, recv = row.get("us_send", 0), row.get("us_wait", 0), row.get("us_recv", 0)
    palm = max(whole - send - wait - recv, 0)
    return whole, {"link send": send, "device wait": wait, "link recv": recv, "palm": palm}


def biggest_get(run, runs=None):
    """The run's largest good GET, among the sizes every run in runs has, so
    the runs are split on the same download."""
    tests = [t for t in gets(run) if good(run["rows"].get(t))]
    if runs:
        common = [t for t in tests if all(good(r["rows"].get(t)) for r in runs)]
        tests = common or tests
    rows = [run["rows"][t] for t in tests]
    return max(rows, key=lambda r: r.get("bytes", 0)) if rows else None


def notes(run):
    """A run's diagnostic notes in order, and how many the Palm had in all."""
    kept = run.get("notes", {})
    return [kept[k] for k in sorted(kept, key=int)], run.get("notes_total", len(kept))


def text_report(runs, log, version=""):
    tail = f"h2h server {version or server_version()}\n"
    if not runs:
        return "No runs yet: run a test on a Palm, then tap Send.\n" + tail
    w = 18
    lines = []
    head = "".ljust(w) + "".join(stack_label(r)[:24].ljust(26) for r in runs)
    lines += ["Head-to-head: " + ", ".join(f"{r['stack']} run {r['id']}" + (f" ({r['who']})" if r.get("who") else "")
                                           for r in runs), ""]
    lines.append("RUNS")
    for name, get in (
        ("tester", lambda r: r.get("who") or "-"),
        ("device", lambda r: r["meta"].get("dev", "?")),
        ("firmware", lambda r: r["meta"].get("fw", "?")),
        ("clock res", lambda r: clock_res(r)),
        ("line", lambda r: f"{r['meta'].get('baud')} baud" if r["meta"].get("baud") not in (None, "0") else "CS1 bus"),
        ("read size", lambda r: f"{r['meta'].get('def', '?')} (max {r['meta'].get('max', '?')})"),
        ("received", lambda r: r["received"]),
    ):
        lines.append(name.ljust(w) + "".join(str(get(r))[:24].ljust(26) for r in runs))
    lines += ["", "LINK ONLY (no network)", head]
    for test in ("ping1", "ping"):
        cells = []
        for r in runs:
            row = r["rows"].get(test)
            cells.append(ms(row.get("us_total")) if good(row) else "-")
        lines.append((test if test == "ping" else "first call").ljust(w) + "".join(c.ljust(26) for c in cells))
    cells = []
    for r in runs:
        row = r["rows"].get("ping")
        if good(row):
            cells.append(f"s{row.get('us_send', 0) / 1000:.1f} w{row.get('us_wait', 0) / 1000:.1f} "
                         f"r{row.get('us_recv', 0) / 1000:.1f} ms")
        else:
            cells.append("-")
    lines.append("  send/wait/recv".ljust(w) + "".join(c.ljust(26) for c in cells))
    lines += ["", "HTTP GET (total time, body rate)", head]
    tests = []
    for r in runs:
        for t in gets(r):
            if t not in tests:
                tests.append(t)
    for t in tests:
        cells = []
        for r in runs:
            row = r["rows"].get(t)
            if not good(row):
                cells.append("failed" if row else "-")
            elif row.get("bytes", 0) <= 64:
                cells.append(f"{ms(row['us_total'])} (ttfb {ms(row.get('us_first'))})")
            else:
                cells.append(f"{ms(row['us_total'])}  {kbs(row):.1f} KB/s")
        lines.append(t.ljust(w) + "".join(c.ljust(26) for c in cells))
    lines += ["", "WHERE THE BIGGEST GET ALL RUNS HAVE WENT", head]
    parts = {}
    for r in runs:
        row = biggest_get(r, runs)
        if row:
            whole, b = breakdown(row)
            parts[r["key"]] = (row, whole, b)
    for name in ("link send", "device wait", "link recv", "palm"):
        cells = []
        for r in runs:
            if r["key"] in parts:
                row, whole, b = parts[r["key"]]
                if not split(row):
                    cells.append("not split (library)" if name != "palm" else "-")
                    continue
                cells.append(f"{ms(b[name])} ({100 * b[name] / whole:.0f}%)" if whole else "-")
            else:
                cells.append("-")
        lines.append(name.ljust(w) + "".join(c.ljust(26) for c in cells))
    for name, fn in (
        ("exchanges", lambda row: f"{row.get('exchanges', 0)} ({row.get('empty', 0)} empty)"),
        ("per exchange", lambda row: ms((row.get("us_send", 0) + row.get("us_wait", 0) + row.get("us_recv", 0))
                                        / row["exchanges"]) if split(row) else "-"),
        ("bytes per read", lambda row: f"{row.get('bytes', 0) / max(row.get('reads', 1), 1):.0f}"),
        ("wire in/out", lambda row: f"{row.get('bytes_in', 0)}/{row.get('bytes_out', 0)} B"),
    ):
        cells = [fn(parts[r["key"]][0]) if r["key"] in parts else "-" for r in runs]
        lines.append(name.ljust(w) + "".join(c.ljust(26) for c in cells))
    srv = [server_side(log, r) for r in runs]
    cells = []
    for r, s in zip(runs, srv):
        row = biggest_get(r, runs)
        t = next((t for t in gets(r) if r["rows"].get(t) is row), None)
        cells.append(f"{s[t]:.1f} ms" if t in s else "-")
    lines.append("server send".ljust(w) + "".join(c.ljust(26) for c in cells))
    lines += ["", "READ SIZE SWEEP (16 KB body, KB/s)", head]
    sizes = sorted({int(t[2:]) for r in runs for t in sweeps(r)})
    for n in sizes:
        cells = []
        for r in runs:
            row = r["rows"].get(f"sw{n}")
            if not good(row):
                cells.append("failed" if row else "-")
            else:
                star = "*" if str(n) == str(r["meta"].get("def")) else ""
                cells.append(f"{kbs(row):.1f}{star}")
        lines.append(str(n).ljust(w) + "".join(c.ljust(26) for c in cells))
    lines.append("(* the read size the shipped apps use)")
    for r in runs:
        kept, total = notes(r)
        if total:
            lines += ["", f"LINK NOTES: {label(r)} run {r['id']} ({total} in all)"]
            lines += ["  " + n for n in kept]
    return "\n".join(lines).rstrip("\n") + "\n\n" + tail


CSS = """
:root{--bg:#fbfbfd;--fg:#1d1f24;--muted:#646a76;--line:#dcdfe6;--card:#fff;
--s1:#3b82f6;--s2:#f59e0b;--s3:#10b981;--s4:#a855f7}
@media (prefers-color-scheme:dark){:root{--bg:#121318;--fg:#e8eaf0;--muted:#9aa1ae;--line:#2c2f38;--card:#1a1c22}}
body{background:var(--bg);color:var(--fg);font:15px/1.45 -apple-system,system-ui,sans-serif;margin:0;padding:24px 16px}
main{max-width:1100px;margin:auto}h1{font-size:22px;margin:0 0 4px}h2{font-size:17px;margin:28px 0 8px}
p.note{color:var(--muted);margin:4px 0 12px}
table{border-collapse:collapse;width:100%;background:var(--card);border:1px solid var(--line);border-radius:8px;overflow:hidden}
th,td{padding:6px 10px;border-bottom:1px solid var(--line);text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
th:first-child,td:first-child{text-align:left}thead th{color:var(--muted);font-weight:600}
.wrap{overflow-x:auto}.bar{display:flex;height:16px;border-radius:3px;overflow:hidden;min-width:160px}
.bar span{display:block;height:100%}.k{display:inline-block;width:10px;height:10px;border-radius:2px;margin:0 4px 0 12px}
.best{font-weight:700}.fail{color:#dc2626}
:root{--c1:#2a78d6;--c2:#eb6834;--c3:#1baf7a;--c4:#eda100;--c5:#e87ba4;--c6:#008300;--c7:#6250d6;--c8:#e34948}
@media (prefers-color-scheme:dark){:root{--c1:#3987e5;--c2:#d95926;--c3:#199e70;--c4:#c98500;--c5:#d55181;
--c6:#008300;--c7:#9085e9;--c8:#e66767}}
.sweep{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px 12px;margin:12px 0}
.sweep .bar2{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:6px}
.sweep .bar2 .t{color:var(--muted);font-size:13px;margin-right:auto}
.sweep button{font:inherit;font-size:13px;color:var(--fg);background:var(--bg);border:1px solid var(--line);
border-radius:6px;padding:3px 10px;cursor:pointer}.sweep button[aria-pressed=true]{border-color:var(--muted)}
.sweep .plot{position:relative;height:440px;min-height:260px;resize:vertical;overflow:hidden}
.sweep .plot>svg{display:block;width:100%;height:100%}.sweep .tip svg{flex:none}
.sweep .legend{display:flex;flex-wrap:wrap;gap:2px 6px;margin-top:8px}
.sweep .legend button{display:flex;align-items:center;gap:6px;border-color:transparent;background:none;padding:2px 6px}
.sweep .legend button:hover{border-color:var(--line)}.sweep .legend button[aria-pressed=false]{opacity:.4}
.sweep .legend small{color:var(--muted)}
.sweep .tip{position:absolute;pointer-events:none;background:var(--card);border:1px solid var(--line);border-radius:6px;
padding:6px 8px;font-size:12px;box-shadow:0 2px 8px rgba(0,0,0,.15);display:none;white-space:nowrap;z-index:2}
.sweep .tip b{display:block;margin-bottom:2px}.sweep .tip div{display:flex;align-items:center;gap:6px}
.sweep .tip span.v{margin-left:auto;padding-left:12px;font-variant-numeric:tabular-nums}
.sweep.big{position:fixed;inset:12px;z-index:10;margin:0;display:flex;flex-direction:column;box-shadow:0 8px 40px rgba(0,0,0,.35)}
.sweep.big .plot{flex:1;height:auto;resize:none}
"""

PART_COLORS = {"link send": "var(--s1)", "device wait": "var(--s2)", "link recv": "var(--s3)", "palm": "var(--s4)"}


def table(head, rows):
    """An HTML table: head is text, rows hold HTML already escaped."""
    s = ["<div class=wrap><table><thead><tr>"] + [f"<th>{html.escape(h)}</th>" for h in head]
    s.append("</tr></thead><tbody>")
    for r in rows:
        s.append("<tr>" + "".join(f"<td>{c}</td>" for c in r) + "</tr>")
    s.append("</tbody></table></div>")
    return "".join(s)


def html_report(runs, log, version=""):
    e = html.escape
    out = [f"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
           f"<title>H2H report</title><style>{CSS}</style></head><body><main>",
           "<h1>FujiNet on the Palm: head to head</h1>",
           f"<p class=note>{len(runs)} runs: each tester's latest of each stack, unless ?who= or ?runs= "
           "narrows them. "
           "Times are the Palm's own, averaged over the repetitions that succeeded. "
           f"h2h server <code>{e(version or server_version())}</code>.</p>"]
    if not runs:
        out.append("<p>No runs yet: run a test on a Palm, then tap Send.</p></main></body></html>")
        return "".join(out)

    head = [""] + [label(r) for r in runs]
    out.append("<h2>The runs</h2>")
    meta_rows = []
    for name, get in (
        ("Tester", lambda r: r.get("who") or "-"),
        ("Run", lambda r: f"{r['id']} ({r['received']})"),
        ("Device", lambda r: r["meta"].get("dev", "?")),
        ("Firmware", lambda r: r["meta"].get("fw", "?")),
        ("Clock resolution", lambda r: clock_res(r)),
        ("Line", lambda r: f"{r['meta'].get('baud')} baud" if r["meta"].get("baud") not in (None, "0") else "CS1 bus"),
        ("Read size (max)", lambda r: f"{r['meta'].get('def', '?')} ({r['meta'].get('max', '?')})"),
        ("Whole run", lambda r: ms(int(r["meta"].get("run", 0)))),
    ):
        meta_rows.append([name] + [e(str(get(r))) for r in runs])
    out.append(table(head, meta_rows))

    out.append("<h2>Link only</h2><p class=note>The smallest round trip each stack has, with no network: "
               "classic N1: STATUS, NIO Clock.GetTime.</p>")
    rows = []
    for name, fn in (
        ("Round trip", lambda row: ms(row.get("us_total"))),
        ("min / max", lambda row: f"{ms(row.get('us_min'))} / {ms(row.get('us_max'))}"),
        ("send / wait / recv", lambda row: " / ".join(ms(row.get(k, 0)) for k in ("us_send", "us_wait", "us_recv"))),
        ("wire bytes out / in", lambda row: f"{row.get('bytes_out', 0)} / {row.get('bytes_in', 0)}"),
    ):
        rows.append([name] + [fn(r["rows"]["ping"]) if good(r["rows"].get("ping")) else "-" for r in runs])
    rows.append(["First call (wake-up)"] + [ms(r["rows"]["ping1"].get("us_total")) if r["rows"].get("ping1") else "-"
                                             for r in runs])
    out.append(table(head, rows))

    out.append("<h2>HTTP GET</h2><p class=note>Total time per request and the body's rate "
               "(from the reply starting to its last byte).</p>")
    tests = []
    for r in runs:
        for t in gets(r):
            if t not in tests:
                tests.append(t)
    rows = []
    for t in tests:
        cells = [t]
        for r in runs:
            row = r["rows"].get(t)
            if not good(row):
                cells.append("<span class=fail>failed</span>" if row else "-")
            elif row.get("bytes", 0) <= 64:
                cells.append(f"{ms(row['us_total'])} <small>ttfb {ms(row.get('us_first'))}</small>")
            else:
                cells.append(f"{ms(row['us_total'])} &middot; <b>{kbs(row):.1f} KB/s</b>")
        rows.append(cells)
    out.append(table(head, rows))

    out.append("<h2>Where the time goes</h2><p class=note>The biggest GET of each run. "
               "<b>link send</b>: the request leaving the Palm; <b>device wait</b>: from then until the reply "
               "starts (the FujiNet, its link and the network); <b>link recv</b>: the reply coming in; "
               "<b>palm</b>: everything between exchanges, checking the data included.</p>")
    out.append("<p class=note>" + "".join(f"<span class=k style='background:{c}'></span>{e(n)}"
                                          for n, c in PART_COLORS.items()) + "</p>")
    rows = []
    srv = {r["key"]: server_side(log, r) for r in runs}
    for r in runs:
        row = biggest_get(r, runs)
        if not row:
            rows.append([e(label(r)), "-", "", "", "", ""])
            continue
        whole, b = breakdown(row)
        if not split(row):
            t = next((t for t in gets(r) if r["rows"].get(t) is row), "")
            rows.append([f"{e(label(r))}<br><small>{e(t)}, {ms(whole)}</small>",
                         "not split: the exchanges run inside the library", "-", "-",
                         f"{row.get('bytes', 0) / max(row.get('reads', 1), 1):.0f} B/read",
                         f"{srv[r['key']][t]:.1f} ms" if t in srv[r["key"]] else "-"])
            continue
        bar = "<div class=bar>" + "".join(
            f"<span title='{e(n)} {ms(v)}' style='width:{100 * v / whole:.2f}%;background:{PART_COLORS[n]}'></span>"
            for n, v in b.items() if whole) + "</div>"
        t = next((t for t in gets(r) if r["rows"].get(t) is row), "")
        per = (row.get("us_send", 0) + row.get("us_wait", 0) + row.get("us_recv", 0)) / max(row.get("exchanges", 1), 1)
        rows.append([f"{e(label(r))}<br><small>{e(t)}, {ms(whole)}</small>", bar,
                     " / ".join(f"{100 * v / whole:.0f}%" for v in b.values()) if whole else "-",
                     f"{row.get('exchanges', 0)} ({row.get('empty', 0)} empty), {ms(per)} each",
                     f"{row.get('bytes', 0) / max(row.get('reads', 1), 1):.0f} B/read, "
                     f"wire {row.get('bytes_in', 0)} in",
                     f"{srv[r['key']][t]:.1f} ms" if t in srv[r["key"]] else "-"])
    out.append(table(["Run", "Split", "send / wait / recv / palm", "Exchanges", "Reads", "Server send"], rows))

    sizes = sorted({int(t[2:]) for r in runs for t in sweeps(r)})
    if sizes:
        out.append("<h2>Read size sweep</h2><p class=note>A 16 KB body read with each read size: "
                   "the body rate in KB/s. * is the size the shipped apps use; bold is the fastest.</p>")
        rows = []
        best = {}
        for r in runs:
            rates = {n: kbs(r["rows"][f"sw{n}"]) for n in sizes if good(r["rows"].get(f"sw{n}"))}
            best[r["key"]] = max(rates, key=rates.get) if rates else None
        for n in sizes:
            cells = [str(n)]
            for r in runs:
                row = r["rows"].get(f"sw{n}")
                if not good(row):
                    cells.append("<span class=fail>failed</span>" if row else "-")
                    continue
                star = "*" if str(n) == str(r["meta"].get("def")) else ""
                cls = " class=best" if best[r["key"]] == n else ""
                cells.append(f"<span{cls}>{kbs(row):.1f}{star}</span>")
            rows.append(cells)
        out.append(table(["Read size"] + [label(r) for r in runs], rows))
        out.append(sweep_chart(runs, sizes))

    out.append("<h2>Every number</h2>")
    for r in runs:
        out.append(f"<p class=note><b>{e(label(r))}</b> run {e(r['id'])}</p>")
        rows = [[e(t)] + [str(r["rows"][t].get(f, "")) for f in FIELDS] for t in r["order"]]
        out.append(table(["test"] + FIELDS, rows))
        kept, total = notes(r)
        if total:
            out.append(f"<p class=note>Link notes ({total} in all):</p><pre>"
                       + e("\n".join(kept)) + "</pre>")
    out.append("<p class=note>Raw data: <a href='runs.json'>runs.json</a>, <a href='log.csv'>log.csv</a>, "
               "<a href='report.txt'>report.txt</a></p></main></body></html>")
    return "".join(out)


# The sweep chart's encoding. Colour follows the stack's family (link and library), in the palette's fixed order, so
# a stack keeps its colour whatever runs are shown; runs of one family differ by dash and marker. Up to 8 families,
# each with as many runs as there are; the ninth family on shares slot 8.
FAMILY_SLOT = {"nio-springboard": 1, "nio-springboard-ram": 1, "niolib-springboard": 2, "niolib-springboard-ram": 2,
               "nio-serial": 3, "classic-serial": 4, "niolib-network": 5, "niolib-serial": 6, "niolib-usb": 7}
DASHES = ["", "7 4", "2 3", "10 3 2 3", "4 2", "1 3"]
MARKERS = ["circle", "square", "triangle", "diamond"]


def run_styles(runs):
    """Per run: its colour slot, dash and marker, numbered within its family in the order given."""
    seen, out = {}, []
    for r in runs:
        slot = FAMILY_SLOT.get(r["stack"], 8)
        i = seen.get(slot, 0)
        seen[slot] = i + 1
        out.append((slot, DASHES[i % len(DASHES)], MARKERS[(i // len(DASHES) + i) % len(MARKERS)]))
    return out


def marker_svg(kind, x, y, r, fill, ring):
    if kind == "square":
        return f"<rect x='{x - r:.1f}' y='{y - r:.1f}' width='{2 * r}' height='{2 * r}' fill='{fill}' stroke='{ring}' stroke-width='2'/>"
    if kind == "triangle":
        return (f"<polygon points='{x:.1f},{y - r - 1:.1f} {x + r + 1:.1f},{y + r:.1f} {x - r - 1:.1f},{y + r:.1f}' "
                f"fill='{fill}' stroke='{ring}' stroke-width='2'/>")
    if kind == "diamond":
        return (f"<polygon points='{x:.1f},{y - r - 1:.1f} {x + r + 1:.1f},{y:.1f} {x:.1f},{y + r + 1:.1f} {x - r - 1:.1f},{y:.1f}' "
                f"fill='{fill}' stroke='{ring}' stroke-width='2'/>")
    return f"<circle cx='{x:.1f}' cy='{y:.1f}' r='{r}' fill='{fill}' stroke='{ring}' stroke-width='2'/>"


def swatch_svg(slot, dash, marker):
    c = f"var(--c{slot})"
    d = f" stroke-dasharray='{dash}'" if dash else ""
    return (f"<svg width='34' height='14' aria-hidden='true'><line x1='1' y1='7' x2='33' y2='7' stroke='{c}' "
            f"stroke-width='2'{d}/>{marker_svg(marker, 17, 7, 4, c, 'var(--card)')}</svg>")


def sweep_chart(runs, sizes):
    """The sweep as a chart: every run a line of its body rate (KB/s) by read size. It is drawn in the page by
    SWEEP_JS from the data here, so it fits its box: a log scale (default) or linear, a crosshair listing every run
    at the read size under the pointer, a legend that hides or picks out runs, and Bigger (the whole window) or the
    plot's drag handle for a taller one. The table above holds the same numbers."""
    styles = run_styles(runs)
    data = {"sizes": sizes, "runs": []}
    for r, (slot, dash, marker) in zip(runs, styles):
        pts, fails = [], []
        for n in sizes:
            row = r["rows"].get(f"sw{n}")
            if good(row):
                pts.append([n, round(kbs(row), 2)])
            elif row:
                fails.append(n)
        data["runs"].append({"label": label(r), "id": r["id"], "slot": slot, "dash": dash, "marker": marker,
                             "def": int(r["meta"]["def"]) if str(r["meta"].get("def", "")).isdigit() else None,
                             "points": pts, "fails": fails})
    rates = [v for d in data["runs"] for _, v in d["points"] if v > 0]
    data["log"] = bool(rates) and max(rates) / min(rates) > 8
    legend = "".join(
        f"<button type=button aria-pressed=true data-i={i}>{swatch_svg(slot, dash, marker)}"
        f"<span>{html.escape(label(r))} <small>run {html.escape(r['id'])}</small></span></button>"
        for i, (r, (slot, dash, marker)) in enumerate(zip(runs, styles)))
    blob = json.dumps(data).replace("</", "<\\/")
    return ("<div class=sweep id=sweep><div class=bar2><span class=t>Body rate (KB/s) by read size, every run. "
            "Hover for values; tap a run below to hide it.</span>"
            "<button type=button class=log aria-pressed=false>Linear scale</button>"
            "<button type=button class=bigger aria-pressed=false>Bigger</button></div>"
            "<div class=plot role=img aria-label='Body rate in KB/s by read size, one line per run'>"
            "<div class=tip></div></div>"
            f"<div class=legend>{legend}</div></div>"
            f"<script type=application/json id=sweep-data>{blob}</script><script>{SWEEP_JS}</script>")


SWEEP_JS = r"""
(function () {
  var root = document.getElementById('sweep'), data = JSON.parse(document.getElementById('sweep-data').textContent);
  var plot = root.querySelector('.plot'), tip = root.querySelector('.tip'), logBtn = root.querySelector('.log');
  var bigBtn = root.querySelector('.bigger'), legend = root.querySelectorAll('.legend button');
  var NS = 'http://www.w3.org/2000/svg', sizes = data.sizes, runs = data.runs;
  var useLog = data.log, hidden = {}, focus = -1, svg = null, geom = null;
  function el(tag, attrs, parent) {
    var e = document.createElementNS(NS, tag);
    for (var k in attrs) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  }
  function marker(kind, x, y, r, fill, ring, parent) {
    var a = {fill: fill, stroke: ring, 'stroke-width': 2};
    if (kind === 'square') { a.x = x - r; a.y = y - r; a.width = 2 * r; a.height = 2 * r; return el('rect', a, parent); }
    if (kind === 'triangle') { a.points = [x, y - r - 1, x + r + 1, y + r, x - r - 1, y + r].join(' '); return el('polygon', a, parent); }
    if (kind === 'diamond') { a.points = [x, y - r - 1, x + r + 1, y, x, y + r + 1, x - r - 1, y].join(' '); return el('polygon', a, parent); }
    a.cx = x; a.cy = y; a.r = r; return el('circle', a, parent);
  }
  function niceTicks(lo, hi) {
    var t = [], k, m;
    if (useLog) {
      for (k = Math.floor(Math.log10(lo)); k <= Math.ceil(Math.log10(hi)); k++)
        [1, 2, 5].forEach(function (m) { var v = m * Math.pow(10, k); if (v >= lo * 0.999 && v <= hi * 1.001) t.push(v); });
      return t;
    }
    var step = Math.pow(10, Math.floor(Math.log10(hi / 5)));
    [1, 2, 2.5, 5, 10].some(function (m) { if (hi / (step * m) <= 6) { step *= m; return true; } });
    for (k = 0; k <= hi + 1e-9; k += step) t.push(+k.toFixed(6));
    return t;
  }
  function fmt(v) { return v >= 100 ? v.toFixed(0) : v >= 10 ? v.toFixed(1) : v.toFixed(2); }
  function draw() {
    var W = plot.clientWidth, H = plot.clientHeight;
    if (!W || !H) return;
    var vals = [];
    runs.forEach(function (r, i) { if (!hidden[i]) r.points.forEach(function (p) { if (p[1] > 0) vals.push(p[1]); }); });
    if (!vals.length) vals = [1];
    var vmax = Math.max.apply(null, vals), vmin = Math.min.apply(null, vals), lo, hi;
    if (useLog) {
      lo = Math.pow(10, Math.floor(Math.log10(vmin) * 2) / 2); hi = Math.pow(10, Math.ceil(Math.log10(vmax) * 2) / 2);
      if (hi <= lo) hi = lo * 10;
    } else { lo = 0; hi = niceTicks(0, vmax * 1.08).slice(-1)[0] || 1; }
    var ticks = niceTicks(lo, hi), left = 52, right = 16, top = 26, bottom = 34;
    var xs = sizes.map(function (n) { return Math.log2(n); }), x0 = Math.min.apply(null, xs), x1 = Math.max.apply(null, xs);
    function X(n) { return left + (x1 > x0 ? (Math.log2(n) - x0) / (x1 - x0) : 0.5) * (W - left - right); }
    function Y(v) {
      var f = useLog ? (Math.log10(Math.max(v, lo)) - Math.log10(lo)) / (Math.log10(hi) - Math.log10(lo)) : v / hi;
      return H - bottom - f * (H - top - bottom);
    }
    if (svg) svg.remove();
    svg = el('svg', {width: W, height: H}, null);
    plot.insertBefore(svg, tip);
    ticks.forEach(function (v) {
      el('line', {x1: left, x2: W - right, y1: Y(v), y2: Y(v), stroke: 'var(--line)', 'stroke-width': 1}, svg);
      var t = el('text', {x: left - 8, y: Y(v) + 4, 'text-anchor': 'end', 'font-size': 11, fill: 'var(--muted)'}, svg);
      t.textContent = fmt(v);
    });
    var lastX = -1e9;
    sizes.forEach(function (n) {
      var x = X(n);
      el('line', {x1: x, x2: x, y1: H - bottom, y2: H - bottom + 4, stroke: 'var(--muted)'}, svg);
      if (x - lastX < 34) return;
      lastX = x;
      var t = el('text', {x: x, y: H - bottom + 17, 'text-anchor': 'middle', 'font-size': 11, fill: 'var(--muted)'}, svg);
      t.textContent = n;
    });
    var ax = el('text', {x: left, y: H - 4, 'font-size': 11, fill: 'var(--muted)'}, svg);
    ax.textContent = 'read size (bytes, log scale)';
    var ay = el('text', {x: 4, y: 12, 'font-size': 11, fill: 'var(--muted)'}, svg);
    ay.textContent = 'KB/s' + (useLog ? ' (log scale)' : '');
    var cross = el('line', {y1: top, y2: H - bottom, stroke: 'var(--muted)', 'stroke-dasharray': '3 3', visibility: 'hidden'}, svg);
    var order = runs.map(function (r, i) { return i; }).filter(function (i) { return i !== focus; });
    if (focus >= 0) order.push(focus);
    order.forEach(function (i) {
      var r = runs[i], c = 'var(--c' + r.slot + ')';
      if (hidden[i] || !r.points.length) return;
      var g = el('g', {opacity: focus >= 0 && focus !== i ? 0.18 : 1}, svg);
      el('polyline', {fill: 'none', stroke: c, 'stroke-width': focus === i ? 3 : 2, 'stroke-dasharray': r.dash,
        'stroke-linejoin': 'round', points: r.points.map(function (p) { return X(p[0]) + ',' + Y(p[1]); }).join(' ')}, g);
      r.points.forEach(function (p) {
        if (p[0] === r.def) el('circle', {cx: X(p[0]), cy: Y(p[1]), r: 8, fill: 'none', stroke: c, 'stroke-width': 1.5}, g);
        marker(r.marker, X(p[0]), Y(p[1]), 4, c, 'var(--card)', g);
      });
    });
    geom = {X: X, cross: cross, W: W};
  }
  function showTip(ev) {
    if (!geom) return;
    var box = plot.getBoundingClientRect(), mx = ev.clientX - box.left, my = ev.clientY - box.top, best = null, bd = 1e9;
    sizes.forEach(function (n) { var d = Math.abs(geom.X(n) - mx); if (d < bd) { bd = d; best = n; } });
    if (best === null || bd > 40) { hideTip(); return; }
    geom.cross.setAttribute('x1', geom.X(best)); geom.cross.setAttribute('x2', geom.X(best));
    geom.cross.setAttribute('visibility', 'visible');
    var rows = [];
    runs.forEach(function (r, i) {
      if (hidden[i]) return;
      var p = r.points.filter(function (q) { return q[0] === best; })[0];
      if (p) rows.push({r: r, v: p[1], t: fmt(p[1]) + ' KB/s' + (best === r.def ? ' (shipped)' : '')});
      else if (r.fails.indexOf(best) >= 0) rows.push({r: r, v: -1, t: 'failed'});
    });
    rows.sort(function (a, b) { return b.v - a.v; });
    tip.textContent = '';
    var h = document.createElement('b'); h.textContent = best + '-byte reads'; tip.appendChild(h);
    if (!rows.length) { var n0 = document.createElement('div'); n0.textContent = 'no run read this size'; tip.appendChild(n0); }
    rows.forEach(function (o) {
      var d = document.createElement('div'), s = document.createElementNS(NS, 'svg');
      s.setAttribute('width', 22); s.setAttribute('height', 10);
      el('line', {x1: 0, y1: 5, x2: 22, y2: 5, stroke: 'var(--c' + o.r.slot + ')', 'stroke-width': 2, 'stroke-dasharray': o.r.dash}, s);
      var a = document.createElement('span'); a.textContent = o.r.label + ' #' + o.r.id;
      var v = document.createElement('span'); v.className = 'v'; v.textContent = o.t;
      d.appendChild(s); d.appendChild(a); d.appendChild(v); tip.appendChild(d);
    });
    tip.style.display = 'block';
    var tw = tip.offsetWidth, th = tip.offsetHeight, tx = mx + 14, ty = my + 14;
    if (tx + tw > box.width) tx = Math.max(4, mx - tw - 14);
    if (ty + th > box.height) ty = Math.max(4, box.height - th - 4);
    tip.style.left = tx + 'px'; tip.style.top = ty + 'px';
  }
  function hideTip() { tip.style.display = 'none'; if (geom) geom.cross.setAttribute('visibility', 'hidden'); }
  plot.addEventListener('mousemove', showTip);
  plot.addEventListener('mouseleave', hideTip);
  plot.addEventListener('touchstart', function (e) { showTip(e.touches[0]); }, {passive: true});
  function setScale(log) { useLog = log; logBtn.textContent = log ? 'Linear scale' : 'Log scale'; draw(); }
  logBtn.addEventListener('click', function () { setScale(!useLog); });
  function setBig(on) {
    root.classList.toggle('big', on); bigBtn.setAttribute('aria-pressed', on);
    bigBtn.textContent = on ? 'Smaller' : 'Bigger'; draw();
  }
  bigBtn.addEventListener('click', function () { setBig(!root.classList.contains('big')); });
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape' && root.classList.contains('big')) setBig(false); });
  Array.prototype.forEach.call(legend, function (b) {
    var i = +b.getAttribute('data-i');
    b.addEventListener('click', function () { hidden[i] = !hidden[i]; b.setAttribute('aria-pressed', !hidden[i]); draw(); });
    b.addEventListener('mouseenter', function () { if (!hidden[i]) { focus = i; draw(); } });
    b.addEventListener('mouseleave', function () { focus = -1; draw(); });
  });
  if (window.ResizeObserver) new ResizeObserver(function () { draw(); }).observe(plot);
  else window.addEventListener('resize', draw);
  setScale(useLog);
})();
"""


def index_page(version="", runs=None):
    e = html.escape
    out = ["<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
           "<title>h2h</title><style>" + CSS + "</style><main>"
           "<h1>h2h server</h1><p class=note>Version <code>" + e(version or server_version()) +
           "</code></p><p>Targets: <code>/h2h/bytes/N</code>. Results: "
           "<a href='/h2h/report'>report</a>, <a href='/h2h/report.txt'>report.txt</a>, "
           "<a href='/h2h/runs.json'>runs.json</a>, <a href='/h2h/log.csv'>log.csv</a>.</p>"]
    runs = sorted((runs or {}).values(), key=lambda r: r["received"], reverse=True)
    if runs:
        rows = []
        for r in runs:
            who = r.get("who", "")
            who_cell = (f"<a href='/h2h/report?who={quote(who)}'>{e(who)}</a>" if who else "-")
            rows.append([who_cell, e(stack_label(r)),
                         f"<a href='/h2h/report?runs={quote(r['key'])}'>{e(r['id'])}</a>",
                         str(len(r.get("order", []))), e(r["received"])])
        out.append(f"<h2>Runs</h2><p class=note>{len(runs)} runs, newest first. A tester's name shows "
                   "their latest run of each stack; a run's number shows that run alone.</p>")
        out.append(table(["Tester", "Stack", "Run", "Tests", "Received"], rows))
    out.append("</main>")
    return "".join(out)


def local_addresses():
    addrs = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addrs.add(info[4][0])
    except OSError:
        pass
    return sorted(a for a in addrs if not a.startswith("127."))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--data", default="results", help="directory for runs.json and requests.csv")
    ap.add_argument("--report", action="store_true", help="print the text report and exit")
    ap.add_argument("--runs", default="", help="with --report: runs to compare (keys or ids, comma-separated)")
    ap.add_argument("--who", default="", help="with --report: only these testers' runs (comma-separated)")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--server-version", default=os.environ.get("H2H_VERSION", ""),
                    help="the version to show (default: $H2H_VERSION, the stamped one, or the git commit)")
    ap.add_argument("--version", action="store_true", help="print the version and exit")
    ap.add_argument("--stamp", metavar="OUT", help="write a copy of this file with its version fixed, and exit")
    args = ap.parse_args(argv)
    version = server_version(args.server_version)
    if args.version:
        print(version)
        return 0
    if args.stamp:
        stamp(args.stamp, version)
        return 0
    store = Store(args.data)
    if args.report:
        sys.stdout.write(text_report(pick_runs(store.runs, args.runs, args.who), read_log(store.log_path), version))
        return 0
    Handler.store = store
    Handler.version = version
    Handler.server_version = "h2h/" + version
    httpd = ThreadingHTTPServer((args.bind, args.port), Handler)
    httpd.daemon_threads = True
    httpd.verbose = args.verbose
    where = ", ".join(f"http://{a}:{args.port}/h2h" for a in local_addresses()) or f"port {args.port}"
    print(f"h2h server {version} on {where}  (report: /h2h/report)", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
