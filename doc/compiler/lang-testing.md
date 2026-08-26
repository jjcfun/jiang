# Language Testing 设计

`test/lang` 是源码级语言契约测试。测试按结果类型分层，把“能通过检查”“必须报错”
“能生成并运行”“能生成目标 IR”分开，而不是只靠端到端 smoke。

## 目录模型

```text
test/lang/<feature>/
  check/       期望 jiangc --check 成功
  fail/        期望 jiangc --check 失败，并用 expected 精确匹配诊断
  run/         期望 emit/link/run 成功，可用 expected-exit 匹配退出码
  emit/        期望 emit-llvm 成功，不要求运行
  diagnostic/ 未来用于精确检查多条诊断、span 和消息
```

`feature` 按语言功能命名，使用 snake_case。不要按编译阶段建目录；语言测试关注用户可见语义，
不是关注 parser/type_check/JIL 的内部实现。

## 覆盖原则

每个语言功能点都要按矩阵补齐用例：

- 正例：最小合法写法、完整显式写法、常见组合写法。
- 反例：语法错误、类型错误、名字解析错误、可见性/作用域错误。
- 边界：空列表、单元素、多元素、尾逗号、省略写法、嵌套写法。
- 交互：与泛型、trait、method、module、ownership、lifetime、default/named args 的组合。
- 后端：会影响 layout/JIL/backend 的功能必须至少有一个 `run/` 或 `emit/` 用例。
- 诊断：每个新增错误分支至少有一个 `fail/` 用例，并写 `// expected: code`。

覆盖目标不是“每个文件很多断言”，而是“每个语法/语义分支至少有一个稳定用例”。一个用例可以覆盖
多个正向组合，但反例应尽量一个文件对应一个 diagnostic，方便定位回归。

## 当前功能矩阵

详细覆盖状态见 `doc/compiler/lang-coverage.md`。当前 release 需要优先覆盖这些 feature：

- `literal`：integer/float/char/string/bool/null，expected type 转换，字符串与 `UInt8[:0]&`、`UInt8*` 兼容语义。
- `type`：type suffix 顺序、handle 限制、raw pointer/slice/array、已移除 many pointer 的反例、
  errorable。
- `aggregate`：tuple、array、slice、struct 默认构造、enum constructor。
- `function`：tail expr、call stmt、overload、default params、named args、constructor args。
- `control_flow`：if/switch/guard/while/for/range/defer/break/continue/Never 合并。
- `nominal`：struct/enum/init/deinit/type function/instance method/member namespace。
- `generic`：generic type/function、where bound、trait conformance、associated type、monomorph。
- `ownership`：Copyable、默认 move、显式 move、`!Movable`、borrow reference、drop/defer cleanup。
- `lifetime`：`@life` 语法、返回引用、字段引用、逃逸失败。
- `error_handling`：`Result<T, E>`、throw、try/catch、catch binding、未处理错误。
- `import`：file import、public import re-export、visibility。
- `package`：manifest dependency、package public surface、跨 package 访问、dependency cycle。
- `runtime`：main/runtime entry、sentinel C string、print/panic/assert 等 runtime-visible 能力。

## 补测试顺序

1. 先按 `doc/grammar.md` 的语法规则补 parser/type check 可见的 check/fail 用例。
2. 再按 `doc/language-design.md` 的语义章节补类型、所有权、lifetime、泛型和模块用例。
3. 最后给所有会影响 JIL/layout/backend 的功能补 `run/` 或 `emit/` 用例。

`script/lang_check.sh` 默认的 `run/` 会用 `jiangc --emit-llvm` 生成 LLVM IR，再用 LLVM clang
链接运行。需要覆盖 release object/executable 路径时，设置：

```bash
LANG_CHECK_RELEASE_RUNS=1 JIANGC=./build/bin/jiangc bash ./script/lang_check.sh
```

这会额外对所有 `run/` 用例执行 `jiangc --mode release -o ...`，覆盖 LLVM codegen opt level 2
和 `default<O2>` pass pipeline。

`run/` 默认必须跨 hosted 平台运行。确实依赖平台系统 API 的用例可用
`// test-platform: macos` 或 `// test-platform: linux` 限定宿主；`// expected-exit: trap`
用于匹配 LLVM trap 在不同宿主上的 SIGILL 或 SIGTRAP，不能匹配 SIGSEGV。

任何新增语言能力必须同步更新本矩阵；如果某个语义尚未定稿，应在对应 TODO 中标注，不能用临时
测试假定长期规则。

## 统一 runner

`script/test.sh` 是语言测试和编译器模块测试共用的执行器。`script/smoke.sh` 只提供日常快速
profile，仍把清单交给同一个 runner；它不定义另一套成功、失败或运行语义。

runner 默认使用逻辑 CPU 数和 4 中的较小值作为进程数。下面两条命令选择完全相同的用例，
结果按发现顺序输出；后一条只改变同时执行的用例数：

```bash
TEST_JOBS=1 JIANGC=./build/bin/jiangc bash ./script/test.sh
TEST_JOBS=4 JIANGC=./build/bin/jiangc bash ./script/test.sh
```

每个 case 在一次运行中拥有唯一的 `work` 和 mutable artifact cache。P3 完成并发安全的
artifact 发布前，测试进程不会共享默认 `build/cache`。语言测试的 `check`、`fail` 和 `emit`
各只调用一次编译器；`run` 只执行一次 emit、link 和 program run。profile 选择与日志汇总
不会再次编译同一 case。

`test/compiler/*/run` 使用 `test/compiler/compiler.jiang` 作为聚合入口。各模块公开一个
`run()`，runner 先把全部编译器单元测试构建为一个 executable，再按 case 路径分别启动它。
因此编译器源码只编译和链接一次，而每个 case 仍保持进程、退出码和全局状态隔离。sanitizer
模式同样只生成一个聚合 executable；`TEST_RELEASE_RUNS=1` 会额外构建一个 release 聚合产物。

runner 执行测试程序时会设置 `JIANG_TEST_WORK_DIR`，并为需要模拟独立 package 的用例提供
仓库外的 `JIANG_TEST_TEMP_DIR`。普通日志、object 和 cache 放在 work 目录；会触发 package
root 查找的临时源码必须放在 temp 目录。两者都按 case 唯一分配，不能使用共享的固定路径。
成功时外部 temp 自动清理；失败时 runner 会打印并保留其路径。

常用控制项：

- `TEST_FILTER=<regex>`：按路径正则筛选所有类别。
- `TEST_LIST=<file>`：只运行清单中逐行列出的仓库相对路径。
- `TEST_RUN_FILTER=<regex>`：进一步筛选需要链接执行的 `run` 用例。
- `TEST_JOBS=<n>`：设置最大并发进程数；`1` 用于稳定地串行复现。
- `TEST_TIMEOUT=<seconds>`：限制单个 case 的总时长，`0` 表示不限制。
- `TEST_KEEP_GOING=1`：失败后继续完成全部已选择用例。
- `TEST_KEEP_WORK=1`：成功时也保留 work、cache 和日志。
- `TEST_TIMING=1`：输出 compile/emit、link、execute、case total 和 suite wall time。
- `TEST_RELEASE_RUNS=1`：在普通 `run` 后额外验证 release executable 路径。

默认 fail-fast 会在发现首个失败后停止派发新 case，并等待已经启动的进程收敛。并行执行时，
先完成的 worker 不直接写最终输出，因此完成顺序不会改变结果顺序。`TEST_KEEP_GOING=1`
用于一次收集完整失败集合，不改变单个 case 的判定。

失败或 `TEST_KEEP_WORK=1` 时，runner 会打印本次运行的 artifact 路径：

```text
build/test/run.<随机后缀>/
  compiler-tests
  compiler-debug-build.out
  cases/<序号>-<类别>-<用例键>/
    cache/
    compiler.out
    emit.out
    link.out
    run.out
```

不同类别只生成实际需要的日志。超时 case 同样保留已经写入的完整日志。诊断用例在源文件中用
`// expected: <文本>` 指定必须出现的诊断片段；runner 的汇总输出只报告结果，原始诊断保存在
case 日志中。

## 性能基线

比较 runner 调度策略时必须使用同一编译器、profile、用例选择和 cache 策略。先串行测量，
再只改变 `TEST_JOBS`：

```bash
TEST_ROOT=test/compiler \
TEST_LIST=test/profile/compiler-smoke.txt \
TEST_JOBS=1 TEST_TIMING=1 \
JIANGC=./build/bin/jiangc bash ./script/test.sh

TEST_ROOT=test/compiler \
TEST_LIST=test/profile/compiler-smoke.txt \
TEST_JOBS=4 TEST_TIMING=1 \
JIANGC=./build/bin/jiangc bash ./script/test.sh
```

记录 compiler version、profile、目标平台、case 数、`jobs` 和 suite wall time。runner 的
阶段时间使用整秒墙钟时间，适合发现调度和外部工具瓶颈，不替代编译器内部 profiler。

2026-07-27 的 P1 基线使用 `jiang 0.5.0`、LLVM 22.1.8、arm64 Darwin，并为每个 case
使用独立冷 cache：

| 选择范围 | case 数 | `TEST_JOBS=1` | `TEST_JOBS=4` |
| --- | ---: | ---: | ---: |
| `test/profile/compiler-smoke.txt` | 17 | 341s | 105s |
| `TEST_FILTER='^test/lang/literal/'` | 21 | 8s | 2s |

compiler profile 的 4-worker wall time 是串行的约 31%，语言 literal 组是 25%。两组串行和
并行运行均全部通过，最终结果保持相同的发现顺序。

2026-07-28 将 67 个 compiler `run` case 改为单一聚合 executable 后，同一 arm64 Darwin
环境使用 `jiang 0.5.0`、LLVM 22.1.8 和 `TEST_JOBS=4` 的完整 compiler suite 数据为：

| 阶段 | 耗时 |
| --- | ---: |
| 聚合 executable 冷构建 | 122s |
| 67 个 case 全部完成（包含构建） | 143s |

旧 runner 仅 17 项 compiler smoke 的 4-worker 基线已经需要 105s，后续同源码测量曾达到
160s；逐文件完整套件还会继续重复编译。聚合后完整 67 项只冷构建一次；剩余主要运行期成本
来自 `driver/pipeline` 和 `sema/model_type_check`，不再来自重复编译编译器源码。

修改 runner 后先运行其独立自测：

```bash
bash ./script/test_runner_self_test.sh
```

自测覆盖串行/并行稳定输出、所有 case 类别、诊断匹配、fail-fast、keep-going、超时和
work/cache 隔离，不编译 Jiang 标准测试集。
