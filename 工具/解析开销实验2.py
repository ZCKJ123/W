# -*- coding: utf-8 -*-
"""解析开销实验二：语句数固定，只变行数。

用来判断压缩行数到底有没有收益，以及每语句的固定成本是多少。
同时把「解析」和「执行」拆开：--检查 只解析，默认路径还要执行。
"""

import os
import subprocess
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "输出", "解析实验")
os.makedirs(OUT, exist_ok=True)

STMT = 6000


def write(name, text):
    p = os.path.join(OUT, name)
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return p


# 6000 语句 / 6000 行
lines1 = ["将 甲 变成 1"] * STMT
a = write("六千条六行.wen", "\n".join(lines1) + "\n打印 甲\n")

# 6000 语句 / 1000 行（每行 6 条，用分号）
lines2 = []
for i in range(0, STMT, 6):
    lines2.append("; ".join(["将 甲 变成 1"] * 6))
b = write("六千条千行.wen", "\n".join(lines2) + "\n打印 甲\n")

# 6000 语句 / 300 行（每行 20 条）
lines3 = []
for i in range(0, STMT, 20):
    lines3.append("; ".join(["将 甲 变成 1"] * 20))
c = write("六千条三百行.wen", "\n".join(lines3) + "\n打印 甲\n")


def run(args, rounds=5):
    best = None
    for _ in range(rounds):
        t0 = time.perf_counter()
        subprocess.run([os.path.join(ROOT, "wen.exe")] + args,
                       cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=180)
        dt = (time.perf_counter() - t0) * 1000
        if best is None or dt < best:
            best = dt
    return best


print("语句数固定 6000，只改行数：")
for name, p, rows in (("6000行", a, 6000), ("1000行", b, 1000), ("300行", c, 300)):
    t_check = run(["--检查", p])
    t_run = run([p])
    t_ast = run(["--无字节码", p])
    print("%-8s %5d 行 %7d 字节 → 检查 %7.1f ms ｜ 执行(VM) %7.1f ms ｜ 执行(AST) %7.1f ms"
          % (name, rows, os.path.getsize(p), t_check, t_run, t_ast))
