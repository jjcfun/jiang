# 标准库孵化文档

顶层 `std/` 是 Jiang 当前的标准库孵化 package。它还不是完整标准库，主要用于把已经稳定下来的
系统能力、内建类型别名和基础容器先放到统一入口下，让编译器源码和语言测试以接近最终用户的方式使用。

普通代码优先导入入口 package：

```jiang
import std;
```

不要直接导入标准库内部文件。内部文件路径和模块划分仍可调整，稳定入口是 `std` 及其公开
namespace。

## 当前入口

`std/std.jiang` 作为入口文件，负责 re-export 当前对外可见的标准库表面：

- `fs`：文件读写、metadata、目录创建、复制、替换和删除。
- `io`：标准输入输出能力。
- `process`：进程参数、环境变量、可执行文件查找和子进程执行。
- `panic(message)`：向标准错误输出消息与换行后立即 abort；它不执行 unwind，也不保证运行析构。
- `debug`：调试输出和主动 trap。
- `collection`：`Vector<T>`、`HashMap<K, V>` 和 `HashSet<T>`。常用的 `Vector<T>` 也可以直接写成
  `std.Vector<T>`。
- `Vector<T>`：可增长连续缓冲区，支持 `append`、`slice()`、`ptr()` 和 `into_slice()`。
  `Vector<T>` 满足 `Contiguous`，其中 `Element == T`；`length()` 表示已初始化元素数量，
  不包含 `capacity()`。`capacity()` 只表示 `Vector` 自己管理的 spare capacity，
  不属于 `Contiguous` 语义。`append()`、`insert()` 和下标赋值会消耗传入元素；下标赋值会先析构
  旧值。`remove()`、`swap_remove()`、`remove_last()` 和 `pop()` 把被移除元素的所有权交给调用者，
  调用者忽略返回值时仍会在语句结束处析构。扩容只移动既有元素，不析构它们；`truncate()`、
  `clear()` 和 `Vector` 自身析构只处理仍在已初始化区间内的元素。`into_slice()` 消耗 `Vector`，
  以 O(1) 转移 storage 和全部元素的所有权；原 `Vector` 不再析构这些元素。内部 `length` / `capacity`
  字段不是公开接口。
- `HashMap<K, V>`：无序 key-value collection。key 必须满足 `Hashable`、`Equatable` 和 `Copyable`；
  value 可以是 move-only 类型。`insert()` 和 `remove()` 返回的 optional 拥有其中旧 value 的所有权，
  `get()` / `get_mut()` 返回与 map 绑定的借用。查询、插入和删除的平均复杂度为 O(1)，迭代顺序不稳定。
  `load_percent` 可在构造时设置为 1-99 的整数，默认为 `80`。
- `HashSet<T>`：无序且不保存重复元素的 collection，元素约束、顺序规则和 `load_percent`
  与 `HashMap` 一致。
- `String`：拥有所有权的 UTF-8 字符串。字面量使用 `String text = "hello";` 初始化；动态字节使用
  `String.from_utf8(bytes)` 检查并返回 `String@Utf8Error`，确定输入合法时可在 `unsafe` 中调用
  `String.from_utf8_unchecked(bytes)`。`bytes()` 返回借用字节视图。
- `StringBuilder`：始终保持合法 UTF-8，支持追加字符串、字符串字面量、整数和浮点值；字面量兼容的
  `append(bytes)` 会验证输入，失败时 panic。动态字节需要恢复错误时使用
  `append_utf8(bytes)` 检查并返回 `Void@Utf8Error`；确定输入合法时可在 `unsafe` 中调用
  `append_utf8_unchecked(bytes)`。失败的 checked append 不修改 builder，错误 offset 相对于输入切片。
  `into_string()` 和 `into_slice()` 都会消耗 builder 并以 O(1) 转移 storage。
- `Path` / `PathBuilder`：面向原始路径字节的 owned path 和 builder，不借用 `StringBuilder` 的 UTF-8
  invariant。`Path.bytes()` 返回借用视图，
  `Path.into_slice()` 可转成拥有所有权的字节切片。路径算法仍保留在 `std.path` namespace 下。
- 内建 primitive type 与 trait 的公开入口。optional、errorable、pointer、reference、array 和 slice
  只通过 `T?`、`T@E`、`T^`、`T&`、`T*`、`T[N]`、`T[]`、`T[:S]` 等表面语法表达，
  compiler-owned constructor 名称不从 `std` re-export。
- `jiang`：Jiang 语言自身的词法和 syntax 辅助 API。当前包括 `std.jiang.syntax.*`、
  `std.jiang.Token`、`std.jiang.Tokenizer` 和 `std.jiang.ident`。
  这些 API 供 compiler 和 lang provider 共享，避免 DSL 从零实现 Jiang-compatible token 和
  syntax tree。

基础 collection 可以这样使用：

```jiang
import std;

std.Vector<Int> values! = std.Vector<Int>();
values.append(1);

std.collection.HashMap<Int, String> names! = std.collection.HashMap<Int, String>();
String one = "one";
names.insert(1, one);

std.collection.HashMap<Int, String> sparse_names! =
    std.collection.HashMap<Int, String>(load_percent: 50);

std.collection.HashSet<Int> seen! = std.collection.HashSet<Int>();
seen.insert(1);
```

## std.jiang

`std.jiang.syntax` 是 lang provider 的公共 syntax ABI。provider 通过
`std.jiang.syntax.Builder.Any&!` 构造 `NodeId` / `Tree`，并用 `std.jiang.syntax.Diagnostic`
报告 syntax 阶段错误。compiler 可以复用这些结构，再在 lang expansion 后转换到内部 AST。
`std.jiang.syntax.Provider` 是 `type = lang` package root `Lang` 需要实现的 trait；compiler
为该类型生成 host dynamic library wrapper，普通用户代码不直接调用 wrapper 符号。
builtin provider 也复用同一套 syntax ABI。当前 inline asm 由编译器内建 provider 实现，
用户源码可写 `#asm { ... }`，需要稳定指向内建实现时可写 `#jiang.asm { ... }`。

`std.jiang.Tokenizer` 是 Jiang 语言 tokenizer 的公共版本。它接受 `std.jiang.syntax.Source`，
每次 `next(builder)` 返回一个 `Token`，并把 lexer 诊断写入传入的 builder。`Token` 不保存
text 或 compiler 内部 symbol id；调用方按 `Token.span` 从 `Source.bytes` 取回文本，并在自己的
symbol store 中 intern。

identifier 判定由 `std.jiang.ident` 提供。ASCII 路径直接判断字节；UTF-8 路径使用 Unicode
`XID_Start` / `XID_Continue`。压缩 XID 表由 `script/gen_unicode_xid.js` 生成到
`std/jiang/text/generated/xid.jiang`，当前以 global array 保存，依赖 JIL 对 global array
动态下标访问的支持。

## 稳定性边界

`std` 当前仍处于 0.x 孵化阶段。模块路径、类型命名和方法集合会随语言功能继续收敛；用户代码应尽量依赖
`import std;` 后的顶层导出，而不是内部文件路径。

0.5.3 采用一次性迁移策略：语义错误或容易误解的名称直接修正，不长期保留重复 alias。迁移成本较高时，
在本节记录旧名和替代写法，但旧名仍会删除。当前迁移项只有 `Path.text()` → `Path.bytes()`；`Path` 保存
native path bytes，不承诺 UTF-8，因而不能继续称为 text。`Vector` 的 `insert_at`、`remove_at`、
`swap_remove_at` 和 `pop_last` 也已在 0.5.3 开发期间分别收敛为 `insert`、`remove`、`swap_remove` 和
`pop`，不提供兼容 alias。

`std` 不代表 no-libc/freestanding 已经可用。当前稳定路径仍是 hosted target；inline asm 已作为
builtin provider 提供基础能力，但 freestanding runtime、target runtime object 和 Linux no-libc
静态 executable 仍在后续阶段继续设计。

## Public module hierarchy

用户只依赖下列入口；`std/*.jiang`、`std/collection/*.jiang` 和 `src/system/*` 都不是可导入的稳定路径。

| 调用入口 | 角色 | 0.5.3 状态 |
| --- | --- | --- |
| `std.String`、`std.StringBuilder`、`std.Path`、`std.PathBuilder` | 高频 owned value | 直接导出 |
| `std.Vector<T>` | 高频连续 collection | 直接导出，同时存在于 `std.collection` |
| `std.collection` | `Vector`、`HashMap`、`HashSet` | 稳定 namespace |
| `std.fs`、`std.io`、`std.process` | hosted filesystem、stream 和 process | provisional error model |
| `std.debug`、`std.panic` | debug output、trap 和不可恢复终止 | 稳定 namespace / 顶层函数 |
| `std.jiang` | lang provider 使用的 Jiang syntax ABI | 稳定 namespace |
| `std.build` | build target 查询 | provisional namespace |
| `std.path` | 无 owner 的 path byte algorithms | 稳定 namespace |

`std` 还直接导出 `Utf8Error`、`Formattable`、`Atomic<T>`、`AtomicValue`、`MemoryOrder`、`Mutex<T>`，
以及 `Integer`、`SignedInteger`、`UnsignedInteger`、`FloatingPoint`。语言 builtin 的 `Bool`、整数、浮点、
`Char`、`Fn`、`FnOnce`、`Movable`、`Mutable`、`Equatable`、`Hashable`、`Iterator`、`Sequence` 和
`Contiguous` 也由入口 re-export；optional、errorable、owner、reference、pointer、array 和 slice 只使用
语言表面语法，不导出 compiler-owned constructor 名。

## 0.5.3 API snapshot

以下表格是当前 public surface 的人工可读 snapshot。`test/lang/package/check/std_surface_*` 以跨 package
interface fixture 固定其中的类型名、generic 参数和 lifetime contract；修改表面时必须同步文档、fixture
和 migration note。

### Text and path

| 类型 | public signature | 所有权、错误与复杂度 |
| --- | --- | --- |
| `String` | `from_utf8(UInt8[]&) -> String@Utf8Error` | 验证并复制，O(n)；精确 byte offset |
|  | `unsafe from_utf8_unchecked(UInt8[]&) -> String` | 调用者证明 UTF-8，复制 O(n) |
|  | `bytes() -> UInt8[]&` | 借用 receiver，O(1) |
|  | `chars() / make_iterator() -> StringChars` | 借用 receiver，O(1) 创建 iterator |
|  | `char_count() -> Int` | Unicode scalar count，O(n) |
|  | `byte_length() -> Int` | O(1) |
| `StringBuilder` | `append(String&)`、`append(Bool)`、整数/浮点 overload | 修改 receiver；数值格式化 O(输出长度) |
|  | `append(UInt8[]&)` | 验证后追加，O(n)；非法 UTF-8 panic |
|  | `append_utf8(UInt8[]&) -> Void@Utf8Error` | 失败不修改 receiver，O(n) |
|  | `unsafe append_utf8_unchecked(UInt8[]&)` | 调用者证明 UTF-8，O(n) append |
|  | `slice() -> UInt8[]&` | 借用 receiver，O(1) |
|  | `into_string() -> String`、`into_slice() -> UInt8[]^` | 消耗 receiver，O(1) storage transfer |
| `Path` | `Path(UInt8[]&)` | 复制 native path bytes，O(n) |
|  | `bytes() -> UInt8[]&`、`dirname() -> UInt8[]&` | 借用 receiver；分别 O(1) / O(n) |
|  | `is_absolute() -> Bool` | O(1) |
|  | `into_slice() -> UInt8[]^` | 消耗 receiver，O(1) transfer |
| `PathBuilder` | `PathBuilder()`、`PathBuilder(UInt8[]&)`、`append(UInt8[]&)` | owned byte builder；append 均摊 O(n) |
|  | `finish() -> Path` | 消耗 builder；当前复制 O(n) |
| `std.path` | `is_absolute`、`equal`、`dirname`、`join`、`has_extension`、`with_extension`、`normalize` | 输入/返回均为 native path bytes；产生 `Path` 的操作 O(n) |

### Collections

| 类型 | public signature family | 所有权与复杂度 |
| --- | --- | --- |
| `Vector<T>` | `Vector(capacity = 0)`、`length`、`capacity`、`is_empty`、`first`、`last` | 查询 O(1)；`first` / `last` 返回 Copyable `T?` |
|  | `append(T)`、`reserve(additional)`、`remove_last()`、`pop()` | append 均摊 O(1)，其余 O(1)；value 发生 transfer |
|  | `insert(index, T)`、`remove(index)`、`truncate(len)`、`clear()` | 保序操作 O(n)，精确 drop 已初始化元素 |
|  | `swap_remove(index)` | 不保序 O(1)，返回被移除 value 的所有权 |
|  | `slice() -> T[]&`、`ptr() -> T*`、`into_slice() -> T[]^` | view O(1)；`into_slice` 消耗并转移 storage |
| `HashMap<K,V>` | `HashMap(capacity = 0, load_percent = 80)` | `K: Hashable & Equatable & Copyable`；1-99% |
|  | `length`、`capacity`、`is_empty`、`contains(K)`、`get(K)`、`get_mut(K)` | 查询平均 O(1)；get 返回与 map 绑定的 borrow |
|  | `insert(K,V) -> V?`、`remove(K) -> V?`、`reserve`、`clear` | 平均 O(1)；optional 返回旧 value 的所有权 |
| `HashSet<K>` | 与 map 对应的 constructor、query、`insert(K) -> Bool`、`remove(K) -> Bool` | `K` 同 map 约束；平均 O(1) |

### Hosted system surface

| namespace | public signature | 当前 contract |
| --- | --- | --- |
| `std.fs` | `open/create(UInt8[]&) -> File?` | `File` 是 move-only owner，deinit 自动 close，显式 close 幂等 |
|  | `read_all(path)`、`read_at(path, offset, length) -> UInt8[]&?` | provider-owned borrowed bytes；provisional optional error |
|  | `write_all`、`exists`、`file_exists`、`dir_exists`、`create_dir(s)`、`copy_file`、`atomic_replace`、`remove_file` | Bool 表示成功；provisional error model |
|  | `file_size -> Int?`、`file_metadata -> FileMetadata?` | metadata 查询；provisional optional error |
| `std.io` | `stdout() / stderr() -> File` | 返回不拥有标准 fd 的 File handle |
| `std.process` | `arguments() -> ProgramArguments`、`env(name) -> UInt8[:0]&?`、`find_executable(name) -> UInt8[]^?` | env 借用 process storage；find 返回 owned path |
|  | `run(executable, String[]& arguments[, options]) -> ProcessResult` | 同步等待；stdout/stderr borrow provider result storage |
|  | `StreamBehavior`、`RunOptions`、`ProcessResult` | `ProcessResult.ok()` 为 started 且 exit code 0 |

`RunOptions.stdout = .pipe` 在 hosted POSIX target 上捕获标准输出。`stderr = .pipe` 尚未实现；调用方当前应选择
`.inherit` 或 `.discard`。

### Synchronization, diagnostics and termination

| API | public signature family | contract |
| --- | --- | --- |
| `Atomic<T>` | `get`、`set`、`get_and_set`、`compare_and_set`，均有默认 sequential 和显式 order overload | `T: AtomicValue & Copyable`；操作 O(1)，非法 order 组合 assert |
| `Mutex<T>` | `Mutex(T)`、`with_lock(FnOnce<R, T&!>) -> R` | `!Movable`；借用只存在于同步 callback；`T: Sendable` 时可跨 Domain |
| `std.debug` | `write`、`write_line -> Bool`、`trap() -> Void` | hosted stderr；trap 不返回 |
| `std.panic` | `panic(UInt8[]&) -> Void` | stderr 输出后 abort，不 unwind、不保证 drop |

## Naming review

0.5.3 使用下列命名规则。表中的反例不进入 compatibility surface：

| 类别 | 推荐调用 | 不采用 | 原因 |
| --- | --- | --- | --- |
| 基本形式 | `map.reserve(additional: 8)` | `map.reserve(map_capacity: 8)` | 参数描述角色，不重复 receiver 类型 |
| 修改操作 | `values.reverse()` | `values.reversed()` | 祈使动词表示修改 receiver |
| 非修改操作 | `values.sorted()` | `values.sort()` | 结果式名称表示返回新 value |
| 名词 operation | `set.union(other)` / `set.form_union(other)` | 两者都叫 `union` | `form_` 明确原地修改版本 |
| Bool query | `path.is_absolute()` | `path.absolute()` | 调用点可以直接作为断言阅读 |
| 工厂 | `sequence.make_iterator()` | `sequence.create_iterator()` | `make_` 是 std 的统一工厂词汇 |
| borrowed view | `text.bytes()` | `text.to_bytes()` | `bytes` 不暗示复制或 transfer |
| consuming conversion | `builder.into_string()` | `builder.to_string()` | `into_` 明确 receiver 被消耗 |
| collection position | `values.remove(index)` | `values.remove_at(index)` | index 参数已经表达位置 |
| safe empty probe | `values.pop() -> T?` | `values.remove_last() -> T?` | `pop` 表达空集合分支 |
| trait | `Sequence`、`Equatable` | `ISequence`、`CanEqual` | 名词描述角色，`-able` 描述能力 |

调用点必须同时表达 operation 和所有权。推荐：

```jiang
values.insert(index, value);
T removed = values.remove(index);
T? maybe_last = values.pop();
UInt8[]& view = path.bytes();
String text = builder.into_string();
```

不采用下列形式：

```jiang
values.insert_at(index, value);     // `_at` 不增加信息
values.remove_value(index);         // 与按位置删除语义冲突
T removed = values.pop();           // `pop` 必须允许空集合
UInt8[]& view = path.text();         // `text` 错误暗示 native path bytes 是 UTF-8
String text = builder.to_string();   // 隐藏 receiver 被消耗
```

本轮调用点结论：`String`、`StringBuilder`、`Vector`、`fs`、`io` 和 `process` 的顶层名称保留；
`Path.text()` 改为 `Path.bytes()`；`PathBuilder.finish()` 保留，因为它终结 builder 并返回领域值，而不是
通用 storage conversion。String 搜索、split、collection iterator/entry 和统一 system error type 属于后续
补充，不在 snapshot 中预留空 alias。
