# 文语言检查插件（@local/wen-check）

把一个 `wen_check` 工具装进 DSH（DeepSeek Harness），让 AI 在写完 `.wen` 之后**自己检查**，拿到的是结构化诊断而不是一段文本。

## 它做什么

模型调用 `wen_check({ path })` → 插件执行：

```
wen.exe --检查 --json <文件>
```

返回：

```json
{
  "ok": false,
  "file": "…/bad.wen",
  "exitCode": 1,
  "statementCount": 0,
  "diagnostics": [
    {
      "kind": "语法错误",
      "line": 2,
      "column": 3,
      "reason": "这里需要一个名字，却遇到运算符「³」；运算符不能当名字，请改名（例如「相加」）",
      "detail": "脚本结构不符合语法，例如缺「则」/「结束」……",
      "sourceLine": { "number": 2, "text": "将 立方 变成 2" }
    }
  ],
  "truncated": false
}
```

`limit` 参数控制最多返回几条（默认 20，上限 200），超出时 `truncated: true`。

## 前提

1. 已安装 DSH；
2. 机器上有 `wen.exe`（文语言 v0.1 及以上，需支持 `--检查 --json`）。

## 安装

把本目录（`插件/wen-check`）整体保留，然后在当前 profile 里安装：

```text
plugin_manager  action: install_bundle  target: <本目录绝对路径>
```

装好后工具名是 `wen_check`，下一个会话即可用。

## 找到 wen.exe 的顺序

1. 插件配置项 `exePath`
2. 环境变量 `WEN_EXE`
3. PATH 里的 `wen` / `wen.exe`
4. 旧默认路径 `C:\Users\Acer\Desktop\zhongwen_lang\wen.exe`

推荐把 `wen.exe` 放进 PATH，或者设一次 `WEN_EXE`：

```powershell
setx WEN_EXE "D:\tools\wen\wen.exe"
```

## 不用 DSH 也能用

`--检查 --json` 是语言自带的能力，任何编辑器、CI、脚本都能直接调：

```powershell
wen.exe --检查 --json 例子\你好.wen
```

退出码 `0` 通过、`1` 有诊断，stdout 一行 JSON。
