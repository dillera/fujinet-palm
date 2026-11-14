"""Host tests: the server on its own, and palm/suite.c built for this machine
(tests/host) run against it end to end, upload and reports included."""
import json
import os
import shutil
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
        # Every bytes request is in the log with its labels.
        log = h2h_server.read_log(os.path.join(self.data, "requests.csv"))
        mine = [e for e in log if e["run"] == run["id"] and e["path"].startswith("/h2h/bytes/")]
        self.assertEqual(len(mine), 2 * 5 + 2 * 8)
        text = h2h_server.text_report(h2h_server.pick_runs(runs, ""), log)
        self.assertIn("get64k", text)
        page = h2h_server.html_report(h2h_server.pick_runs(runs, run["id"]), log)
        self.assertIn("Read size sweep", page)
        self.assertIn("<svg", page)

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
