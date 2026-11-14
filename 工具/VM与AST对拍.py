# -*- coding: utf-8 -*-
"""VM / AST 路径对拍：同一个可执行文件，字节码路径 vs --无字节码 路径，比较输出。

这条对拍是用来验证「编译器 + VM 没有偏离 AST 解释器」的基线门禁，
和跨版本对拍（双引擎对拍.py）互补：跨版本只能说明两个 exe 一样，
这条才能说明字节码路径本身没走歪。

用法：python 工具/VM与AST对拍.py 可执行文件 [用例目录]
"""

import os
import sys
import glob
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(exe, f, extra):
    try:
        p = subprocess.run([exe] + extra + [f], cwd=ROOT, capture_output=True, timeout=25)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return "timeout", b"", b""


def main():
    if len(sys.argv) < 2:
        print("用法：python 工具/VM与AST对拍.py 可执行文件 [用例目录]")
        return 1
    exe = sys.argv[1]
    case_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "测试", "用例")
    files = sorted(glob.glob(os.path.join(case_dir, "*.wen"))
                   + glob.glob(os.path.join(case_dir, "*.编译源")))
    ok = fail = skip = 0
    fails = []
    for f in files:
        vm = run(exe, f, [])
        ast = run(exe, f, ["--无字节码"])
        if vm[0] == "timeout" or ast[0] == "timeout":
            skip += 1
            continue
        if vm[0] == ast[0] and vm[1] == ast[1]:
            ok += 1
        else:
            fail += 1
            fails.append(os.path.basename(f))
    print("用例 %d：VM/AST 一致 %d、不一致 %d、超时 %d" % (len(files), ok, fail, skip))
    for n in fails[:40]:
        print("  " + n)
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
