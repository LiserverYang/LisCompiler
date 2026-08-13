# Lis Language Support (VSCode)

Lis 语言的 VSCode 扩展:语法高亮 + lisls 语言服务器(诊断 / 跳转定义 / 悬停 / 补全)。

## 构建

```bash
npm install
npm run compile
```

## 安装(开发)

1. 确认 `lisls.exe` 在 PATH,或在 VSCode 设置 `lis-lang.lislsPath` 指向它
   (LisCompiler 构建产物 `Build/Binaries/lisls.exe`,其旁需有 `lstdlib/`)。
   **Windows 注意**:lisls.exe 由 MinGW 编译,依赖 MinGW 运行时 DLL ——
   `C:\MinGW\bin` 需在 VSCode 进程的 PATH 中(系统环境变量)。
2. 打开本目录,按 F5 启动扩展开发宿主,或
   `npx vsce package` 打包 .vsix 后 `code --install-extension lis-lang-0.1.0.vsix`。

## 功能

- **语法高亮**:关键字 / 类型 / 字面量 / 注释 / 属性
- **实时诊断**:保存时(400ms 防抖)全量编译,错误红波浪线 + E 错误码
- **跳转定义**(Ctrl+点击 / F12)、**悬停**(类型签名)、**补全**(关键字 + 全局符号)

打开任意 `.lis` 文件即激活。
