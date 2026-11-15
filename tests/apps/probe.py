# Exercises CPython under Barrique: interpreter loop, hashlib/zlib/json/sqlite3/ssl/
# ctypes/decimal, threads, subprocess. Prints RESULT: ok when all checks pass.
import sys, platform, time, hashlib, zlib, json, sqlite3, ssl, ctypes, decimal, threading, subprocess, os, tempfile
ok = True
def check(name, cond):
    global ok
    print(f"{name}: {'ok' if cond else 'FAILED'}")
    ok &= bool(cond)
print(sys.version.replace("\n", " "), platform.machine(), ctypes.sizeof(ctypes.c_void_p) * 8, "bit")
t = time.perf_counter(); s = sum((i ^ (i >> 3)) % 7 for i in range(5_000_000))
print(f"loop {s} {(time.perf_counter() - t) * 1000:.0f} ms")
check("loop", s == 14999988)
data = b"barrique " * 10000
check("hashlib", hashlib.sha256(data).hexdigest().startswith("45bbd328f377fc74"))
check("zlib", zlib.decompress(zlib.compress(data)) == data)
check("json", json.loads(json.dumps({"a": [1, 2.5, None]})) == {"a": [1, 2.5, None]})
db = sqlite3.connect(":memory:"); db.execute("create table t(x)"); db.executemany("insert into t values(?)", [(i,) for i in range(1000)])
check("sqlite3", db.execute("select sum(x) from t").fetchone()[0] == 499500)
check("ssl", ssl.create_default_context().protocol is not None and ssl.OPENSSL_VERSION.startswith("OpenSSL"))
check("ctypes", ctypes.windll.kernel32.GetCurrentProcessId() == os.getpid())
decimal.getcontext().prec = 50
check("decimal", str(decimal.Decimal(1) / decimal.Decimal(7)).startswith("0.142857142857"))
res = [0] * 8
def work(k): res[k] = sum(range(k * 100000))
th = [threading.Thread(target=work, args=(k,)) for k in range(8)]
[x.start() for x in th]; [x.join() for x in th]
check("threads", res[7] == sum(range(700000)))
out = subprocess.run([sys.executable, "-c", "print(6*7)"], capture_output=True, text=True).stdout.strip()
check("subprocess", out == "42")
with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, "f.txt"); open(p, "w").write("hello"); check("files", open(p).read() == "hello")
print("RESULT:", "ok" if ok else "FAILED")
