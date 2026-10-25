# dsh-wen-check

DSH（DeepSeek Harness）插件：把「文」语言的编译诊断接成 `wen_check` 工具，模型写完 `.wen` 直接自检，拿回结构化 JSON（行号、列号、原因、源码行）。

## 它依赖什么

- DSH；
- 本机有 `wen.exe`（文语言 v0.1+，支持 `--检查 --json`；老版本也能用，插件会自动退回解析文本）。

`wen.exe` 的查找顺序：插件配置 `exePath` → 环境变量 `WEN_EXE` → PATH 里的 `wen` / `wen.exe` → 旧默认路径。

## 安装（三选一）

**1）Git 分支地址（推荐）**

```
git+https://gitee.com/zhuchen_1/wen.git#plugin-wen-check
```

**2）压缩包地址**

```
https://gitee.com/zhuchen_1/wen/raw/plugin-wen-check/wen-check-0.3.1.tgz
```

下载后解压，在 DSH 的「添加插件」里粘贴目录绝对路径。

**3）本地目录**

把 `插件/wen-check` 复制到任意目录，在 DSH 的「添加插件」里粘贴那个目录的绝对路径。

装好后工具名是 `wen_check`。

## 用法

```json
{ "path": "D:\\proj\\例子\\你好.wen", "limit": 20 }
```

返回：

```json
{
  "ok": false,
  "file": "D:\\proj\\例子\\你好.wen",
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

`limit`：最多返回几条诊断，默认 20，上限 200。

## 不用 DSH 也能用

`--检查 --json` 是语言自带的能力，任何编辑器 / CI / 脚本都能直接调：

```powershell
wen.exe --检查 --json 例子\你好.wen
```

退出码 `0` 通过、`1` 有诊断，stdout 一行 JSON。参考解析器见文语言仓库的 `插件/参考-解析器.py`。

## 许可证

MIT。
