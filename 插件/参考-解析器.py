#!/usr/bin/env python3
"""文语言诊断解析器（最小参考实现，仅标准库）。

用途：不装 DSH 插件，也能把 `wen.exe --检查` 的结果变成结构化数据。
把它丢给任何 AI，让它照这个在你自己的项目里重写一份即可。

用法：
    python 参考-解析器.py 路径\\到\\脚本.wen
    python 参考-解析器.py 路径\\到\\脚本.wen --exe D:\\tools\\wen.exe

输出：一行 JSON
    {"ok": bool, "file": str, "exitCode": int, "statementCount": int,
     "diagnostics": [{"kind","line","column","reason","detail","sourceLine"}],
     "truncated": bool, "error": str?}
"""

import json
import os
import re
import subprocess
import sys

MAX_DIAGS = 20


def split_args(argv):
    """返回 (目标文件, 检查器路径)。--exe 后面的值是检查器，不算目标文件。"""
    target = None
    exe = os.environ.get("WEN_EXE") or "wen.exe"
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--exe":
            if i + 1 < len(argv):
                exe = argv[i + 1]
                i += 2
                continue
        elif a == "--limit":
            i += 2
            continue
        elif not a.startswith("--"):
            target = a
        i += 1
    return target, exe


def decode(raw):
    for enc in ("utf-8", "gbk"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", "replace")


def parse_text(stderr):
    """旧版 wen.exe（没有 --json）的四段式文本兜底解析。"""
    text = (stderr or "").replace("\r\n", "\n")
    starts = [m.start() for m in re.finditer(r"错误\s*\d+\s*/\s*\d+", text)]
    chunks = [text[a:b] for a, b in zip(starts, starts[1:] + [len(text)])] if starts else ([text] if "【" in text else [])
    out = []
    for chunk in chunks:
        kind = re.search(r"【(.+?)】", chunk)
        pos = re.search(r"位置：(.+?)\s+第\s*(\d+)\s*行(?:\s*第\s*(\d+)\s*列)?", chunk)
        reason = re.search(r"原因：([^\n]*)", chunk)
        detail = re.search(r"说明：([^\n]*)", chunk)
        code = re.search(r"^\s*(\d+)\s*\|\s?(.*)$", chunk, re.M)
        item = {
            "kind": kind.group(1) if kind else "",
            "line": int(pos.group(2)) if pos else 0,
            "column": int(pos.group(3)) if pos and pos.group(3) else 0,
            "reason": reason.group(1).strip() if reason else "",
            "detail": detail.group(1).strip() if detail else "",
        }
        if code:
            item["sourceLine"] = {"number": int(code.group(1)), "text": code.group(2)}
        out.append(item)
    return out


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass

    target, exe = split_args(sys.argv[1:])
    if not target:
        print("用法：python 参考-解析器.py 文件.wen [--exe wen.exe]", file=sys.stderr)
        return 2
    target = os.path.abspath(target)

    try:
        proc = subprocess.run(
            [exe, "--检查", "--json", target],
            capture_output=True, timeout=30,
        )
        stdout, stderr, code = decode(proc.stdout), decode(proc.stderr), proc.returncode
    except FileNotFoundError:
        print(json.dumps({
            "ok": False, "file": target, "exitCode": -1, "statementCount": 0,
            "diagnostics": [], "truncated": False,
            "error": f"找不到 wen 检查器：{exe}（放进 PATH 或设 WEN_EXE）",
        }, ensure_ascii=False))
        return 1

    payload = None
    if stdout.strip().startswith("{"):
        try:
            payload = json.loads(stdout)
        except json.JSONDecodeError:
            payload = None

    if payload is not None:
        diags = payload.get("diagnostics") or []
        result = {
            "ok": bool(payload.get("ok")),
            "file": payload.get("file") or target,
            "exitCode": code,
            "statementCount": int(payload.get("statementCount") or 0),
            "diagnostics": diags[:MAX_DIAGS],
            "truncated": len(diags) > MAX_DIAGS,
        }
    else:
        diags = parse_text(stderr)
        count = re.search(r"语法通过：顶层\s*(\d+)", stdout)
        result = {
            "ok": code == 0,
            "file": target,
            "exitCode": code,
            "statementCount": int(count.group(1)) if count else 0,
            "diagnostics": diags[:MAX_DIAGS],
            "truncated": len(diags) > MAX_DIAGS,
        }

    print(json.dumps(result, ensure_ascii=False))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
