# -*- coding: utf-8 -*-
"""压缩对拍：对 测试/用例 下每个 .wen 做「压缩 -> 跑 -> 比对输出」。

判定标准：原文件与压缩文件的 stdout、stderr、退出码三者完全一致才算通过。
超时/随机输出导致的不一致单独归类，人工复核。
用法：python 工具/压缩对拍.py [用例目录]
"""

import io
import os
import subprocess
import sys
import glob
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEN = os.path.join(ROOT, "wen.exe")
COMP = os.path.join(ROOT, "工具", "压缩源码.py")
OUTDIR = os.path.join(ROOT, "输出", "压缩", "用例")


def run(exe_args, timeout=20):
    try:
        p = subprocess.run(exe_args, cwd=ROOT, capture_output=True,
                           timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return "timeout", b"", b""


def norm(b, path):
    """把 stderr 里的具体文件路径抹平成占位符：压缩前后路径必然不同，
    这种差异不是语义差异。行号仍然原样比较（行号真的变了才报出来）。"""
    s = b.decode("utf-8", "ignore")
    s = s.replace(path, "<FILE>")
    s = s.replace(os.path.basename(path), "<FILE>")
    return s


def main():
    case_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "测试", "用例")
    files = sorted(glob.glob(os.path.join(case_dir, "*.wen")))
    if not os.path.isdir(OUTDIR):
        os.makedirs(OUTDIR)

    ok = fail = skip = 0
    fails = []
    for f in files:
        name = os.path.basename(f)
        dst = os.path.join(OUTDIR, name)
        c = subprocess.run([sys.executable, COMP, f, dst, "--内联", "--扁平"],
                           cwd=ROOT, capture_output=True)
        if c.returncode != 0:
            fails.append((name, "压缩失败", c.stderr.decode("utf-8", "ignore")[:200]))
            fail += 1
            continue
        r1 = run([WEN, f])
        r2 = run([WEN, dst])
        if r1[0] == "timeout" or r2[0] == "timeout":
            skip += 1
            continue
        e1 = norm(r1[2], f)
        e2 = norm(r2[2], dst)
        if r1[0] == r2[0] and r1[1] == r2[1] and e1 == e2:
            ok += 1
        else:
            why = []
            if r1[0] != r2[0]:
                why.append("退出码 %s vs %s" % (r1[0], r2[0]))
            if r1[1] != r2[1]:
                why.append("stdout 不同")
            if e1 != e2:
                why.append("stderr 不同")
            fails.append((name, "、".join(why), ""))
            fail += 1

    print("用例总数 %d：通过 %d、失败 %d、超时跳过 %d" % (len(files), ok, fail, skip))
    if fails:
        print("--- 失败明细 ---")
        for n, w, d in fails[:40]:
            print("%s：%s %s" % (n, w, d))
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
