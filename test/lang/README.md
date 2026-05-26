# Jiang language tests

`test/lang` 保存源码级语言语义用例，和 `test/smoke` 的内部模块 API 测试分开。

- `check/`：期望 `jiangc --check` 成功的语言用例。
- `fail/`：期望 `jiangc --check` 失败的语言用例，文件头用 `expected:` 标出核心 diagnostic code。
- `run/`：后续用于需要生成并运行目标程序的端到端语言用例。
- `diagnostic/`：后续用于精确检查多条 diagnostic、span 和消息的用例。

运行方式：

```bash
JIANGC=/path/to/jiangc ./script/lang_check.sh
```

runner 当前只区分 `check` / `fail` 的退出码；精确 diagnostic 匹配等 CLI 输出稳定后再接入。
