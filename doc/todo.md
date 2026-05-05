# JIR reference lowering and codegen lookup cleanup

Goal: make JIR a temporary, arena-backed IR whose cross-entity dependencies are
stable borrow references instead of AST/HIR/source lookups. JIR is not a cache
format; future incremental compilation should introduce stable ids or side
tables at the cache boundary.

Plan:

- [x] Add `ArenaRefList<T>` for arena-allocated entities with stable addresses.
- [x] Store `JirModule` decls, stmts, and exprs as stable arena entities while
  preserving id-based accessors during the transition.
- [x] Resolve call targets into a JIR side table after all graph modules are
  lowered, including local, imported, instance method, and static method calls.
- [x] Make codegen consume resolved call target refs without scan-based
  function lookup fallback.
- [x] Introduce `CodegenContext` for the active compiler context, JIR module,
  LLVM context/module/builder, and context-owned `TypeTable&`.
- [x] Move field, struct init, variant, and enum-member semantic information
  into JIR so codegen no longer has to recover it from names, AST spans, or
  source text.
  - [x] Enum member explicit integer values.
  - [x] Field indexes and optional-field result metadata.
  - [x] Struct init target and field mapping.
  - [x] Field/variant declaration `type_id` for layout and field init.
  - [x] Union/enum variant indexes and payload metadata.
  - [x] Static integer indexes for tuple and optional-array access.
  - [x] Literal values and literal pattern tests carry lowered JIR payloads
    instead of requiring codegen to parse source text.
- [x] Remove codegen fallback lookup helpers for function/global call targets.
- [x] Keep arena ownership in the compile/codegen session; JIR internals use
  `&` and reference lists, not owned arena pointers.
- [x] Continue shrinking codegen signatures so expression/statement emission
  takes `CodegenContext&` instead of threading `ctx`, `TypeTable&`,
  `JirModule&`, LLVM context/module, and builder separately.
  - [x] Function/init/module emission.
  - [x] Statement/block emission and condition/pattern helpers.
  - [x] Expression/address emission.
  - [x] Constant-expression emission.
- [x] Replace remaining codegen `ir.AstType`/source-text literal paths with
  lowered JIR/type information where practical.
  - [x] Type-value/sizeof/alignof/alloc_array use lowered `JirTypeExpr`
    `TypeId` instead of codegen-local AST type lowering.
  - [x] Literal codegen consumes `JirLiteral`; codegen-local source text literal
    parsing helpers were removed.
  - [x] Cast codegen consumes lowered target `TypeId` instead of inspecting
    `ir.AstType`.
  - [x] JIR field/variant/type/extend/catch nodes no longer store `ir.AstType`;
    `JirModule` no longer stores source text.

Tests:

- `script/test_compiler.sh type_minimal.jiang`
- `script/test_compiler.sh lower_jir_minimal.jiang llvm_api_minimal.jiang compiler_bootstrap_smoke.jiang`
- `script/stage1_smoke.sh`
- Targeted samples covering mutable generics, raw pointers, methods, imports,
  structs, enums, and unions.
