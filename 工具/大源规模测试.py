# -*- coding: utf-8 -*-
"""大源码规模测试：生成 1MB 级别的普通源码，测 --检查（纯解析）耗时。

顺便量一下每个规模档位的词法占比，用来判断「上兆源码 1 秒内」还差多远。
"""

import os
import subprocess
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "输出", "解析实验")
os.makedirs(OUT, exist_ok=True)

# 一行一句，包含中文标识符、数字、字符串、运算符、函数调用，尽量贴近真实源码
one = '将 变量甲 变成 变量乙 + 42 * (3 - 1); 打印 "这一段是中文文本 %d"\n'


def build(rows, name):
    body = []
    for i in range(rows):
        body.append(one.strip() % i)
    text = "将 变量甲 变成 0\n将 变量乙 变成 1\n" + "\n".join(body) + "\n打印 变量甲\n"
    p = os.path.join(OUT, name)
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return p, os.path.getsize(p)


def timed(args, rounds=3):
    best = None
    for _ in range(rounds):
        t0 = time.perf_counter()
        subprocess.run([os.path.join(ROOT, "wen_prof.exe")] + args,
                       cwd=ROOT, stdout=subprocess.DEVNULL,
                       stderr=subprocess.PIPE, timeout=300)
        dt = (time.perf_counter() - t0) * 1000
        if best is None or dt < best:
            best = dt
    return best


def parse_profile(path):
    p = subprocess.run([os.path.join(ROOT, "wen_prof.exe"), "--检查", path],
                       cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                       timeout=300, env={**os.environ, "WEN_PROFILE": "1"})
    out = p.stderr.decode("utf-8", "ignore")
    for line in out.splitlines():
        if "解析计时" in line:
            return line.strip()
    return "（无计时输出）"


for rows, name in ((30000, "一点五MB.wen"), (200000, "十MB.wen")):
    p, size = build(rows, name)
    t = timed(["--检查", p])
    print("%s：%d 行 %d 字节 → --检查 %7.1f ms（%.3f µs/字节）"
          % (name, rows + 3, size, t, t * 1000 / size))
    print("   " + parse_profile(p))
