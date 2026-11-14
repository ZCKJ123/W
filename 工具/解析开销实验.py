# -*- coding: utf-8 -*-
"""解析开销归因实验：同样多语句、不同行数，看 --检查 耗时差多少。

一组「一行一条语句」，一组「一行两条语句（分号）」，语句总数相同。
如果行多的一组明显更慢，说明每行存在固定开销，压缩行数就是真收益。
"""

import os
import subprocess
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "输出", "解析实验")
os.makedirs(OUT, exist_ok=True)

N = 3000  # 每种 3000 行


def write(name, text):
    p = os.path.join(OUT, name)
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return p


# 一行一条：6000 语句 / 3000 行
one_per_line = []
for i in range(N):
    one_per_line.append("将 甲 变成 %d" % (i % 7))
a = write("一行一条.wen", "\n".join(one_per_line) + "\n打印 甲\n")

# 一行两条：6000 语句 / 3000 行（同样行数，双倍语句）
two_per_line = []
for i in range(N):
    two_per_line.append("将 甲 变成 %d; 将 乙 变成 %d" % (i % 7, i % 5))
b = write("一行两条.wen", "\n".join(two_per_line) + "\n打印 甲\n")

# 一行三条：9000 语句 / 3000 行
three_per_line = []
for i in range(N):
    three_per_line.append("将 甲 变成 %d; 将 乙 变成 %d; 将 丙 变成 %d" % (i % 7, i % 5, i % 3))
c = write("一行三条.wen", "\n".join(three_per_line) + "\n打印 甲\n")


def check(path, rounds=5):
    best = None
    for _ in range(rounds):
        t0 = time.perf_counter()
        subprocess.run([os.path.join(ROOT, "wen.exe"), "--检查", path],
                       cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
        dt = (time.perf_counter() - t0) * 1000
        if best is None or dt < best:
            best = dt
    return best


for name, p, stmts in (("一行一条", a, N), ("一行两条", b, N * 2), ("一行三条", c, N * 3)):
    size = os.path.getsize(p)
    t = check(p)
    print("%-8s %2d 行 / %5d 语句 / %6d 字节 → %7.1f ms（%.3f ms/语句，%.3f ms/行）"
          % (name, N, stmts, size, t, t / stmts, t / N))
