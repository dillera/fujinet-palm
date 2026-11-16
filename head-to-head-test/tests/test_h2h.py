"""Host tests: the server on its own, and palm/suite.c built for this machine
(tests/host) run against it end to end, upload and reports included."""
import csv
import http.client
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "server"))
import h2h_server  # noqa: E402


def build_host(out_dir):
    exe = os.path.join(out_dir, "host-h2h")
    subprocess.run(
        ["cc", "-O1", "-Wall", "-Werror", "-Wno-format-security",
         "-I", os.path.join(ROOT, "tests", "host"), "-I", os.path.join(ROOT, "palm"),
         "-o", exe, os.path.join(ROOT, "tests", "host", "host.c"), os.path.join(ROOT, "palm", "suite.c")],
        check=True)
    return exe


class Server:
    def __init__(self, data):
        h2h_server.Handler.store = h2h_server.Store(data)
        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), h2h_server.Handler)
        self.httpd.daemon_threads = True
        self.httpd.verbose = False
        self.port = self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def stop(self):
        self.httpd.shutdown()
        self.httpd.server_close()

    def request(self, method, path, body=None, headers=None, chunked=False):
        """One request with http.client, which follows no redirect: status, headers (lower-case names), body."""
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        headers = dict(headers or {})
        if chunked:
            headers["Transfer-Encoding"] = "chunked"
            body = iter([body[i:i + 1000] for i in range(0, len(body), 1000)])
        c.request(method, path, body=body, headers=headers, encode_chunked=chunked)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, {k.lower(): v for k, v in r.getheaders()}, data

    def raw(self, data):
        """Bytes straight onto a connection, and everything that comes back."""
        with socket.create_connection(("127.0.0.1", self.port), timeout=10) as s:
            s.sendall(data)
            out = b""
            while True:
                piece = s.recv(65536)
                if not piece:
                    return out
                out += piece

    def log(self):
        return h2h_server.read_log(h2h_server.Handler.store.log_path)


class PatternAndStore(unittest.TestCase):
    def test_pattern(self):
        body = h2h_server.pattern(70000)
        self.assertEqual(len(body), 70000)
        for i in (0, 1, 255, 256, 257, 65535, 65536, 69999):
            self.assertEqual(body[i], (i ^ (i >> 8)) & 0xFF)

    def test_rows_merge_by_offset(self):
        with tempfile.TemporaryDirectory() as d:
            store = h2h_server.Store(d)
            store.header({"s": "nio-serial", "id": "7", "fw": "1.0 x", "baud": "115200"}, "1.2.3.4")
            store.row({"s": "nio-serial", "id": "7", "t": "get1k", "o": "0", "v": "3,0,490,1024"}, "1.2.3.4")
            store.row({"s": "nio-serial", "id": "7", "t": "get1k", "o": "4", "v": "500000,400000"}, "1.2.3.4")
            run = h2h_server.Store(d).runs["nio-serial:7"]
            self.assertEqual(run["meta"]["fw"], "1.0 x")
            self.assertEqual(run["rows"]["get1k"]["chunk"], 490)
            self.assertEqual(run["rows"]["get1k"]["us_total"], 500000)
            self.assertEqual(run["rows"]["get1k"]["us_min"], 400000)

    def test_unsplit_link_is_said_so(self):
        # A NioLib run counts no exchanges: the reports must not put its whole
        # time down to the Palm.
        with tempfile.TemporaryDirectory() as d:
            store = h2h_server.Store(d)
            store.header({"s": "niolib-springboard", "id": "9", "n": "1"}, "1.2.3.4")
            values = [3, 0, 1002, 16384, 600000, 590000, 610000, 5000, 6000, 12000, 580000, 4000,
                      90000, 0, 0, 17, 0, 0, 0, 0, 0, 0, 0]
            store.row({"s": "niolib-springboard", "id": "9", "t": "get16k", "o": "0",
                       "v": ",".join(map(str, values))}, "1.2.3.4")
            runs = h2h_server.Store(d).runs
            text = h2h_server.text_report(list(runs.values()), [])
            self.assertIn("not split (library)", text)
            self.assertNotIn("palm              5", text)
            page = h2h_server.html_report(list(runs.values()), [])
            self.assertIn("not split: the exchanges run inside the library", page)


    def test_pattern_pieces_match_the_whole(self):
        # The sink and echo check bodies piece by piece with pattern_at, across 64 KB boundaries too.
        whole = bytes((i ^ (i >> 8)) & 0xFF for i in range(140000))
        self.assertEqual(h2h_server.pattern(140000), whole)
        for start, n in ((0, 1), (255, 2), (65530, 100), (65536, 65536), (131000, 9000)):
            self.assertEqual(h2h_server.pattern_at(start, n), whole[start:start + n], (start, n))

    def test_old_request_log_gets_the_new_columns(self):
        # An older server's log keeps its rows under the new header; one with columns this server does not know
        # is moved aside rather than mixed.
        with tempfile.TemporaryDirectory() as d:
            old = ["time", "client", "path", "stack", "run", "test", "rep", "bytes", "ms_request", "ms_send",
                   "status"]
            with open(os.path.join(d, "requests.csv"), "w", newline="") as f:
                w = csv.writer(f)
                w.writerow(old)
                w.writerow(["t0", "1.2.3.4", "/h2h/bytes/16", "nio-serial", "7", "get16", "0", "16", "0.1",
                            "0.0", "200"])
            store = h2h_server.Store(d)
            store.log({"path": "/h2h/sink/16", "method": "POST", "bytes_in": 16, "encoding": "chunked",
                       "result": "ok", "status": 200})
            rows = h2h_server.read_log(store.log_path)
            self.assertEqual(list(rows[0]), h2h_server.LOG_COLUMNS)
            self.assertEqual((rows[0]["test"], rows[0]["status"], rows[0]["encoding"]), ("get16", "200", ""))
            self.assertEqual((rows[1]["method"], rows[1]["encoding"], rows[1]["result"]), ("POST", "chunked", "ok"))
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "requests.csv"), "w") as f:
                f.write("time,elsewhere\nt0,x\n")
            h2h_server.Store(d)
            self.assertFalse(os.path.exists(os.path.join(d, "requests.csv")))
            (moved,) = [n for n in os.listdir(d) if n.startswith("requests-")]
            with open(os.path.join(d, moved)) as f:
                self.assertEqual(f.read(), "time,elsewhere\nt0,x\n")

    def test_failures_are_named(self):
        self.assertEqual(h2h_server.failure({"last_err": 0x8046}), "redirect not followed")
        self.assertEqual(h2h_server.failure({"last_err": 0x8102}), "NIO invalid")
        self.assertEqual(h2h_server.failure({"last_err": 0x8148}), "NIO error 72")
        self.assertEqual(h2h_server.failure({"last_err": 0x1234}), "error 0x1234")


class Uploads(unittest.TestCase):
    """POST and PUT to /sink/N and /echo, with every body framing NIO uses."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.server = Server(self.tmp)

    def tearDown(self):
        self.server.stop()
        shutil.rmtree(self.tmp)

    def test_sink_checks_every_byte(self):
        body = h2h_server.pattern(16384)
        for method, chunked in (("POST", False), ("POST", True), ("PUT", False), ("PUT", True)):
            status, _, reply = self.server.request(method, "/h2h/sink/16384?r=5&t=post16k&k=0&s=host", body,
                                                   chunked=chunked)
            self.assertEqual((status, reply), (200, b"ok 16384\n"), (method, chunked))
        bad = bytearray(body[:1024])
        bad[700] ^= 0xFF
        for sent, want in ((body[:1000], b"short: 1000 of 1024\n"), (body[:1100], b"long: 1100 of 1024\n"),
                           (bytes(bad), b"bad at 700: got 0x%02x want 0x%02x\n" % (bad[700], body[700])),
                           (b"", b"short: 0 of 1024\n")):
            status, _, reply = self.server.request("POST", "/h2h/sink/1024", sent)
            self.assertEqual((status, reply), (422, want))
        self.assertEqual(self.server.request("POST", "/h2h/sink/99999999", b"x")[0], 400)
        self.assertEqual(self.server.request("GET", "/h2h/sink/16")[0], 405)
        log = self.server.log()
        firsts = [(e["method"], e["encoding"], e["bytes_in"], e["result"]) for e in log[:4]]
        self.assertEqual(firsts, [("POST", "length", "16384", "ok"), ("POST", "chunked", "16384", "ok"),
                                  ("PUT", "length", "16384", "ok"), ("PUT", "chunked", "16384", "ok")])
        self.assertEqual((log[0]["stack"], log[0]["run"], log[0]["test"], log[0]["rep"]), ("host", "5", "post16k", "0"))

    def test_echo_sends_the_body_back(self):
        body = h2h_server.pattern(4096)
        status, headers, reply = self.server.request("POST", "/h2h/echo", body, chunked=True)
        self.assertEqual((status, reply, headers["x-h2h-check"]), (200, body, "ok"))
        self.assertEqual(headers["content-type"], "application/octet-stream")
        self.assertEqual(headers["x-content-type-options"], "nosniff")
        status, headers, reply = self.server.request("PUT", "/h2h/echo", b"\x00\x02")
        self.assertEqual((status, reply, headers["x-h2h-check"]), (200, b"\x00\x02", "bad at 1: got 0x02 want 0x01"))
        status, _, _ = self.server.request("POST", "/h2h/echo", None,
                                           headers={"Content-Length": str(h2h_server.ECHO_MAX + 1)})
        self.assertEqual(status, 413)

    def test_broken_bodies_are_refused(self):
        head = b"POST /h2h/sink/4 HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n"
        for body in (b"zz\r\n\x00\x01\r\n0\r\n\r\n", b"4\r\n\x00\x01\x02\x03XX0\r\n\r\n"):
            reply = self.server.raw(head + body)
            self.assertTrue(reply.startswith(b"HTTP/1.1 400 "), reply)
        reply = self.server.raw(b"POST /h2h/sink/4 HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: gzip\r\n\r\n")
        self.assertTrue(reply.startswith(b"HTTP/1.1 501 "), reply)
        # A trailer field after the last chunk is read and ignored.
        reply = self.server.raw(head + b"4\r\n\x00\x01\x02\x03\r\n0\r\nX-T: 1\r\n\r\n")
        self.assertTrue(reply.startswith(b"HTTP/1.1 200 ") and reply.endswith(b"ok 4\n"), reply)


class Redirects(unittest.TestCase):
    """/redirect/HOPS/WHERE, one hop at a time as a client that follows them sees it."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.server = Server(self.tmp)

    def tearDown(self):
        self.server.stop()
        shutil.rmtree(self.tmp)

    def test_a_chain_ends_at_the_target(self):
        path, seen = "/h2h/redirect/301a-307-308ae/bytes/1024?r=3&t=redir3&k=1&s=host", []
        for _ in range(5):
            status, headers, body = self.server.request("GET", path)
            if status == 200:
                break
            seen.append((status, headers["location"], len(body) > 0))
            path = headers["location"].replace(f"http://127.0.0.1:{self.server.port}", "")
        self.assertEqual(body, h2h_server.pattern(1024))
        base = f"http://127.0.0.1:{self.server.port}"
        q = "?r=3&t=redir3&k=1&s=host"
        self.assertEqual(seen, [(301, f"{base}/h2h/redirect/307-308ae/bytes/1024{q}", True),
                                (307, f"/h2h/redirect/308ae/bytes/1024{q}", True),
                                (308, f"{base}/h2h/bytes/1024{q}", False)])
        log = [e for e in self.server.log() if e["test"] == "redir3"]
        self.assertEqual([e["status"] for e in log], ["301", "307", "308", "200"])
        self.assertTrue(all(e["rep"] == "1" for e in log))
        self.assertEqual(log[0]["result"], f"-> {base}/h2h/redirect/307-308ae/bytes/1024{q}")

    def test_a_page_says_where_it_went(self):
        status, headers, body = self.server.request("GET", "/h2h/redirect/302/echo")
        self.assertEqual((status, headers["location"]), (302, "/h2h/echo"))
        self.assertIn(b"<h1>302 Found</h1>", body)
        self.assertIn(b'<a href="/h2h/echo">', body)
        self.assertEqual(headers["content-type"], "text/html; charset=utf-8")

    def test_a_post_is_read_then_sent_again(self):
        body = h2h_server.pattern(1024)
        status, headers, _ = self.server.request("POST", "/h2h/redirect/307/sink/1024?t=post307", body)
        self.assertEqual((status, headers["location"]), (307, "/h2h/sink/1024?t=post307"))
        status, _, reply = self.server.request("POST", headers["location"], body)
        self.assertEqual((status, reply), (200, b"ok 1024\n"))
        hop = self.server.log()[0]
        self.assertEqual((hop["method"], hop["bytes_in"], hop["encoding"]), ("POST", "1024", "length"))

    def test_bad_chains_are_refused(self):
        for path in ("/h2h/redirect/304/bytes/1", "/h2h/redirect/302x/bytes/1", "/h2h/redirect//bytes/1",
                     "/h2h/redirect/302/report", "/h2h/redirect/" + "-".join(["302"] * 11) + "/bytes/1",
                     "/h2h/redirect/302/bytes/x"):
            self.assertEqual(self.server.request("GET", path)[0], 400, path)

    def test_location_without_a_usable_host(self):
        # No port in Host: this server's is added. No usable Host: the address the client reached.
        reply = self.server.raw(b"GET /h2h/redirect/302a/bytes/1 HTTP/1.1\r\nHost: example.org\r\n\r\n")
        self.assertIn(f"Location: http://example.org:{self.server.port}/h2h/bytes/1\r\n".encode(), reply)
        reply = self.server.raw(b"GET /h2h/redirect/302a/bytes/1 HTTP/1.1\r\nHost: a b\r\n\r\n")
        self.assertIn(f"Location: http://127.0.0.1:{self.server.port}/h2h/bytes/1\r\n".encode(), reply)


class Testers(unittest.TestCase):
    def test_runs_are_kept_per_tester(self):
        # Two testers whose Palms drew the same run id on the same stack keep
        # two runs; a run with no name keeps the old key.
        with tempfile.TemporaryDirectory() as d:
            store = h2h_server.Store(d)
            for who, ms in (("Alice", 100000), ("bob, jr", 200000), ("", 300000)):
                q = {"s": "nio-serial", "id": "7", "w": who}
                store.header(dict(q, fw="1.0"), "1.2.3.4")
                store.row(dict(q, t="get1k", o="0", v=f"3,0,490,1024,{ms}"), "1.2.3.4")
            runs = h2h_server.Store(d).runs
            self.assertEqual(sorted(runs), ["Alice/nio-serial:7", "bob jr/nio-serial:7", "nio-serial:7"])
            self.assertEqual(runs["Alice/nio-serial:7"]["rows"]["get1k"]["us_total"], 100000)
            self.assertEqual(runs["bob jr/nio-serial:7"]["who"], "bob jr")
            self.assertEqual(len(h2h_server.pick_runs(runs, "")), 3)
            (alice,) = h2h_server.pick_runs(runs, "", "alice")
            self.assertEqual(alice["who"], "Alice")
            self.assertEqual(len(h2h_server.pick_runs(runs, "", "Alice,BOB JR")), 2)
            self.assertEqual(h2h_server.label(alice), "NIO, cradle serial \u00b7 Alice")
            text = h2h_server.text_report(h2h_server.pick_runs(runs, ""), [])
            self.assertIn("tester", text)
            self.assertIn("bob jr", text)
            self.assertIn("NIO, cradle serial \u00b7 Alice", h2h_server.html_report([alice], []))
            page = h2h_server.index_page("v", runs)
            self.assertIn("/h2h/report?who=bob%20jr", page)
            self.assertIn("/h2h/report?runs=Alice/nio-serial%3A7", page)

    def test_sweep_chart_escapes_tester_names(self):
        # Names come from anyone who sends a run: the chart's legend is HTML and its data sits in a script element.
        run = {"key": "x", "id": "9", "stack": "nio-serial", "who": "</script><b>x</b>", "meta": {"def": "490"},
               "order": ["sw64", "sw490"], "rows": {"sw64": {"reps": 1, "fails": 0, "bytes": 16384, "us_body": 8000000},
                                                   "sw490": {"reps": 1, "fails": 1, "bytes": 16384, "us_body": 0}}}
        page = h2h_server.sweep_chart([run], [64, 490])
        self.assertNotIn("<b>x</b>", page)
        self.assertNotIn("</script><b>", page)
        self.assertIn("&lt;b&gt;x&lt;/b&gt;", page)
        data = json.loads(page.split("id=sweep-data>")[1].split("</script>")[0].replace("<\\/", "</"))
        self.assertEqual(data["runs"][0]["points"], [[64, 2.0]])
        self.assertEqual(data["runs"][0]["fails"], [490])
        self.assertEqual(data["runs"][0]["def"], 490)

    def test_tester_names_are_cleaned(self):
        self.assertEqual(h2h_server.tester({"w": "  Ann \t\n Lee  "}), "Ann Lee")
        self.assertEqual(h2h_server.tester({"w": "a,b"}), "a b")
        self.assertEqual(h2h_server.tester({"w": "x" * 60}), "x" * 40)
        self.assertEqual(h2h_server.tester({}), "")


class Version(unittest.TestCase):
    def test_version_is_shown_everywhere(self):
        # The header, /h2h/version, the index page and both reports all name
        # the server's version: here the one main() was given.
        from urllib.request import urlopen
        saved = h2h_server.Handler.version, h2h_server.Handler.server_version
        h2h_server.Handler.version, h2h_server.Handler.server_version = "v-test", "h2h/v-test"
        with tempfile.TemporaryDirectory() as d:
            server = Server(d)
            try:
                base = f"http://127.0.0.1:{server.port}/h2h"
                with urlopen(base + "/version") as r:
                    self.assertEqual(r.read(), b"v-test\n")
                    self.assertEqual(r.headers["Server"], "h2h/v-test")
                for path in ("/", "/report", "/report.txt"):
                    with urlopen(base + path) as r:
                        self.assertIn(b"v-test", r.read(), path)
            finally:
                server.stop()
                h2h_server.Handler.version, h2h_server.Handler.server_version = saved

    def test_default_is_the_commit_and_a_stamp_fixes_it(self):
        # In this checkout the version is the commit of the server file (or
        # "unknown" outside git); a stamped copy keeps its own wherever it goes.
        v = h2h_server.server_version()
        self.assertTrue(v == "unknown" or v.split("-")[0].isalnum(), v)
        self.assertEqual(h2h_server.server_version("given"), "given")
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "h2h_server.py")
            h2h_server.stamp(out, "abc1234")
            p = subprocess.run([sys.executable, out, "--version"], capture_output=True, text=True, check=True)
            self.assertEqual(p.stdout.strip(), "abc1234")


class EndToEnd(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.exe = build_host(cls.tmp)
        cls.data = os.path.join(cls.tmp, "results")
        cls.server = Server(cls.data)
        cls.target = f"http://127.0.0.1:{cls.server.port}/h2h"

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()
        shutil.rmtree(cls.tmp)

    def test_suite_runs_uploads_and_reports(self):
        p = subprocess.run([self.exe, self.target, "2"], capture_output=True, text=True, timeout=120)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("-- HTTP GET, read 1024 --", p.stdout)
        self.assertIn("-- Read size, 16K body --", p.stdout)
        self.assertIn("Tests with failures: 0", p.stdout)
        with open(os.path.join(self.data, "runs.json")) as f:
            runs = json.load(f)
        run = next(r for r in runs.values() if r["stack"] == "host")
        # The host harness's tester name (with a space, URL-encoded on the way).
        self.assertEqual(run["who"], "Host Tester")
        self.assertEqual(run["key"], f"Host Tester/host:{run['id']}")
        sizes = {"get16": 16, "get1k": 1024, "get4k": 4096, "get16k": 16384, "get64k": 65536}
        for test, size in sizes.items():
            row = run["rows"][test]
            self.assertEqual(row["bytes"], size, test)
            self.assertEqual(row["fails"], 0, test)
            self.assertEqual(row["reps"], 2, test)
            self.assertEqual(len(row), len(h2h_server.FIELDS), test)
        sweep = sorted(int(t[2:]) for t in run["rows"] if t.startswith("sw"))
        self.assertEqual(sweep, [64, 128, 256, 512, 1024, 2048, 4096, 8192])
        self.assertEqual(run["meta"]["fw"], "host")
        self.assertEqual(run["meta"]["def"], "1024")
        # The redirect and upload tests: every one passed, uploads counted by what they sent.
        extras = {"redir1": 1024, "redir3": 1024, "keep302": None, "post1k": 1024, "put1k": 1024,
                  "post16k": 16384, "post64k": 65536, "echo4k": 4096, "post307": 1024}
        for test, size in extras.items():
            row = run["rows"][test]
            self.assertEqual((row["reps"], row["fails"]), (2, 0), test)
            if size:
                self.assertEqual(row["bytes"], size, test)
            self.assertEqual(len(row), len(h2h_server.FIELDS), test)
        self.assertEqual(run["rows"]["post64k"]["writes"], 65)     # 64 writes of 1024, then the commit
        self.assertEqual(run["rows"]["get1k"]["writes"], 0)
        self.assertIn("-- Redirects and uploads --", p.stdout)
        self.assertIn("keep302", p.stdout)
        # Every bytes request is in the log with its labels.
        log = h2h_server.read_log(os.path.join(self.data, "requests.csv"))
        mine = [e for e in log if e["run"] == run["id"] and e["path"].startswith("/h2h/bytes/")
                and e["test"][:2] in ("ge", "sw")]
        self.assertEqual(len(mine), 2 * 5 + 2 * 8)
        # The host link sends bodies chunked unless told otherwise; the 307 has it sent again with its length.
        sinks = {(e["test"], e["encoding"]) for e in log if e["run"] == run["id"] and e["path"].startswith("/h2h/sink/")}
        self.assertEqual(sinks, {("post1k", "chunked"), ("put1k", "chunked"), ("post16k", "chunked"),
                                 ("post64k", "chunked"), ("post307", "length")})
        text = h2h_server.text_report(h2h_server.pick_runs(runs, ""), log)
        self.assertIn("get64k", text)
        self.assertIn("3 hops + target", text)
        self.assertIn("1 hop, not followed", text)
        self.assertIn("chunked", text)
        page = h2h_server.html_report(h2h_server.pick_runs(runs, run["id"]), log)
        self.assertIn("Read size sweep", page)
        self.assertIn("<svg", page)
        self.assertIn("id=sweep-data", page)
        self.assertIn("class=bigger", page)
        self.assertIn("<h2>Redirects and uploads</h2>", page)
        self.assertIn("KB/s up", page)

    def test_bodies_with_a_length(self):
        # As NIO's curl build sends them: the whole body at the commit, with its length.
        p = subprocess.run([self.exe, self.target, "1", "--no-upload"], capture_output=True, text=True, timeout=120,
                           env=dict(os.environ, H2H_HOST_BODY="length"))
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("Tests with failures: 0", p.stdout)
        log = h2h_server.read_log(os.path.join(self.data, "requests.csv"))
        last = [e for e in log if e["path"].startswith("/h2h/sink/") or e["path"] == "/h2h/echo"][-6:]
        self.assertEqual({e["encoding"] for e in last}, {"length"})
        self.assertEqual({e["result"] for e in last}, {"ok"})

    def test_an_older_server_has_none_of_them(self):
        # A server from before the redirect and upload tests answers 404: each says so instead of failing oddly.
        saved = h2h_server.Handler._redirect, h2h_server.Handler._sink, h2h_server.Handler._echo
        h2h_server.Handler._redirect = lambda self, rest: self._send(404, b"not found\n", info=self._drain())
        h2h_server.Handler._sink = lambda self, size: self._send(404, b"not found\n", info=self._drain())
        h2h_server.Handler._echo = lambda self: self._send(404, b"not found\n", info=self._drain())
        try:
            p = subprocess.run([self.exe, self.target, "1", "--no-upload"], capture_output=True, text=True,
                               timeout=120)
        finally:
            h2h_server.Handler._redirect, h2h_server.Handler._sink, h2h_server.Handler._echo = saved
        for test in ("redir1", "keep302", "post1k", "echo4k", "post307"):
            self.assertIn(f"{test} failed: not on server", p.stdout)
        self.assertIn("Tests with failures: 9", p.stdout)     # all of them: the host run has 64K on

    def test_a_redirect_not_followed_is_named(self):
        # A link that hands back a redirect it was asked to follow, as NioLib's Network transport does.
        p = subprocess.run([self.exe, self.target, "1", "--no-upload"], capture_output=True, text=True,
                           timeout=120, env=dict(os.environ, H2H_HOST_NOFOLLOW="1"))
        self.assertIn("redir1 failed: not followed", p.stdout)
        self.assertIn("redir3 failed: not followed", p.stdout)
        self.assertIn("post307 failed: not followed", p.stdout)
        self.assertIn("Tests with failures: 3", p.stdout)

    def test_flaky_link_notes_and_retries(self):
        # Every 7th ping and open fails: the run notes each, and Send's
        # retries still get every row and the notes to the server.
        data = os.path.join(self.tmp, "flaky")
        server = Server(data)
        try:
            env = dict(os.environ, H2H_FLAKY="7")
            p = subprocess.run([self.exe, f"http://127.0.0.1:{server.port}/h2h", "1"], capture_output=True,
                               text=True, timeout=120, env=env)
        finally:
            server.stop()
            h2h_server.Handler.store = h2h_server.Store(self.data)   # back to the shared server's
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("-- Link notes (", p.stdout)
        self.assertIn("err 5", p.stdout)
        with open(os.path.join(data, "runs.json")) as f:
            runs = json.load(f)
        (run,) = runs.values()
        self.assertEqual(len(run["order"]), int(run["meta"]["n"]))
        self.assertGreater(run["notes_total"], 3)
        kept, total = h2h_server.notes(run)
        self.assertEqual(len(kept), min(total, 12))
        self.assertTrue(all("err 5" in n for n in kept))
        text = h2h_server.text_report(h2h_server.pick_runs(runs, run["id"]), [])
        self.assertIn("LINK NOTES", text)
        self.assertIn("Link notes", h2h_server.html_report(h2h_server.pick_runs(runs, run["id"]), []))

    def test_any_page(self):
        target = f"http://127.0.0.1:{self.server.port}/h2h/bytes/3000"
        p = subprocess.run([self.exe, target, "2", "--no-upload"], capture_output=True, text=True, timeout=60)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("page", p.stdout)
        self.assertNotIn("Read size", p.stdout)
        self.assertIn("Tests with failures: 0", p.stdout)

    def test_bad_body_is_caught(self):
        # A server that sends the wrong bytes: the check must fail the test.
        real = h2h_server.pattern
        h2h_server._pattern_cache.clear()
        h2h_server.pattern = lambda n: bytes(n)
        try:
            p = subprocess.run([self.exe, self.target, "1", "--no-upload"], capture_output=True, text=True,
                               timeout=60)
        finally:
            h2h_server.pattern = real
        self.assertIn("bad data", p.stderr)
        self.assertNotIn("Tests with failures: 0", p.stdout)


if __name__ == "__main__":
    unittest.main()
