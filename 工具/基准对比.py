# -*- coding: utf-8 -*-
"""基准对比：多个可执行文件跑同一批基准脚本，取多轮最小值。

用法：python 工具/基准对比.py 基准文件1 [基准文件2 ...] -- exe1 exe2 ...
每轮取该 exe 的最快一次，避免首轮缓存冷启动和系统抖动。
"""

import os
import sys
import time
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROUNDS = 7


def bench(exe, f, extra):
    best = None
    for _ in range(ROUNDS):
        t0 = time.perf_counter()
        try:
            subprocess.run([exe] + extra + [f], cwd=ROOT,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
        except subprocess.TimeoutExpired:
            return None
        dt = (time.perf_counter() - t0) * 1000
        if best is None or dt < best:
            best = dt
    return best


def main():
    if "--" not in sys.argv:
        print("用法：python 工具/基准对比.py 基准1 [基准2...] -- exe1 [exe2...]")
        return 1
    i = sys.argv.index("--")
    files = sys.argv[1:i]
    exes = sys.argv[i + 1:]
    if not files or not exes:
        print("基准文件和可执行文件都不能为空")
        return 1
    names = [os.path.basename(e) for e in exes]
    print("%-24s %s" % ("基准", "  ".join("%-14s" % n for n in names)))
    for f in files:
        if not os.path.exists(os.path.join(ROOT, f)):
            print("%-24s 文件不存在" % os.path.basename(f))
            continue
        row = []
        for e in exes:
            t = bench(e, f, [])
            row.append("--" if t is None else "%.1f ms" % t)
        print("%-24s %s" % (os.path.basename(f), "  ".join("%-14s" % v for v in row)))
        row2 = []
        for e in exes:
            t = bench(e, f, ["--不快"])
            row2.append("--" if t is None else "%.1f ms" % t)
        print("%-24s %s" % ("  └ --不快（纯 VM）", "  ".join("%-14s" % v for v in row2)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
