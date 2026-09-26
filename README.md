<p align="center">
  <img src="doc/logo.svg" alt="Jiang" width="160">
</p>

# Jiang 语言

Jiang 编程语言。项目仍处于早期阶段，语言与标准库会持续迭代。

[官网与语言文档](https://jiang-lang.org/) · [发行版](https://github.com/jjcfun/jiang/releases)

## 安装

支持 **macOS Apple Silicon** 和 **Linux x86_64**。在终端粘贴以下命令：

```bash
curl -fsSL https://github.com/jjcfun/jiang/releases/latest/download/install.sh | bash && . "$HOME/.jiang/env"
```

自动下载最新发行版、校验 SHA-256、安装到 `~/.jiang` 并为 bash/zsh 配置 PATH。验证安装：

```bash
jiang --version
```

一键安装入口从 0.6.0 发行版开始提供。也可以在 [Releases](https://github.com/jjcfun/jiang/releases)
下载对应平台的压缩包，解压后运行 `./install.sh`。

编译程序需要系统 C 工具链：macOS 可运行 `xcode-select --install`；Ubuntu/Debian 可运行
`sudo apt install build-essential`。无需安装 LLVM 或下载编译器源码。

## 第一个程序

保存为 `hello.jiang`：

```jiang
Int main() {
    print("Hello, Jiang!");
    return 0;
}
```

编译并运行：

```bash
jiang -o hello hello.jiang
./hello
```

## VS Code

在扩展市场搜索并安装 **Jiang Language**，然后打开 `.jiang` 文件。
Jiang 0.6.0 及更新版本提供诊断、补全、悬停文档和定义跳转。

扩展默认从 PATH 查找 `jiang`。安装 CLI 后重新打开 VS Code；如果未找到，在扩展设置中将
`Jiang: Server Path` 设置为 `~/.jiang/bin` 对应的绝对路径。
[扩展使用说明](https://github.com/jjcfun/vscode-jiang)

## 文档

- [语言指南](doc/jiang.md)
- [版本说明](doc/releases/0.6.0.md)
- [构建、测试与发布](doc/build-and-test.md)
- [编译器开发流程](doc/develop.md)
- [架构](doc/architecture.md) · [语法](doc/grammar.md) · [语言设计](doc/language-design.md)

## License

Copyright 2026 JiangJunChen。采用 Apache License 2.0，详见 [LICENSE](LICENSE) 和 [NOTICE](NOTICE)。
