# 文语言（wen）

[![为爱发电](为爱发电.svg)](https://afdian.com/a/akmwen)

用中文关键字写程序的小语言：解释运行、编译成原生 exe、带 33 个内置库。

- 版本：`0.1.0-snapshot`
- 许可证：MIT
- 内置签名：`WEN-LANG-0.1`
- 语法产生式：[语法.ebnf](语法.ebnf)
- 语法总表：[语法总表.md](语法总表.md)
- 语言规范：[语言规范.md](语言规范.md)
- 为爱发电：[为爱发电.md](为爱发电.md)
- 作者：Akm / 筑宸科技

## 构建与运行

```powershell
.\完整构建.bat   # 完整版：wen_full.exe
.\wen.exe 例子\你好.wen
.\wen.exe --签名
.\wen.exe --验证
.\wen.exe --库
.\发布检查.ps1   # 发货前检查
```

## 能做什么

- 中文关键字、少括号、块用 `则 … 结束`
- 数学/概率/物理/图形/函数库
- 文件/网络/网页/JSON/CSV
- 时间、路径、日志、哈希、Base64/URL 编码、正则
- SVG / OpenGL 4.6 / Vulkan 1.3 / WebGPU 导出
- 3D 渲染：立方体/球/OBJ；OpenGL 3D 走深度缓冲，Vulkan/WebGPU 暂投影
- 外派到 Python/JavaScript/JSON/Java
- 翻译到字节码/C++/汇编；字节码可执行
- 二进制缓冲、CPU 并行归约
- 内存缓存、压缩、牵引、跃迁、加速

## 文档

- [性能基准](性能基准.md)

- [使用教程.md](使用教程.md) —— 先看这个：安装、语法、库用途、文件地图
- [使用规范.md](使用规范.md) —— 语法、风格、贡献、库准入
- [更新目录.md](更新目录.md) —— 只记新增、修改、删除
- [LICENSE](LICENSE) —— MIT

## 署名

源码和生成物都带内置签名：

```text
文语言（wen） · 内置签名 WEN-LANG-0.1 · 2026 · 作者 Akm / 筑宸科技
```

`wen.exe --签名` 可直接打印。分发时保留本签名与 `LICENSE` 即可。
