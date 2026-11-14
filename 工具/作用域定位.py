# -*- coding: utf-8 -*-
"""找出指定行所处的花括号作用域链，用来看「某个 lambda 定义在这里，别处能不能用」。

用法：python 作用域定位.py 文件 行号1 行号2 ...
输出每一行所在的 block 起始行号链（从外到内）。
"""

import sys

def scan(path):
    text = open(path, "r", encoding="utf-8").read()
    lines = text.splitlines()
    stack = []          # 每个元素是这一层 { 所在的行号
    depth_at = {}       # 行号 -> 该行开头时的作用域链
    in_block = False
    for ln, line in enumerate(lines, 1):
        depth_at[ln] = list(stack)
        i = 0
        while i < len(line):
            if in_block:
                if line.startswith("*/", i):
                    in_block = False
                    i += 2
                    continue
                i += 1
                continue
            if line.startswith("//", i):
                break
            if line.startswith("/*", i):
                in_block = True
                i += 2
                continue
            ch = line[i]
            if ch == '"' or ch == "'":
                q = ch
                i += 1
                while i < len(line) and line[i] != q:
                    if line[i] == "\\":
                        i += 1
                    i += 1
                i += 1
                continue
            if ch == "{":
                stack.append(ln)
            elif ch == "}":
                if stack:
                    stack.pop()
            i += 1
    return lines, depth_at

lines, depth_at = scan(sys.argv[1])
for arg in sys.argv[2:]:
    n = int(arg)
    print("%d 行处于作用域链：%s" % (n, depth_at.get(n, [])))
    print("   内容：%s" % (lines[n - 1].strip()[:70]))
