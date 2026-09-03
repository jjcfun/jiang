# 标准库孵化文档

`src/std/` 是 Jiang 当前的标准库孵化 package。它还不是完整标准库，主要用于把已经稳定下来的
系统能力、内建类型别名和基础容器先放到统一入口下，让编译器源码和语言测试以接近最终用户的方式使用。

普通代码优先导入入口 package：

```jiang
import std;
```

不要直接导入标准库内部文件。内部文件路径和模块划分仍可调整，稳定入口是 `std` 及其公开
namespace。

## 当前入口

`src/std/std.jiang` 作为入口文件，负责 re-export 当前对外可见的标准库表面：

- `fs`：文件读写、metadata、目录创建、复制、替换和删除。
- `io`：标准输入输出能力。
- `process`：进程参数、环境变量、可执行文件查找和子进程执行。
- `time`：checked monotonic/wall clock 与不可为负的 `Duration`。
- `random`：操作系统 entropy 与可复现实验用 `Generator`；后者不能用于密码学场景。
- `Atomic<T>`、`MemoryOrder`、`Mutex<T>`：直接导出 core 同步原语，不建立 std wrapper。
- `panic(message)`：向标准错误输出消息与换行后立即 abort；它不执行 unwind，也不保证运行析构。
- `debug`：调试输出和主动 trap。
- `collection`：`Vector<T>`、`HashMap<K, V>` 和 `HashSet<T>`。常用的 `Vector<T>` 也可以直接写成
  `std.Vector<T>`。
- `Vector<T>`：可增长连续缓冲区，支持 `append`、`slice()`、`ptr()` 和 `into_slice()`。
  `Vector<T>` 同时满足 `Collection` 和 `Contiguous`；前者通过 `Sequence` 产生 `T&`，后者的
  `Element == T`。`length()` 表示已初始化元素数量，
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
  `load_percent` 可在构造时设置为 1-99 的整数，默认为 `80`。有系统 entropy 时，每个获得 storage 的
  map 使用独立随机 hash seed；不支持 entropy 的 target 仍可使用 HashMap，但不承诺抵抗恶意碰撞输入。
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
  `Path.into_slice()` 可转成拥有所有权的字节切片。`PathBuilder.finish()` 消耗 builder 并以 O(1)
  转移 storage。0.5.3 的 lexical path 语义面向 macOS/Linux POSIX separator；不访问 filesystem、
  不解析 symlink，也不把 native bytes 当作 UTF-8。路径算法保留在 `std.path` namespace 下。
- 内建 primitive type 与 trait 的公开入口。optional、errorable、pointer、reference、array 和 slice
  只通过 `T?`、`T@E`、`T^`、`T&`、`T*`、`T[N]`、`T[]`、`T[:S]` 等表面语法表达，
  compiler-owned constructor 名称不从 `std` re-export。
- `jiang`：Jiang 语言自身的词法和 syntax 辅助 API。当前包括 `std.jiang.syntax.*`、
  `std.jiang.Token`、`std.jiang.TokenKind` 和 `std.jiang.ident`。通用 tokenizer/parser 通过
  `std.jiang.syntax.Tokenizer<K>` / `Parser<K>` 使用，避免 DSL 从零实现 cursor、诊断和 Jiang syntax。

基础 collection 可以这样使用：

```jiang
import std;

std.Vector<Int> values! = std.Vector<Int>();
values.append(1);
Int needle = 1;
Bool has_one = values.contains(needle$.ref());

std.collection.HashMap<Int, String> names! = std.collection.HashMap<Int, String>();
String one = "one";
names.insert(1, one);

std.collection.HashMap<Int, String> sparse_names! =
    std.collection.HashMap<Int, String>(load_percent: 50);

std.collection.HashSet<Int> seen! = std.collection.HashSet<Int>();
seen.insert(1);
```

## 同步与异步边界

`Atomic<T>` 是一个 move-only 原子 identity，只支持 `AtomicValue & Copyable` 的 lock-free 标量。
默认 overload 使用 sequential order；显式 overload 会在运行时拒绝不适用于该 operation 的
`MemoryOrder`。所有操作都是 O(1)，同一 Atomic 可以从多个线程并发访问：

```jiang
Atomic<Int> state = Atomic<Int>(0);
state.set(1, .release);
Int observed = state.get(.acquire);
Bool changed = state.compare_and_set(
    1, 2, MemoryOrder.acquire_release, MemoryOrder.acquire
);
```

`Mutex<T>` 消耗并拥有受保护值，且本体是 `!Movable`。需要转移共享 handle 时使用 `Mutex<T>^`；
`with_lock` 只在同步 callback 内提供唯一 `T&!`，callback 返回或错误 cleanup 时自动解锁，返回值不能
携带从该引用派生的借用：

```jiang
Mutex<Int>^ count = new Mutex<Int>(0);
Int next = count.with_lock { value =>
    value$.set(value$.get() + 1);
    value$.get()
};
```

`Domain`、`Task`、`coroutine.sync` 和 `coroutine.suspend` 是语言 core 能力，std 不提供 wrapper、alias
family 或第二套 scheduler/cancellation API。使用方式见[语言指南的异步函数章节](jiang.md#异步函数async)。

## std.jiang

`std.jiang.syntax` 是 lang provider 的公共 syntax API。核心类型是 `Input`、opaque
`SyntaxContext`、`Token<K>`、`Tokenizer<K>`、`Parser<K>`、typed syntax handle 和 opaque `Ast`。
provider 通过 `Parser<K>` 的 typed method 生成 Jiang syntax，不公开 AST data、node index、child
range 或 arena，也不允许用户手工组装 compiler AST。

`std.jiang.syntax.Provider` 是 `type = lang` package root `Lang` 需要实现的 trait。采用 Jiang 默认
词法规则时只需实现 `parse`；默认 `scan` 会把连续 token storage 交给 `default_parser`。自定义词法
规则可以使用 `Tokenizer<CustomKind>`，由 provider 自己保存并解释 custom kind。

`Token<K>` 不复制 token text；调用方通过 `Token.span` 从 `Source.bytes` 读取。`Tokenizer<K>` 管理
source cursor、trivia、连续 storage 和 `checkpoint/rewind`，但不理解 `K`。诊断由
`Tokenizer.error*`、`Parser.expect*` 和 `Parser.error` 发出，message 通常可以省略。

compiler 为 provider 生成 host dynamic library wrapper，普通用户代码不直接调用 wrapper 符号。
当前 inline asm 由编译器内建 provider 实现；用户源码可写 `#asm { ... }`，需要稳定指向内建实现时
可写 `#jiang.asm { ... }`。API 文档也由 compiler builtin provider 实现，使用 `#doc` 或
`#jiang.doc`；它不扩大 `std.jiang.syntax.Provider` 的 public invocation grammar。

identifier 判定由 `std.jiang.ident` 提供。ASCII 路径直接判断字节；UTF-8 路径使用 Unicode
`XID_Start` / `XID_Continue`。压缩 XID 表由 `script/gen_unicode_xid.js` 生成到
`src/std/jiang/text/generated/xid.jiang`，当前以 global array 保存，依赖 JIL 对 global array
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

用户只依赖下列入口；`src/std/*.jiang`、`src/std/collection/*.jiang` 和
`src/compiler/system/*` 都不是可导入的稳定路径。

| 调用入口 | 角色 | 0.5.3 状态 |
| --- | --- | --- |
| `std.String`、`std.StringBuilder`、`std.Path`、`std.PathBuilder` | 高频 owned value | 直接导出 |
| `std.Vector<T>` | 高频连续 collection | 直接导出，同时存在于 `std.collection` |
| `std.collection` | `Vector`、`HashMap`、`HashSet` | 稳定 namespace |
| `std.fs`、`std.io`、`std.process` | hosted filesystem、stream 和 process | portable `FileError` / stable surface |
| `std.time` | monotonic/wall clock 与 `Duration` | checked hosted clock namespace |
| `std.random` | OS entropy 与可复现实验用 generator | hosted entropy / portable generator |
| `std.debug`、`std.panic` | debug output、trap 和不可恢复终止 | 稳定 namespace / 顶层函数 |
| `std.jiang` | lang provider 使用的 Jiang syntax ABI | 稳定 namespace |
| `std.build` | build target 与 debug/release mode 查询 | stable compile-time namespace |
| `std.path` | 无 owner 的 path byte algorithms | 稳定 namespace |

`std` 还直接导出 `Utf8Error`、`Duration`、`Instant`、`SystemTime`、`Formattable`、`Atomic<T>`、
`AtomicValue`、`MemoryOrder`、`Mutex<T>`，
以及 `Integer`、`SignedInteger`、`UnsignedInteger`、`FloatingPoint`。语言 builtin 的 `Bool`、整数、浮点、
`Char`、`Fn`、`FnOnce`、`Movable`、`Mutable`、`Equatable`、`Hashable`、`Iterator`、`Sequence`、
`Collection` 和 `Contiguous` 也由入口 re-export；optional、errorable、owner、reference、pointer、array 和 slice 只使用
语言表面语法，不导出 compiler-owned constructor 名。

### 命名与调用点审查

下列真实调用点覆盖本轮冻结的 public 名称。名称修改必须同时更新对应调用点，不能只让 declaration
看起来简短：

| API family | 调用点证据 |
| --- | --- |
| `String` 构造、view、搜索、split、`StringBuilder` append/transfer | `test/lang/runtime/run/string_utf8_construction.jiang`、`std_string_search.jiang`、`std_string_builder*.jiang` |
| `Vector` 查询、增长、删除、算法、iterator、storage transfer | `vector_collection_methods.jiang`、`vector_algorithms.jiang`、`vector_into_slice.jiang` |
| `HashMap` / `HashSet` lookup、insert、remove、iteration、clear | `hash_collections.jiang`、`hash_map_iterator_local_escape.jiang` |
| `Path` / `PathBuilder` lexical operation 与 consuming finish | `std_path.jiang`、`std_path_invalid_extension.jiang` |
| `std.fs` / `std.io` / `std.process` | `std_hosted_system.jiang`、`std_fs_errors.jiang`、`example/std/files.jiang`、`list_files.jiang` |
| `std.time` / `std.random` | `std_time.jiang`、`std_time_duration_overflow.jiang`、`std_random.jiang` |
| `Atomic` / `Mutex` / `Sendable` | `atomic_scalar_orders.jiang`、`mutex_payload_drop.jiang`、`sendable_*.jiang` |
| `assert` / `std.debug` / `panic` / `process.exit` / `std.build` | `std_assert_message.jiang`、`std_debug_trap.jiang`、`panic_abort.jiang`、`std_process_exit.jiang`、`std_build_mode.jiang` |

这些调用点同时固定 Jiang 的 snake_case、按位置 `remove(index)`、明确 `into_*` ownership transfer、
Bool 查询前缀和 errorable 返回约定。没有发现需要保留的领域惯例例外。

### 验证矩阵

| 维度 | 覆盖证据 |
| --- | --- |
| 正常、空值与边界 | empty collection/string/slice、零长度 entropy/read、目录空/非空、clock 差值和 numeric unit 边界均有 run case |
| 明确错误 | UTF-8 各错误 payload、FileError、非法 range/load percent/radix/precision/order、allocation overflow 与 unsupported target 均有 catch、fail 或 trap case |
| move、borrow 与 drop | Vector/Map/Set/String/Path/File 的 consuming、use-after-move、borrow escape、iterator mutation、active element drop 和 allocation balance 均有正负例 |
| generic substitution | Collection/Sequence/Contiguous associated type、Hashable/Equatable bound、move-only element、carried lifetime 与 cross-package `.ji` fixture 均已覆盖 |
| 并发边界 | Atomic order、Mutex callback escape、Sendable 正负矩阵、Continuation result 与 File 非 Sendable contract 均有 check/fail/run case |

无法可靠由单机运行触发的平台错误使用 source selection 或 provider-specific 边界测试，不通过伪造
public provider 状态制造测试入口。

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
|  | `byte_index_of(bytes[, start]) -> Int?`、`contains(bytes)` | byte offset；朴素搜索 O(n*m) |
|  | `starts_with(bytes)`、`ends_with(bytes)` | byte prefix/suffix，O(m) |
|  | `appending(bytes) -> String` | 保留 receiver，验证并复制 UTF-8 suffix，O(n+m) |
|  | `split(separator, omit_empty = true) -> Vector<String>` | eager copy；separator 必须是非空 UTF-8，O(n*m) |
|  | `Hashable` | 按完整 UTF-8 byte sequence 写入调用方 hasher，O(n) |
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
| `PathBuilder` | `PathBuilder()`、`PathBuilder(UInt8[]&)`、`append(UInt8[]&)` | owned byte builder；append 对 segment 为 amortized O(n)，absolute segment 替换当前内容 |
|  | `finish() -> Path` | 消耗 builder；O(1) storage transfer |
| `std.path` | `is_absolute`、`equal`、`dirname`、`join`、`has_extension`、`with_extension`、`normalize` | 输入/返回均为 native path bytes；产生 `Path` 的操作 O(n) |

`join(base, relative)` 在 relative 为空时保留 base，relative 为 absolute path 时替换 base。
`with_extension(path, extension)` 接受不带 leading dot 或 separator 的 extension；空 extension 删除原
extension。basename 的 leading dot 不算 extension。`normalize` 只折叠重复 separator、`.` 与 lexical
`..`；它不访问 filesystem，因此不会解析 symlink，也不能用于证明两个路径指向同一文件。

### Collections

`Sequence.Element` 始终是 iterator 实际产生的类型。`Collection` 继承 `Sequence`，增加有限、可重复
遍历以及 `length()` / `is_empty()`；`Contiguous` 是独立的连续 storage capability。Vector 同时满足
Collection 和 Contiguous，HashMap/HashSet 只满足 Collection，String 只满足 Sequence。

| 类型 | public signature family | 所有权与复杂度 |
| --- | --- | --- |
| `Vector<T>` | `Vector(capacity = 0)`、`length`、`capacity`、`is_empty`、`first`、`last` | 查询 O(1)；`first` / `last` 借用并返回 `(T value)&?` |
|  | `make_iterator() -> SliceIterator<T>` | 借用 Vector；逐项返回 `T&`，iterator 不能活过 Vector |
|  | `for_each`、`map`、`reduce`、`contains`、`contains_where` | 借用输入；线性 eager 算法，按元素顺序执行 |
|  | `filter(Self self!, predicate) -> Vector<T>` | 消耗输入；转移保留元素、析构其余元素，O(n) |
|  | `append(T)`、`reserve(additional)`、`remove_last()`、`pop()` | append 均摊 O(1)，其余 O(1)；value 发生 transfer |
|  | `insert(index, T)`、`remove(index)`、`truncate(len)`、`clear()` | 保序操作 O(n)，精确 drop 已初始化元素 |
|  | `swap_remove(index)` | 不保序 O(1)，返回被移除 value 的所有权 |
|  | `slice() -> T[]&`、`ptr() -> T*`、`into_slice() -> T[]^` | view O(1)；`into_slice` 消耗并转移 storage |
| `HashMap<K,V>` | `HashMap(capacity = 0, load_percent = 80)` | `K: Hashable & Equatable & Copyable`；1-99% |
|  | `length`、`capacity`、`is_empty`、`contains(K/K&)`、`get(K/K&)`、`get_mut(K/K&)` | 查询平均 O(1)；引用 overload 不复制 key，get 返回与 map 绑定的 borrow |
|  | `make_iterator() -> HashMapIterator<K,V>` | 借用 map；逐项返回 `(K&, V&)`，不分配，迭代顺序不稳定 |
|  | `insert(K,V) -> V?`、`get_or_insert(K,V) -> V&!`、`remove(K/K&) -> V?` | 平均 O(1)；insert/remove 转移旧 value，get_or_insert 返回 map borrow |
|  | `reserve`、`clear` | reserve 保证 additional 容量；clear 析构全部 active value |
| `HashSet<K>` | 与 map 对应的 constructor、query、`insert(K) -> Bool`、`remove(K/K&) -> Bool` | `K` 同 map 约束；引用 overload 不复制 key，平均 O(1) |
|  | `make_iterator() -> HashSetIterator<K>` | 借用 set；逐项返回 `K&`，不分配，迭代顺序不稳定 |

0.5.3 的 collection 算法均为 eager operation：`map` 和 `filter` 直接产生新的 `Vector`，不建立
lazy adapter graph。`filter` 为支持 move-only 元素而消耗 receiver；调用后原 Vector 已移动。
`for_each`、`map`、`reduce`、`contains` 和 `contains_where` 只借用 receiver；`contains` 要求元素实现
`Equatable`，两种查询都在首次匹配时提前结束。

`HashMap` 与 `HashSet` 的 iterator 直接借用现有 slot storage，不复制 key/value，也不建立快照。活跃
iterator 会阻止可能扩容或重排 slot 的修改；删除产生的 tombstone 会被跳过。表的探测与扩容会改变
遍历次序，实例 seed 也可能让同一组元素在不同 map 或不同运行中采用不同顺序，因此调用方不能依赖
HashMap/HashSet 的迭代顺序。

collection allocation failure 是不可恢复的进程级错误：负 capacity、capacity/byte size 溢出或底层
allocator 对非零大小返回 null 时立即 trap，不 unwind，也不保证析构。`reserve`、增长和插入因此不返回
OOM error；在 allocation 成功前不会替换原 storage，成功后才移动元素并释放旧 storage。

需要“存在则修改、不存在则插入”时使用 `get_or_insert(key, default_value)`。它只计算一次 key hash，
命中时析构未使用的 default value，未命中时把 key/value 所有权转入 map。0.5.3 不额外公开
`Entry` / `OccupiedEntry` / `VacantEntry` 过渡类型，避免为同一条探测与扩容管线建立第二套 API。

### Hosted system surface

| namespace | public signature | 当前 contract |
| --- | --- | --- |
| `std.fs` | `open/create(UInt8[]&) -> File@FileError` | `File` 是 move-only owner，deinit 自动 close，显式 close 幂等且可报告错误 |
|  | `File.read_all/read_at -> UInt8[]^@FileError` | 返回独立 owned bytes；调用方负责其 lifetime，读取复杂度为 O(n) |
|  | `read_all/read_at -> UInt8[]^@FileError` | 路径 convenience API；返回值不借用临时 File |
|  | `read_all_async(UInt8[]&) -> async UInt8[]^@FileError` | 在共享并发 IO Domain 执行 regular-file 读取，不阻塞调用者 Domain |
|  | `write_all`、`create_dir(s)`、`copy_file`、`atomic_replace`、`remove_file -> Void@FileError` | 修改操作明确传播错误，不使用 Bool 丢失原因 |
|  | `file_size -> Int@FileError`、`file_metadata -> FileMetadata@FileError` | metadata 查询明确传播错误 |
|  | `list_dir(UInt8[]&) -> Vector<Path>@FileError` | eager 返回完整子路径；不包含 `.` / `..`，顺序未指定，复杂度为 O(entries + path bytes) |
|  | `exists`、`file_exists`、`dir_exists -> Bool` | 仅用于存在性探测；不存在或不可访问均为 false |
| `std.io` | `stdout() / stderr() -> File` | 返回不拥有标准 fd 的 File handle |
| `std.process` | `arguments() -> ProgramArguments`、`env(name) -> UInt8[:0]&?`、`find_executable(name) -> UInt8[]^?` | env 借用 process storage；find 返回 owned path |
|  | `run(executable, String[]& arguments[, options]) -> ProcessResult` | 同步等待；stdout/stderr borrow provider result storage |
|  | `StreamBehavior`、`RunOptions`、`ProcessResult` | `ProcessResult.ok()` 为 started 且 exit code 0 |
|  | `exit(code) -> Void` | 立即终止当前进程；不 unwind，也不保证 Jiang value 析构 |
| `std.time` | `nanoseconds/microseconds/milliseconds/seconds(UInt64) -> Duration` | 非负 duration；unit overflow assert |
|  | `monotonic_now() -> Instant?` | 进程内 interval clock；不表示日期，provider 不支持或失败时为 null |
|  | `wall_now() -> SystemTime?` | Unix epoch wall clock；可能受系统校时影响，失败时为 null |
| `std.random` | `fill_entropy(UInt8[]&!) -> Bool` | OS entropy；失败不伪造数据，buffer 可能被部分修改，O(n) |
|  | `Generator(seed)`、`next_u64()`、`fill(UInt8[]&!)` | 可复现、非密码学用途；算法不属于稳定 contract |

`FileError` 使用可移植 case：`not_found`、`permission_denied`、`already_exists`、`not_a_directory`、
`is_a_directory`、`invalid_argument`、`too_many_open_files`、`no_space`、`read_only`、`closed`、
`unsupported` 和未能进一步分类的 `io`。它不公开 errno 或平台 handle。

`std.fs.File` 的 move 会转移唯一 handle；`close()` 会让 File 失效且可重复调用，关闭后的 read/write
返回 `.closed`。未显式关闭的 owned File 在 deinit 时执行同一条 close 路径。raw handle constructor
不进入 std public API；`stdout()` / `stderr()` 返回的非 owning handle 即使 close 也不会关闭进程的标准 fd。
0.5.3 的 `File` 不满足 `Sendable`：同一 handle 的共享 offset 与 close 并发 contract 尚未冻结，跨
Domain 应在单一 Domain 内完成 IO 后转移 owned bytes，而不是传递 File。

`read_all_async(path)` 会挂起调用者，并在 std 共享的并发 IO Domain 执行同步 regular-file provider
operation。取消是 cooperative 的：尚未开始的读取不会打开文件；已经进入阻塞 provider call 的读取会先
完成该调用，再关闭 File、释放未交付结果并完成取消。该 API 避免阻塞调用者 Domain，但不承诺
kernel readiness IO，也不适合作为完整网络 IO runtime。

`list_dir(path)` eager 构造 owned `Vector<Path>`；每个 `Path` 都独立拥有 native path bytes，结果不会
借用目录参数或打开的目录 handle。返回顺序不稳定，需要确定性输出时由调用方显式排序。

`RunOptions.stdout = .pipe` 在 hosted POSIX target 上捕获标准输出。`stderr = .pipe` 尚未实现；调用方当前应选择
`.inherit` 或 `.discard`。

### Synchronization, diagnostics and termination

| API | public signature family | contract |
| --- | --- | --- |
| `Atomic<T>` | `get`、`set`、`get_and_set`、`compare_and_set`，均有默认 sequential 和显式 order overload | move-only identity；`T: AtomicValue & Copyable`；操作 O(1)，非法 order 组合 assert |
| `Mutex<T>` | `Mutex(T)`、`with_lock(FnOnce<R, T&!>) -> R` | `!Movable` owner；借用只存在于同步 callback；`T: Sendable` 时可跨 Domain |
| `coroutine.Continuation<T>` | `resume(T)`、`on_cancel(Fn<Void>)` | `T: Sendable` 时可跨 Domain；result ownership 由恰好一次完成路径转移 |
| `std.debug` | `write`、`write_line -> Bool`、`trap() -> Void` | hosted stderr；trap 立即 abort，不 unwind |
| `std.panic` | `panic(UInt8[]&) -> Void` | stderr 输出后 abort，不 unwind、不保证 drop |

`assert(condition[, message])` 在 debug 和 release mode 都保留。失败输出包含 source path、byte offset
和可选 message，随后执行 LLVM trap；它不 unwind，也不保证析构。`std.build.mode` 是只读 compile-time
`BuildMode`，可以在 comptime branch 中查询 `.debug` 或 `.release`，不能由普通代码改变。

0.5.3 不公开 stack backtrace API：当前 hosted provider 没有稳定的 unwind 与 symbolization contract。
返回永远 unavailable 的占位 API 不能提供有效能力，后续应在至少 macOS/Linux 都能明确报告可用性后再加入。

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
