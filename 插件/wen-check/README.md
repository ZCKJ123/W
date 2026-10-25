# dsh-wen-check

DSH（DeepSeek Harness）插件：给「文」语言加两个工具。

- `wen_check`：检查 `.脚本` / `.wen`，返回结构化诊断和改法 `hint`。
- `wen_run`：直接运行，返回 `exitCode` / `stdout` / `stderr` / 错误行号。

## 安装

DSH「添加插件」粘贴：

```text
https://gitee.com/zhuchen_1/wen/raw/p/pkg.tgz
```

安装包自带 `wen.exe`，不用另下。

ZIP 备用：

```text
https://gitee.com/zhuchen_1/wen/raw/p/pkg.zip
```

## wen_check

检查文件：

```json
{ "path": "D:\\proj\\例子\\你好.脚本" }
```

直接检查一段代码，不用先落盘：

```json
{ "source": "打印 1\n将 立方 变成 2\n" }
```

返回：

```json
{
  "ok": false,
  "exitCode": 1,
  "diagnostics": [
    {
      "kind": "语法错误",
      "line": 2,
      "column": 3,
      "reason": "这里需要一个名字，却遇到运算符「³」；运算符不能当名字，请改名（例如「相加」）",
      "hint": "换一个不是运算符的名字，例如「相加」「平方算」。",
      "sourceLine": { "number": 2, "text": "将 立方 变成 2" }
    }
  ]
}
```

`limit` 默认 20，上限 200。

## wen_run

```json
{ "path": "D:\\proj\\例子\\你好.脚本" }
```

返回：

```json
{
  "ok": true,
  "exitCode": 0,
  "line": 0,
  "stdout": "你好\n",
  "stderr": ""
}
```

## wen.exe 查找顺序

1. 插件配置 `exePath`
2. 环境变量 `WEN_EXE`
3. 插件目录自带的 `wen.exe`
4. 缓存下载
5. PATH 里的 `wen` / `wen.exe`

## 许可证

MIT。
