# Language Test Coverage Matrix

这个文件记录 `test/lang` 的覆盖矩阵。它不是测试清单的替代品，而是用来确认每个语言分支是否有
对应的 `check` / `fail` / `run` / `emit` 用例。

## 覆盖状态

- **covered**：已有正例和关键反例。
- **partial**：已有用例，但还缺边界或交互场景。
- **missing**：还没有稳定用例。
- **deferred**：语言规则未定稿，暂不作为当前 release 验收前置。

## Grammar Coverage

| Grammar area | Feature dir | Status | Missing cases |
| --- | --- | --- | --- |
| literal tokens | `literal` | partial | float 边界、escape 解码语义、非法数字分隔符 |
| identifier tokens | `import` | covered | 普通 escaped identifier deferred；当前只允许 extern symbol name |
| trivia / recovery tokens | `token` | covered | block comment deferred |
| import / alias | `import`, `package` | covered | package registry/lockfile deferred |
| top-level global | `import`, `runtime` | partial | global destructure 正反例 |
| function declaration | `function` | covered | 更多 overload + named/default 参数交互 |
| parameter list | `function` | covered | 默认参数表达式覆盖更多 literal/constructor 场景 |
| generic params | `generic` | partial | nested generic decl、尾逗号、空参数列表反例 |
| where constraints | `generic` | partial | projected equality 多关联类型链、负 trait bound 组合 |
| lifetime annotation | `lifetime` | partial | return/self 组合、非法 ordering |
| type postfix | `type` | covered | pointer-to-pointer ABI 场景已有基础覆盖 |
| tuple / unit type | `type`, `aggregate` | partial | 嵌套 tuple type |
| struct | `nominal`, `aggregate` | covered | 默认构造、custom init、方法和字段可见性 |
| enum | `nominal`, `control_flow` | covered | underlying int 类型的更多边界值 |
| union | `nominal`, `control_flow` | covered | 多 payload 模式组合已有基础覆盖 |
| trait / extend | `generic` | partial | type function requirement、move receiver trait object deferred |
| block / tail expr | `function`, `control_flow` | covered | tail expr 与 defer/drop 组合 |
| var / destructure stmt | `function`, `aggregate` | partial | local/global destructure 尚未接入 HIR/type check/MIR |
| assignment stmt | `control_flow`, `type` | covered | compound assignment 与 overload deferred |
| call stmt | `function` | covered | implicit call statement 的更多反例 |
| extern declaration | `import` | covered | `@link_name` 不支持；ABI 非普通名字通过 extern escaped identifier |
| if / switch | `control_flow` | covered | switch exhaustiveness deferred |
| guard | `control_flow` | covered | guard pattern deferred |
| while / for | `control_flow` | partial | nested break/continue cleanup run 用例 |
| try / catch / throw | `error_handling` | covered | nested catch 与 generic error type |
| postfix call / member / index | `function`, `type`, `aggregate` | covered | optional chain runtime 行为更多 run 用例 |
| implicit layer call | `type`, `ownership` | partial | `size/align/max_align` run/emit、非法 type arg 组合 |
| struct expression | `aggregate`, `nominal` | covered | default field initializer 语义 deferred |
| array expression | `aggregate` | covered | array literal 对 tuple/union 元素 expected type |
| pattern | `control_flow`, `nominal` | partial | payload ignore、mutable payload binding、or-pattern 更多反例 |

## Semantic Coverage

| Semantic area | Feature dir | Status | Missing cases |
| --- | --- | --- | --- |
| expected type literal conversion | `literal`, `aggregate` | partial | float/int 溢出边界、char 到非字符目标 |
| mutability layering | `type`, `ownership` | partial | tuple/array/union 内层可变性组合 |
| pointer/reference operations | `type`, `ownership` | covered | raw pointer arithmetic deferred |
| ownership move/copy | `ownership` | covered | generic negative bound 更多组合 |
| drop/deinit/defer | `ownership` | partial | nested loop early-exit run 用例 |
| lifetime escape | `lifetime`, `ownership` | partial | stored field reference 多层嵌套 |
| overload resolution | `function` | partial | named args + overload + default args 的非歧义正例更多覆盖 |
| constructor resolution | `function`, `nominal` | covered | generic constructor overload |
| trait conformance | `generic` | partial | associated type equality 与 parent trait 混合 |
| trait object companion | `generic`, `package` | covered | owned receiver / move receiver trait object deferred |
| monomorphization | `generic`, `package` | partial | 跨 package public generic body run 用例已覆盖，更多 nested instance 待补 |
| package public surface | `package`, `import` | covered | public alias chain 已覆盖 |
| layout-sensitive aggregate | `aggregate`, `nominal` | partial | emit/run 覆盖更多 tuple/array/union 嵌套 |
| MIR control flow | `control_flow`, `error_handling` | partial | loop cleanup、nested try/catch run 用例 |
| backend runtime entry | `runtime` | covered | argv/env deferred |
| sentinel const | `generic`, `runtime`, `literal` | covered | `Bool` / `Char` / enum / struct sentinel 和 move-only 反例已覆盖 |
| sentinel string | `runtime`, `literal` | covered | `UInt8[:0]` / `UInt8*` 基础转换已覆盖 |

## 补测优先级

1. `lifetime`：补 return/self 组合、字段引用嵌套和非法 ordering，因为后续 LSP/诊断会依赖这些错误边界。
2. `control_flow` + `ownership`：补 nested loop、early return、defer/drop cleanup 的 run 用例。
3. `generic`：补 nested monomorph、associated type equality 和 parent trait 组合。
4. `aggregate`：补 tuple/array/union 的嵌套 expected type 和 backend run/emit。
5. `literal`：补 float、escape、溢出和非法 numeric token 的 fail 用例。
6. `destructure`：parser 已有 local/global destructure，后续需要补 HIR/type check/MIR 后再加用例。

任何新增语言规则进入实现前，先在本矩阵中定位到 feature dir 和最小测试层级。
