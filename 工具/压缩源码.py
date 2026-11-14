# -*- coding: utf-8 -*-
"""文语言源码压缩器。

做三件事，全部只在文本层，不改变语义：
  1. 删掉整行注释（行首 #）
  2. 删掉空行（语言里空行只用于分隔，压缩后全删，顺带绕开"连续两空行报错"）
  3. 去掉每行行尾空白

可选（--内联）：把同一层里连续的"单条简单语句"用分号并到一行。
  语言已经支持分号分隔语句，所以这步是合法的；含块关键字的行一律不并，
  避免把 `如果 … 则` / `结束` 这种结构边界粘在一起。
可选（--扁平）：把 `如果 A 则` 紧接 `如果 B 则`、且两个 `结束` 紧邻的嵌套
  折叠成 `如果 A 且 B 则`。这是"去掉多余嵌套层"的那一刀，条件很严，折叠不了就原样保留。

用法：
  python 工具/压缩源码.py 源.wen 目标.wen [--内联] [--扁平]

输出一份压缩统计（字节、行数、比例），方便记录到文档里做对比。
"""

import sys
import os
import io

BLOCK_WORDS = ("如果", "否则如果", "否则", "当", "对于", "函数", "尝试",
               "捕获", "最终", "匹配", "情形", "情况", "结束")

# 这些行的行首就是块结构或声明行，绝不参与内联
# （`依赖` 后面必须换行、不能接别的语句；`导出` 同理）
BLOCK_PREFIX = ("如果", "否则如果", "否则", "当", "对于", "函数", "尝试",
                "捕获", "最终", "匹配", "情形", "情况", "结束", "依赖", "导出")


def read_text(path):
    with io.open(path, "r", encoding="utf-8-sig") as f:
        return f.read()


def write_text(path, text):
    d = os.path.dirname(path)
    if d and not os.path.isdir(d):
        os.makedirs(d)
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def leading(line):
    return line[:len(line) - len(line.lstrip(" \t"))]


def strip_lines(text):
    """去掉注释行、空行、行尾空白。"""
    out = []
    removed_comment = 0
    removed_blank = 0
    for raw in text.replace("\r\n", "\n").split("\n"):
        line = raw.rstrip(" \t\r")
        s = line.strip()
        if not s:
            removed_blank += 1
            continue
        if s.startswith("#"):
            # `# 牵引: 全部` 这类注释是被解释器 scanPull 扫描的语义注释，
            # 删掉会直接改掉内存库的压缩策略，必须原样保留。
            body = s[1:].strip()
            if body.startswith("牵引"):
                out.append(line)
                continue
            removed_comment += 1
            continue
        out.append(line)
    return out, removed_comment, removed_blank


def scan_line(line, depth=0, in_str=None):
    """扫一行，返回 (行末括号深度, 行末字符串状态)。

    语言允许括号跨行（`打印 最大(\\n 1,\\n 2\\n)`），也允许三引号多行文本
    （`将 诗 变成 \"\"\"\\n…\\n\"\"\"`）。压缩时必须把这种行整段当成一条语句，
    不能按行并；字符串里面的括号与分号一律不计。
    in_str: None / '"' / "'" / '\"\"\"'
    """
    i = 0
    n = len(line)
    esc = False
    while i < n:
        ch = line[i]
        if in_str:
            if in_str == '"""':
                if line.startswith('"""', i):
                    in_str = None
                    i += 3
                    continue
                i += 1
                continue
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == in_str:
                in_str = None
            i += 1
            continue
        if line.startswith('"""', i):
            in_str = '"""'
            i += 3
            continue
        if ch in ("\"", "'"):
            in_str = ch
        elif ch == "#":
            break
        elif ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        i += 1
    return depth, in_str


def line_depths(lines):
    """给每行算「行首/行末括号深度」和「行首/行末字符串状态」。"""
    starts = []
    ends = []
    sstr = []
    estr = []
    d = 0
    st = None
    for ln in lines:
        starts.append(d)
        sstr.append(st)
        d, st = scan_line(ln, d, st)
        ends.append(d)
        estr.append(st)
    return starts, ends, sstr, estr


def is_simple(line, start_depth=0, end_depth=0, str_start=None, str_end=None):
    """单条简单语句：不含块结构关键字，括号本行闭合，且不在跨行字符串里。"""
    s = line.strip()
    if s.startswith("#"):
        # 注释行永远独占一行：一旦被并进别的语句，`#` 会把后面的代码全注释掉
        return False
    if start_depth != 0 or end_depth != 0:
        return False
    if str_start is not None or str_end is not None:
        return False
    for w in BLOCK_PREFIX:
        if s.startswith(w):
            return False
    if s.endswith("则"):
        return False
    # 行内出现块关键字（例如 `函数` 定义在同一行）也不并
    for w in ("如果", "对于", "函数", "匹配", "尝试", "结束"):
        if w in s:
            return False
    return True


def inline(lines, starts, ends, sstr, estr, max_per_line=6):
    """同缩进、连续、都是完整简单语句的行用分号并起来。"""
    out = []
    i = 0
    merged = 0
    while i < len(lines):
        cur = lines[i]
        ind = leading(cur)
        if not is_simple(cur, starts[i], ends[i], sstr[i], estr[i]):
            out.append(cur)
            i += 1
            continue
        group = [cur.strip()]
        j = i + 1
        while j < len(lines) and len(group) < max_per_line:
            nxt = lines[j]
            if leading(nxt) != ind or not is_simple(nxt, starts[j], ends[j], sstr[j], estr[j]):
                break
            group.append(nxt.strip())
            j += 1
        if len(group) > 1:
            merged += len(group) - 1
            out.append(ind + "; ".join(group))
        else:
            out.append(cur)
        i = j
    return out, merged


def flatten_if(lines, starts, ends, sstr, estr):
    """把紧邻的两层 `如果` 折叠成 `如果 A 且 B 则`。

    只认这种形态，且必须严格满足：
        如果 A 则
            如果 B 则
                <体>
            结束
        结束
    体必须是连续的非空行，且中间不出现 `否则`。
    """
    out = []
    i = 0
    folded = 0
    n = len(lines)
    while i < n:
        s = lines[i].strip()
        if (s.startswith("如果") and s.endswith("则")
                and starts[i] == 0 and ends[i] == 0
                and sstr[i] is None and estr[i] is None):
            ind = leading(lines[i])
            cond_a = s[2:-1].strip()
            # 下一行必须是同层再缩进的 如果 B 则
            if i + 1 < n:
                nxt = lines[i + 1]
                nind = leading(nxt)
                ns = nxt.strip()
                if (len(nind) > len(ind) and ns.startswith("如果") and ns.endswith("则")):
                    cond_b = ns[2:-1].strip()
                    # 从 i+2 起找内层 结束（同 nind 缩进），且它后面紧跟外层 结束
                    k = i + 2
                    depth = 1
                    inner_end = -1
                    while k < n:
                        ks = lines[k].strip()
                        kind = leading(lines[k])
                        if ks.startswith("如果") and ks.endswith("则"):
                            depth += 1
                        elif ks == "结束":
                            depth -= 1
                            if depth == 0:
                                inner_end = k
                                break
                        k += 1
                    if inner_end > 0 and inner_end + 1 < n:
                        if lines[inner_end + 1].strip() == "结束":
                            body = lines[i + 2:inner_end]
                            body_ok = all(not ln.strip().startswith(("否则", "否则如果"))
                                          for ln in body)
                            if body_ok and cond_a and cond_b:
                                out.append(ind + "如果 " + cond_a + " 且 " + cond_b + " 则")
                                out.extend(body)
                                out.append(ind + "结束")
                                folded += 1
                                i = inner_end + 2
                                continue
        out.append(lines[i])
        i += 1
    return out, folded


def main(argv):
    if len(argv) < 3:
        print("用法：python 工具/压缩源码.py 源.wen 目标.wen [--内联] [--扁平]")
        return 1
    src, dst = argv[1], argv[2]
    do_inline = "--内联" in argv
    do_flat = "--扁平" in argv

    text = read_text(src)
    src_bytes = len(text.encode("utf-8"))
    src_lines = len(text.replace("\r\n", "\n").split("\n"))

    lines, rc, rb = strip_lines(text)
    starts, ends, sstr, estr = line_depths(lines)
    merged = 0
    folded = 0
    if do_inline:
        lines, merged = inline(lines, starts, ends, sstr, estr)
        starts, ends, sstr, estr = line_depths(lines)
    if do_flat:
        lines, folded = flatten_if(lines, starts, ends, sstr, estr)

    result = "\n".join(lines) + "\n"
    write_text(dst, result)

    dst_bytes = len(result.encode("utf-8"))
    dst_lines = len(lines)
    pct = (1 - dst_bytes / src_bytes) * 100 if src_bytes else 0
    print("源文件：%s" % src)
    print("目标文件：%s" % dst)
    print("字节：%d -> %d（省 %.1f%%）" % (src_bytes, dst_bytes, pct))
    print("行数：%d -> %d" % (src_lines, dst_lines))
    print("删注释 %d 行、删空行 %d 行、并语句 %d 条、折叠嵌套 %d 处" % (rc, rb, merged, folded))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
