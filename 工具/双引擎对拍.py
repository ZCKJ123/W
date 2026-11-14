# -*- coding: utf-8 -*-
"""双引擎对拍：拿两个 wen.exe 跑同一批用例，比较 stdout/stderr/退出码。

用法：python 工具/双引擎对拍.py 旧.exe 新.exe [用例目录]
stderr 里的具体可执行名会抹平，其余逐字比较。
"""

import os
import sys
import glob
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(exe, f, timeout=20):
    try:
        p = subprocess.run([exe, f], cwd=ROOT, capture_output=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return "timeout", b"", b""


def main():
    if len(sys.argv) < 3:
        print("用法：python 工具/双引擎对拍.py 旧.exe 新.exe [用例目录]")
        return 1
    old, new = sys.argv[1], sys.argv[2]
    case_dir = sys.argv[3] if len(sys.argv) > 3 else os.path.join(ROOT, "测试", "用例")
    files = sorted(glob.glob(os.path.join(case_dir, "*.wen"))
                   + glob.glob(os.path.join(case_dir, "*.编译源")))
    ok = fail = skip = 0
    fails = []
    for f in files:
        a = run(old, f)
        b = run(new, f)
        if a[0] == "timeout" or b[0] == "timeout":
            skip += 1
            continue
        if a[0] == b[0] and a[1] == b[1]:
            ea = a[2].decode("utf-8", "ignore").replace(os.path.basename(old), "<EXE>")
            eb = b[2].decode("utf-8", "ignore").replace(os.path.basename(new), "<EXE>")
            if ea == eb:
                ok += 1
                continue
            fails.append((os.path.basename(f), "stderr 不同"))
        else:
            fails.append((os.path.basename(f),
                          "退出码 %s vs %s / stdout %s" %
                          (a[0], b[0], "一致" if a[1] == b[1] else "不同")))
        fail += 1
    print("用例 %d：一致 %d、不一致 %d、超时 %d" % (len(files), ok, fail, skip))
    for n, w in fails[:40]:
        print("%s：%s" % (n, w))
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
