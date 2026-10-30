# PE 原生运行时路线

当前决定：先做 **x64 PE 原生文件写入**，不先追完整自举。

## 已验证

```text
x64 PE：打印常量文本        ✓
x64 PE：打印整数常量表达式  ✓
x64 PE：列表/字典常量下标   ✓
x86 PE：打印常量文本        ✓
```

## 失败记录

### msvcrt 路线

导入：

```text
fopen / fwrite / fclose
```

结果：

```text
PE 能加载，进程挂起，文件未创建
```

### kernel32 路线

导入：

```text
CreateFileA / WriteFile / CloseHandle
```

修正了 ILT/IAT 从 0x20 改到 0x28 的重叠问题，仍然：

```text
PE 能加载，进程挂起，文件未创建
```

## 探针进度
```text
1. ExitProcess 立即退出      ✓ 已有 x64 PE
2. CreateFileA 创建文件        ✓ 2026-10-30 探针通过，exit=0，文件存在
3. WriteFile 写入固定字符串   ✓ 探针通过，内容正确
4. CloseHandle               ✗（当前未关句柄）
5. 接入 写入(路径,文本)       ✓ x64 PE 创建并写入成功
```
本轮修复：生成 `lea` 时只发出了 3 字节指令，没有先预留 4 字节位移占位，后续写位移把前面的指令覆盖，导致 PE 入口执行到乱码。
## 下一步

不要直接接语言 `写入`。先做独立的 PE 探针：

```text
1. 只导入 ExitProcess，入口立即退出 7
2. 只导入 CreateFileA，成功后 ExitProcess 0，失败 1
3. 加 WriteFile，写入一个固定字符串
4. 加 CloseHandle
5. 最后才接 `写入(路径, 文本)`
```

每一步都用：

```text
工具/原生EXE检查.ps1
Start-Process -Wait
Get-Content 目标文件
```

验收，不通过就停在当前步骤。

## 运行时顺序

```text
文件写入
  → 字符串变量/拼接
  → 列表/字典运行时表示
  → 错误对象
  → 自举编译器独立
  → 32 位自举
```
