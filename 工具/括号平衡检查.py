# -*- coding: utf-8 -*-
"""粗查花括号平衡：逐行统计（跳过 // 行注释；/* */ 块注释粗略处理）。

用来快速判断编辑有没有多删/多留一个花括号，从而让后续函数嵌套错位。
"""

import sys

def stats(path):
    text = open(path, "r", encoding="utf-8").read()
    op = cl = 0
    in_block = False
    depth_by_line = []
    depth = 0
    for ln, line in enumerate(text.splitlines(), 1):
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
            if line.startswith('"', i):
                i += 1
                while i < len(line) and line[i] != '"':
                    if line[i] == "\\":
                        i += 1
                    i += 1
                i += 1
                continue
            if line[i] == "{":
                op += 1
                depth += 1
            elif line[i] == "}":
                cl += 1
                depth -= 1
                if depth == 0:
                    depth_by_line.append(ln)
            i += 1
    return op, cl, depth, depth_by_line

a = stats(sys.argv[1])
b = stats(sys.argv[2]) if len(sys.argv) > 2 else None
print("%s：{ %d  } %d  结尾深度 %d" % (sys.argv[1], a[0], a[1], a[2]))
if b:
    print("%s：{ %d  } %d  结尾深度 %d" % (sys.argv[2], b[0], b[1], b[2]))
    print("差值：{ %+d  } %+d" % (a[0] - b[0], a[1] - b[1]))
    # 找出两版里「深度回到 0」的行号差异，那通常就是函数边界错位的地方
    extra = [x for x in a[3] if x not in b[3]]
    print("新版多出的顶格闭合行（前 20）：%s" % extra[:20])
