#!/usr/bin/env python3
"""The head-to-head test's target server, and the place its results meet.

Python 3, no dependencies, so it runs on this Mac or on any web host:

    python3 server/h2h_server.py --port 8080 --data results
    python3 server/h2h_server.py --data results --report      # text report, no server

Under /h2h:
  /bytes/N      N bytes (up to 4 MB), byte i = (i ^ (i >> 8)) & 0xFF, so the
                Palm can check every byte. Content-Length, then the connection
                closes. The apps add ?r=<run>&t=<test>&k=<rep>&s=<stack>, which
                only label the request log.
  /h?...        a run's header, sent by the app's Send button
  /r?...        one row of a run's results (o = index of the first value in v)
  /n?...        one diagnostic note of a run (i = its number, m = the text)
  /report       every stack's latest run side by side (HTML); ?runs=a,b picks runs
  /report.txt   the same as text
  /runs.json    everything stored
  /log.csv      every request served: when, who, what, and how long it took here

Runs are kept in <data>/runs.json, the request log in <data>/requests.csv.
"""
import argparse
import csv
import datetime
import html
import json
import os
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

MAX_BYTES = 4 * 1024 * 1024

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

    def _run(self, stack, run_id, client):
        key = f"{stack}:{run_id}"
        run = self.runs.get(key)
        if run is None:
            run = self.runs[key] = {
                "key": key, "stack": stack, "id": run_id, "client": client,
                "received": now_iso(), "meta": {}, "rows": {}, "order": [],
            }
        return run

    def header(self, q, client):
        with self.lock:
            run = self._run(q["s"], q["id"], client)
            for f in HEADER_FIELDS:
                if f in q:
                    run["meta"][f] = q[f]
            run["received"] = now_iso()
            self._save()

    def row(self, q, client):
        values = [int(v) for v in q.get("v", "").split(",") if v != ""]
        offset = int(q.get("o", "0"))
        with self.lock:
            run = self._run(q["s"], q["id"], client)
            row = run["rows"].setdefault(q["t"], {})
            if q["t"] not in run["order"]:
                run["order"].append(q["t"])
            for i, v in enumerate(values):
                if 0 <= offset + i < len(FIELDS):
                    row[FIELDS[offset + i]] = v
            self._save()

    def note(self, q, client):
        with self.lock:
            run = self._run(q["s"], q["id"], client)
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


def query(url):
    return {k: v[-1] for k, v in parse_qs(urlsplit(url).query, keep_blank_values=True).items()}


class Handler(BaseHTTPRequestHandler):
    server_version = "h2h/1"
    protocol_version = "HTTP/1.1"
    store = None

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
            if path == "/runs.json":
                return self._send(200, json.dumps(self.store.runs, indent=1).encode(), "application/json")
            if path == "/log.csv":
                data = open(self.store.log_path, "rb").read() if os.path.exists(self.store.log_path) else b""
                return self._send(200, data, "text/csv; charset=utf-8")
            if path in ("/report", "/report.txt"):
                runs = pick_runs(self.store.runs, q.get("runs", ""))
                log = read_log(self.store.log_path)
                if path == "/report":
                    return self._send(200, html_report(runs, log).encode(), "text/html; charset=utf-8")
                return self._send(200, text_report(runs, log).encode())
            if path == "":
                return self._send(200, index_page().encode(), "text/html; charset=utf-8")
        except (KeyError, ValueError) as e:
            return self._send(400, f"bad request: {e}\n".encode())
        return self._send(404, b"not found\n")


# ---------------------------------------------------------------- reporting

def pick_runs(runs, wanted):
    """The runs named in wanted (keys or ids), else the latest run of each stack."""
    if wanted:
        names = [w for w in wanted.split(",") if w]
        return [r for r in runs.values() if r["key"] in names or r["id"] in names]
    latest = {}
    for r in runs.values():
        if r["stack"] not in latest or r["received"] > latest[r["stack"]]["received"]:
            latest[r["stack"]] = r
    order = list(STACKS)
    return sorted(latest.values(), key=lambda r: order.index(r["stack"]) if r["stack"] in order else 99)


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


def label(run):
    return STACKS.get(run["stack"], run["stack"])


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


def text_report(runs, log):
    if not runs:
        return "No runs yet: run a test on a Palm, then tap Send.\n"
    w = 18
    lines = []
    head = "".ljust(w) + "".join(label(r)[:24].ljust(26) for r in runs)
    lines += ["Head-to-head: " + ", ".join(f"{r['stack']} run {r['id']}" for r in runs), ""]
    lines.append("RUNS")
    for name, get in (
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
    return "\n".join(lines) + "\n"


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
"""

PART_COLORS = {"link send": "var(--s1)", "device wait": "var(--s2)", "link recv": "var(--s3)", "palm": "var(--s4)"}


def html_report(runs, log):
    e = html.escape
    out = [f"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
           f"<title>H2H report</title><style>{CSS}</style></head><body><main>",
           "<h1>FujiNet on the Palm: head to head</h1>",
           f"<p class=note>{len(runs)} runs, the latest of each stack unless ?runs= names them. "
           "Times are the Palm's own, averaged over the repetitions that succeeded.</p>"]
    if not runs:
        out.append("<p>No runs yet: run a test on a Palm, then tap Send.</p></main></body></html>")
        return "".join(out)

    def table(head, rows):
        s = ["<div class=wrap><table><thead><tr>"] + [f"<th>{e(h)}</th>" for h in head] + ["</tr></thead><tbody>"]
        for r in rows:
            s.append("<tr>" + "".join(f"<td>{c}</td>" for c in r) + "</tr>")
        s.append("</tbody></table></div>")
        return "".join(s)

    head = [""] + [label(r) for r in runs]
    out.append("<h2>The runs</h2>")
    meta_rows = []
    for name, get in (
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
        out.append(sweep_svg(runs, sizes))

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


def sweep_svg(runs, sizes):
    w, h, pad = 720, 260, 44
    colors = ["var(--s1)", "var(--s2)", "var(--s3)", "var(--s4)"]
    series = []
    top = 1.0
    for r in runs:
        pts = [(n, kbs(r["rows"][f"sw{n}"])) for n in sizes if good(r["rows"].get(f"sw{n}"))]
        series.append((r, pts))
        top = max([top] + [v for _, v in pts])
    import math
    lo, hi = math.log2(min(sizes)), math.log2(max(sizes)) if len(sizes) > 1 else math.log2(min(sizes)) + 1

    def x(n):
        return pad + (math.log2(n) - lo) / max(hi - lo, 1e-9) * (w - 2 * pad)

    def y(v):
        return h - pad + 10 - v / top * (h - 2 * pad)

    s = [f"<div class=wrap><svg viewBox='0 0 {w} {h}' width='100%' style='max-width:{w}px' role=img "
         "aria-label='Body rate by read size'>"]
    s.append(f"<line x1={pad} y1={y(0):.1f} x2={w - pad} y2={y(0):.1f} stroke='var(--line)'/>")
    for n in sizes:
        s.append(f"<text x={x(n):.1f} y={h - 12} text-anchor=middle font-size=11 fill='var(--muted)'>{n}</text>")
    for i in range(5):
        v = top * i / 4
        s.append(f"<text x={pad - 6} y={y(v) + 4:.1f} text-anchor=end font-size=11 fill='var(--muted)'>{v:.0f}</text>")
    for i, (r, pts) in enumerate(series):
        c = colors[i % len(colors)]
        if pts:
            s.append(f"<polyline fill=none stroke='{c}' stroke-width=2 points='"
                     + " ".join(f"{x(n):.1f},{y(v):.1f}" for n, v in pts) + "'/>")
            for n, v in pts:
                s.append(f"<circle cx={x(n):.1f} cy={y(v):.1f} r=3 fill='{c}'><title>{n}: {v:.1f} KB/s</title></circle>")
        s.append(f"<text x={pad + 8 + i * 230} y=16 font-size=12 fill='{c}'>&#9632; {html.escape(label(r))}</text>")
    s.append(f"<text x={pad - 30} y={pad - 14} font-size=11 fill='var(--muted)'>KB/s</text></svg></div>")
    return "".join(s)


def index_page():
    return ("<!doctype html><meta charset=utf-8><title>h2h</title><style>" + CSS + "</style><main>"
            "<h1>h2h server</h1><p>Targets: <code>/h2h/bytes/N</code>. Results: "
            "<a href='/h2h/report'>report</a>, <a href='/h2h/report.txt'>report.txt</a>, "
            "<a href='/h2h/runs.json'>runs.json</a>, <a href='/h2h/log.csv'>log.csv</a>.</p></main>")


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
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)
    store = Store(args.data)
    if args.report:
        sys.stdout.write(text_report(pick_runs(store.runs, args.runs), read_log(store.log_path)))
        return 0
    Handler.store = store
    httpd = ThreadingHTTPServer((args.bind, args.port), Handler)
    httpd.daemon_threads = True
    httpd.verbose = args.verbose
    where = ", ".join(f"http://{a}:{args.port}/h2h" for a in local_addresses()) or f"port {args.port}"
    print(f"h2h server on {where}  (report: /h2h/report)", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
