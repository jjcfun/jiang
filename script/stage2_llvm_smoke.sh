#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_DIR="$BUILD_DIR/stage2_llvm_smoke"
LLVM_CONFIG="${LLVM_CONFIG:-llvm-config}"
LLVM_BINDIR="$($LLVM_CONFIG --bindir)"
LLVM_CLANG="$LLVM_BINDIR/clang"
LLVM_LLI="$LLVM_BINDIR/lli"

mkdir -p "$OUT_DIR"
cd "$PROJECT_ROOT"

bash "$PROJECT_ROOT/script/build_stage2.sh"

MINIMAL_LL="$OUT_DIR/minimal.ll"
MINIMAL_O="$OUT_DIR/minimal.o"
WHILE_LL="$OUT_DIR/while_minimal.ll"
WHILE_O="$OUT_DIR/while_minimal.o"
BREAK_CONTINUE_LL="$OUT_DIR/break_continue_minimal.ll"
BREAK_CONTINUE_O="$OUT_DIR/break_continue_minimal.o"
FOR_RANGE_LL="$OUT_DIR/for_range_minimal.ll"
FOR_RANGE_O="$OUT_DIR/for_range_minimal.o"
FOR_INFER_RANGE_LL="$OUT_DIR/for_infer_range_minimal.ll"
FOR_INFER_RANGE_O="$OUT_DIR/for_infer_range_minimal.o"
FOR_ITEM_ARRAY_LL="$OUT_DIR/for_item_array_minimal.ll"
FOR_ITEM_ARRAY_O="$OUT_DIR/for_item_array_minimal.o"
FOR_INDEXED_LL="$OUT_DIR/for_indexed_minimal.ll"
FOR_INDEXED_O="$OUT_DIR/for_indexed_minimal.o"
FOR_INDEXED_TYPED_LL="$OUT_DIR/for_indexed_typed_minimal.ll"
FOR_INDEXED_TYPED_O="$OUT_DIR/for_indexed_typed_minimal.o"
FOR_INDEXED_TUPLE_BINDING_LL="$OUT_DIR/for_indexed_tuple_binding_minimal.ll"
FOR_INDEXED_TUPLE_BINDING_O="$OUT_DIR/for_indexed_tuple_binding_minimal.o"
FOR_MUTABLE_BINDING_LL="$OUT_DIR/for_mutable_binding_minimal.ll"
FOR_MUTABLE_BINDING_O="$OUT_DIR/for_mutable_binding_minimal.o"
FOR_INDEXED_MUTABLE_TUPLE_BINDING_LL="$OUT_DIR/for_indexed_mutable_tuple_binding_minimal.ll"
FOR_INDEXED_MUTABLE_TUPLE_BINDING_O="$OUT_DIR/for_indexed_mutable_tuple_binding_minimal.o"
FOR_TUPLE_BINDING_LL="$OUT_DIR/for_tuple_binding_minimal.ll"
FOR_TUPLE_BINDING_O="$OUT_DIR/for_tuple_binding_minimal.o"
FOR_TUPLE_BINDING_TYPED_LL="$OUT_DIR/for_tuple_binding_typed_minimal.ll"
FOR_TUPLE_BINDING_TYPED_O="$OUT_DIR/for_tuple_binding_typed_minimal.o"
SWITCH_ENUM_LL="$OUT_DIR/switch_enum_minimal.ll"
SWITCH_ENUM_O="$OUT_DIR/switch_enum_minimal.o"
ENUM_SWITCH_SHORTHAND_LL="$OUT_DIR/enum_switch_shorthand_minimal.ll"
ENUM_SWITCH_SHORTHAND_O="$OUT_DIR/enum_switch_shorthand_minimal.o"
STRUCT_ENUM_FIELD_SHORTHAND_LL="$OUT_DIR/struct_enum_field_shorthand_minimal.ll"
STRUCT_ENUM_FIELD_SHORTHAND_O="$OUT_DIR/struct_enum_field_shorthand_minimal.o"
ENUM_SHORTHAND_LL="$OUT_DIR/enum_shorthand_minimal.ll"
ENUM_SHORTHAND_O="$OUT_DIR/enum_shorthand_minimal.o"
ENUM_SHORTHAND_ARG_LL="$OUT_DIR/enum_shorthand_arg_minimal.ll"
ENUM_SHORTHAND_ARG_O="$OUT_DIR/enum_shorthand_arg_minimal.o"
UNION_LL="$OUT_DIR/union_minimal.ll"
UNION_O="$OUT_DIR/union_minimal.o"
UNION_SHORTHAND_LL="$OUT_DIR/union_shorthand_minimal.ll"
UNION_SHORTHAND_O="$OUT_DIR/union_shorthand_minimal.o"
UNION_BIND_LL="$OUT_DIR/union_bind_minimal.ll"
UNION_BIND_O="$OUT_DIR/union_bind_minimal.o"
UNION_TUPLE_BIND_LL="$OUT_DIR/union_tuple_bind_minimal.ll"
UNION_TUPLE_BIND_O="$OUT_DIR/union_tuple_bind_minimal.o"
UNION_IF_PATTERN_LL="$OUT_DIR/union_if_pattern_minimal.ll"
UNION_IF_PATTERN_O="$OUT_DIR/union_if_pattern_minimal.o"
UNION_IF_MUTABLE_BINDING_LL="$OUT_DIR/union_if_mutable_binding_minimal.ll"
UNION_IF_MUTABLE_BINDING_O="$OUT_DIR/union_if_mutable_binding_minimal.o"
UNION_IF_SHORTHAND_PATTERN_LL="$OUT_DIR/union_if_shorthand_pattern_minimal.ll"
UNION_IF_SHORTHAND_PATTERN_O="$OUT_DIR/union_if_shorthand_pattern_minimal.o"
UNION_TUPLE_IF_SHORTHAND_PATTERN_LL="$OUT_DIR/union_tuple_if_shorthand_pattern_minimal.ll"
UNION_TUPLE_IF_SHORTHAND_PATTERN_O="$OUT_DIR/union_tuple_if_shorthand_pattern_minimal.o"
UNION_TUPLE_IF_MUTABLE_SHORTHAND_PATTERN_LL="$OUT_DIR/union_tuple_if_mutable_shorthand_pattern_minimal.ll"
UNION_TUPLE_IF_MUTABLE_SHORTHAND_PATTERN_O="$OUT_DIR/union_tuple_if_mutable_shorthand_pattern_minimal.o"
UNION_SWITCH_SHORTHAND_PATTERN_LL="$OUT_DIR/union_switch_shorthand_pattern_minimal.ll"
UNION_SWITCH_SHORTHAND_PATTERN_O="$OUT_DIR/union_switch_shorthand_pattern_minimal.o"
UNION_SWITCH_MUTABLE_BINDING_LL="$OUT_DIR/union_switch_mutable_binding_minimal.ll"
UNION_SWITCH_MUTABLE_BINDING_O="$OUT_DIR/union_switch_mutable_binding_minimal.o"
UNION_TUPLE_SWITCH_MUTABLE_BINDING_LL="$OUT_DIR/union_tuple_switch_mutable_binding_minimal.ll"
UNION_TUPLE_SWITCH_MUTABLE_BINDING_O="$OUT_DIR/union_tuple_switch_mutable_binding_minimal.o"
STRUCT_UNION_FIELD_SHORTHAND_LL="$OUT_DIR/struct_union_field_shorthand_minimal.ll"
STRUCT_UNION_FIELD_SHORTHAND_O="$OUT_DIR/struct_union_field_shorthand_minimal.o"
UINT8_LL="$OUT_DIR/uint8_minimal.ll"
UINT8_O="$OUT_DIR/uint8_minimal.o"
UINT8_SLICE_LL="$OUT_DIR/uint8_slice_minimal.ll"
UINT8_SLICE_O="$OUT_DIR/uint8_slice_minimal.o"
SLICE_LENGTH_LL="$OUT_DIR/slice_length_minimal.ll"
SLICE_LENGTH_O="$OUT_DIR/slice_length_minimal.o"
SLICE_INDEX_LL="$OUT_DIR/slice_index_minimal.ll"
SLICE_INDEX_O="$OUT_DIR/slice_index_minimal.o"
SLICE_ASSIGN_LL="$OUT_DIR/slice_assign_minimal.ll"
SLICE_ASSIGN_O="$OUT_DIR/slice_assign_minimal.o"
SLICE_RETURN_LENGTH_LL="$OUT_DIR/slice_return_length_minimal.ll"
SLICE_RETURN_LENGTH_O="$OUT_DIR/slice_return_length_minimal.o"
ARRAY_LL="$OUT_DIR/array_minimal.ll"
ARRAY_O="$OUT_DIR/array_minimal.o"
INFER_ARRAY_LENGTH_LL="$OUT_DIR/infer_array_length_minimal.ll"
INFER_ARRAY_LENGTH_O="$OUT_DIR/infer_array_length_minimal.o"
TYPED_ARRAY_CONSTRUCTOR_LL="$OUT_DIR/typed_array_constructor_minimal.ll"
TYPED_ARRAY_CONSTRUCTOR_O="$OUT_DIR/typed_array_constructor_minimal.o"
TYPED_ARRAY_CONSTRUCTOR_INFER_LL="$OUT_DIR/typed_array_constructor_infer_minimal.ll"
TYPED_ARRAY_CONSTRUCTOR_INFER_O="$OUT_DIR/typed_array_constructor_infer_minimal.o"
ARRAY_ASSIGN_LL="$OUT_DIR/array_assign_minimal.ll"
ARRAY_ASSIGN_O="$OUT_DIR/array_assign_minimal.o"
UINT8_ARRAY_STRING_LL="$OUT_DIR/uint8_array_string_minimal.ll"
UINT8_ARRAY_STRING_O="$OUT_DIR/uint8_array_string_minimal.o"
INFER_UINT8_ARRAY_STRING_LL="$OUT_DIR/infer_uint8_array_string_minimal.ll"
INFER_UINT8_ARRAY_STRING_O="$OUT_DIR/infer_uint8_array_string_minimal.o"
NESTED_ARRAY_LL="$OUT_DIR/nested_array_minimal.ll"
NESTED_ARRAY_O="$OUT_DIR/nested_array_minimal.o"
STRUCT_ARRAY_FIELD_LL="$OUT_DIR/struct_array_field_minimal.ll"
STRUCT_ARRAY_FIELD_O="$OUT_DIR/struct_array_field_minimal.o"
ENUM_LL="$OUT_DIR/enum_minimal.ll"
ENUM_O="$OUT_DIR/enum_minimal.o"
ENUM_VALUE_LL="$OUT_DIR/enum_value_minimal.ll"
ENUM_VALUE_O="$OUT_DIR/enum_value_minimal.o"
STRUCT_LL="$OUT_DIR/struct_minimal.ll"
STRUCT_O="$OUT_DIR/struct_minimal.o"
FIELDS_LL="$OUT_DIR/fields_minimal.ll"
FIELDS_O="$OUT_DIR/fields_minimal.o"
CALL_FIELD_LL="$OUT_DIR/call_result_field_minimal.ll"
CALL_FIELD_O="$OUT_DIR/call_result_field_minimal.o"
NESTED_FIELDS_LL="$OUT_DIR/nested_fields_minimal.ll"
NESTED_FIELDS_O="$OUT_DIR/nested_fields_minimal.o"
MULTI_FILE_LL="$OUT_DIR/multi_file_minimal.ll"
MULTI_FILE_O="$OUT_DIR/multi_file_minimal.o"
NAMESPACED_LL="$OUT_DIR/namespaced_import_minimal.ll"
NAMESPACED_O="$OUT_DIR/namespaced_import_minimal.o"
ALIAS_IMPORT_FUNCTION_LL="$OUT_DIR/alias_import_function_minimal.ll"
ALIAS_IMPORT_FUNCTION_O="$OUT_DIR/alias_import_function_minimal.o"
PUBLIC_ALIAS_FUNCTION_LL="$OUT_DIR/public_alias_function_minimal.ll"
PUBLIC_ALIAS_FUNCTION_O="$OUT_DIR/public_alias_function_minimal.o"
ALIAS_IMPORT_TYPE_LL="$OUT_DIR/alias_import_type_minimal.ll"
ALIAS_IMPORT_TYPE_O="$OUT_DIR/alias_import_type_minimal.o"
PUBLIC_ALIAS_TYPE_LL="$OUT_DIR/public_alias_type_minimal.ll"
PUBLIC_ALIAS_TYPE_O="$OUT_DIR/public_alias_type_minimal.o"
MULTI_STRUCT_LL="$OUT_DIR/multi_file_struct_minimal.ll"
MULTI_STRUCT_O="$OUT_DIR/multi_file_struct_minimal.o"
MULTI_STRUCT_RETURN_LL="$OUT_DIR/multi_file_struct_return_minimal.ll"
MULTI_STRUCT_RETURN_O="$OUT_DIR/multi_file_struct_return_minimal.o"
MULTI_STRUCT_ARRAY_LL="$OUT_DIR/multi_file_struct_array_minimal.ll"
MULTI_STRUCT_ARRAY_O="$OUT_DIR/multi_file_struct_array_minimal.o"
NS_STRUCT_LL="$OUT_DIR/namespaced_struct_import_minimal.ll"
NS_STRUCT_O="$OUT_DIR/namespaced_struct_import_minimal.o"
NS_STRUCT_RETURN_LL="$OUT_DIR/namespaced_struct_return_minimal.ll"
NS_STRUCT_RETURN_O="$OUT_DIR/namespaced_struct_return_minimal.o"
NS_STRUCT_ARRAY_LL="$OUT_DIR/namespaced_struct_array_minimal.ll"
NS_STRUCT_ARRAY_O="$OUT_DIR/namespaced_struct_array_minimal.o"
MULTI_ENUM_LL="$OUT_DIR/multi_file_enum_minimal.ll"
MULTI_ENUM_O="$OUT_DIR/multi_file_enum_minimal.o"
MULTI_ENUM_SHORTHAND_LL="$OUT_DIR/multi_file_enum_shorthand_minimal.ll"
MULTI_ENUM_SHORTHAND_O="$OUT_DIR/multi_file_enum_shorthand_minimal.o"
MULTI_ENUM_SHORTHAND_ARG_LL="$OUT_DIR/multi_file_enum_shorthand_arg_minimal.ll"
MULTI_ENUM_SHORTHAND_ARG_O="$OUT_DIR/multi_file_enum_shorthand_arg_minimal.o"
MULTI_ENUM_FIELD_SHORTHAND_LL="$OUT_DIR/multi_file_enum_field_shorthand_minimal.ll"
MULTI_ENUM_FIELD_SHORTHAND_O="$OUT_DIR/multi_file_enum_field_shorthand_minimal.o"
MULTI_ENUM_VALUE_LL="$OUT_DIR/multi_file_enum_value_minimal.ll"
MULTI_ENUM_VALUE_O="$OUT_DIR/multi_file_enum_value_minimal.o"
NS_ENUM_LL="$OUT_DIR/namespaced_enum_import_minimal.ll"
NS_ENUM_O="$OUT_DIR/namespaced_enum_import_minimal.o"
NS_ENUM_SHORTHAND_LL="$OUT_DIR/namespaced_enum_shorthand_minimal.ll"
NS_ENUM_SHORTHAND_O="$OUT_DIR/namespaced_enum_shorthand_minimal.o"
NS_ENUM_FIELD_SHORTHAND_LL="$OUT_DIR/namespaced_enum_field_shorthand_minimal.ll"
NS_ENUM_FIELD_SHORTHAND_O="$OUT_DIR/namespaced_enum_field_shorthand_minimal.o"
NS_ENUM_VALUE_LL="$OUT_DIR/namespaced_enum_value_minimal.ll"
NS_ENUM_VALUE_O="$OUT_DIR/namespaced_enum_value_minimal.o"
MULTI_SLICE_RETURN_LL="$OUT_DIR/multi_file_slice_return_minimal.ll"
MULTI_SLICE_RETURN_O="$OUT_DIR/multi_file_slice_return_minimal.o"
NS_SLICE_RETURN_LL="$OUT_DIR/namespaced_slice_return_minimal.ll"
NS_SLICE_RETURN_O="$OUT_DIR/namespaced_slice_return_minimal.o"
MULTI_SLICE_INDEX_LL="$OUT_DIR/multi_file_slice_index_minimal.ll"
MULTI_SLICE_INDEX_O="$OUT_DIR/multi_file_slice_index_minimal.o"
NS_SLICE_INDEX_LL="$OUT_DIR/namespaced_slice_index_minimal.ll"
NS_SLICE_INDEX_O="$OUT_DIR/namespaced_slice_index_minimal.o"
POINTER_LL="$OUT_DIR/pointer_minimal.ll"
POINTER_O="$OUT_DIR/pointer_minimal.o"
MULTI_POINTER_LL="$OUT_DIR/multi_file_pointer_minimal.ll"
MULTI_POINTER_O="$OUT_DIR/multi_file_pointer_minimal.o"
NS_POINTER_LL="$OUT_DIR/namespaced_pointer_minimal.ll"
NS_POINTER_O="$OUT_DIR/namespaced_pointer_minimal.o"
ARRAY_TO_SLICE_ARG_LL="$OUT_DIR/array_to_slice_arg_minimal.ll"
ARRAY_TO_SLICE_ARG_O="$OUT_DIR/array_to_slice_arg_minimal.o"
ARRAY_TO_SLICE_LOCAL_LL="$OUT_DIR/array_to_slice_local_minimal.ll"
ARRAY_TO_SLICE_LOCAL_O="$OUT_DIR/array_to_slice_local_minimal.o"
ARRAY_TO_SLICE_ASSIGN_LL="$OUT_DIR/array_to_slice_assign_minimal.ll"
ARRAY_TO_SLICE_ASSIGN_O="$OUT_DIR/array_to_slice_assign_minimal.o"
ARRAY_TO_SLICE_RETURN_LL="$OUT_DIR/array_to_slice_return_minimal.ll"
ARRAY_TO_SLICE_RETURN_O="$OUT_DIR/array_to_slice_return_minimal.o"
GLOBAL_LL="$OUT_DIR/global_minimal.ll"
GLOBAL_O="$OUT_DIR/global_minimal.o"
UNARY_TUPLE_GLOBAL_LL="$OUT_DIR/unary_tuple_global_decl_minimal.ll"
UNARY_TUPLE_GLOBAL_O="$OUT_DIR/unary_tuple_global_decl_minimal.o"
INFER_LOCAL_LL="$OUT_DIR/infer_local_minimal.ll"
INFER_LOCAL_O="$OUT_DIR/infer_local_minimal.o"
INFER_MUTABLE_LOCAL_LL="$OUT_DIR/infer_mutable_local_minimal.ll"
INFER_MUTABLE_LOCAL_O="$OUT_DIR/infer_mutable_local_minimal.o"
UNARY_TUPLE_LOCAL_LL="$OUT_DIR/unary_tuple_local_decl_minimal.ll"
UNARY_TUPLE_LOCAL_O="$OUT_DIR/unary_tuple_local_decl_minimal.o"
UNARY_TUPLE_INFER_LOCAL_LL="$OUT_DIR/unary_tuple_infer_local_decl_minimal.ll"
UNARY_TUPLE_INFER_LOCAL_O="$OUT_DIR/unary_tuple_infer_local_decl_minimal.o"
TUPLE_DESTRUCTURE_LL="$OUT_DIR/tuple_destructure_minimal.ll"
TUPLE_DESTRUCTURE_O="$OUT_DIR/tuple_destructure_minimal.o"
TUPLE_DESTRUCTURE_INFER_LL="$OUT_DIR/tuple_destructure_infer_minimal.ll"
TUPLE_DESTRUCTURE_INFER_O="$OUT_DIR/tuple_destructure_infer_minimal.o"
TUPLE_DESTRUCTURE_MUTABLE_INFER_LL="$OUT_DIR/tuple_destructure_mutable_infer_minimal.ll"
TUPLE_DESTRUCTURE_MUTABLE_INFER_O="$OUT_DIR/tuple_destructure_mutable_infer_minimal.o"
TUPLE_DESTRUCTURE_GLOBAL_LL="$OUT_DIR/tuple_destructure_global_minimal.ll"
TUPLE_DESTRUCTURE_GLOBAL_O="$OUT_DIR/tuple_destructure_global_minimal.o"
TUPLE_DESTRUCTURE_RETURN_LL="$OUT_DIR/tuple_destructure_return_minimal.ll"
TUPLE_DESTRUCTURE_RETURN_O="$OUT_DIR/tuple_destructure_return_minimal.o"
TUPLE_VALUE_LL="$OUT_DIR/tuple_value_minimal.ll"
TUPLE_VALUE_O="$OUT_DIR/tuple_value_minimal.o"
TERNARY_LL="$OUT_DIR/ternary_minimal.ll"
TERNARY_O="$OUT_DIR/ternary_minimal.o"
TERNARY_ENUM_LL="$OUT_DIR/ternary_enum_minimal.ll"
TERNARY_ENUM_O="$OUT_DIR/ternary_enum_minimal.o"
TUPLE_RETURN_LL="$OUT_DIR/tuple_return_minimal.ll"
TUPLE_RETURN_O="$OUT_DIR/tuple_return_minimal.o"
TUPLE_INFER_LL="$OUT_DIR/tuple_infer_minimal.ll"
TUPLE_INFER_O="$OUT_DIR/tuple_infer_minimal.o"
INFER_GLOBAL_LL="$OUT_DIR/infer_global_minimal.ll"
INFER_GLOBAL_O="$OUT_DIR/infer_global_minimal.o"
OPTIONAL_LL="$OUT_DIR/optional_minimal.ll"
OPTIONAL_O="$OUT_DIR/optional_minimal.o"
OPTIONAL_NULL_COMPARE_LL="$OUT_DIR/optional_null_compare_minimal.ll"
OPTIONAL_NULL_COMPARE_O="$OUT_DIR/optional_null_compare_minimal.o"
OPTIONAL_COALESCE_LL="$OUT_DIR/optional_coalesce_minimal.ll"
OPTIONAL_COALESCE_O="$OUT_DIR/optional_coalesce_minimal.o"
OPTIONAL_IF_NARROW_LL="$OUT_DIR/optional_if_narrow_minimal.ll"
OPTIONAL_IF_NARROW_O="$OUT_DIR/optional_if_narrow_minimal.o"
OPTIONAL_ELSE_NARROW_LL="$OUT_DIR/optional_else_narrow_minimal.ll"
OPTIONAL_ELSE_NARROW_O="$OUT_DIR/optional_else_narrow_minimal.o"
OPTIONAL_NESTED_ARRAY_LL="$OUT_DIR/optional_nested_array_minimal.ll"
OPTIONAL_NESTED_ARRAY_O="$OUT_DIR/optional_nested_array_minimal.o"
OPTIONAL_SOME_PATTERN_LL="$OUT_DIR/optional_some_pattern_minimal.ll"
OPTIONAL_SOME_PATTERN_O="$OUT_DIR/optional_some_pattern_minimal.o"
OPTIONAL_SWITCH_PATTERN_LL="$OUT_DIR/optional_switch_pattern_minimal.ll"
OPTIONAL_SWITCH_PATTERN_O="$OUT_DIR/optional_switch_pattern_minimal.o"
OPTIONAL_CHAIN_MEMBER_LL="$OUT_DIR/optional_chain_member_minimal.ll"
OPTIONAL_CHAIN_MEMBER_O="$OUT_DIR/optional_chain_member_minimal.o"
OPTIONAL_CHAIN_INDEX_LL="$OUT_DIR/optional_chain_index_minimal.ll"
OPTIONAL_CHAIN_INDEX_O="$OUT_DIR/optional_chain_index_minimal.o"
OPTIONAL_CHAIN_NESTED_PURE_BASE_LL="$OUT_DIR/optional_chain_nested_pure_base_minimal.ll"
OPTIONAL_CHAIN_NESTED_PURE_BASE_O="$OUT_DIR/optional_chain_nested_pure_base_minimal.o"
MUTABLE_QUALIFIER_LL="$OUT_DIR/mutable_qualifier_minimal.ll"
MUTABLE_QUALIFIER_O="$OUT_DIR/mutable_qualifier_minimal.o"
MUTABLE_ARRAY_QUALIFIER_LL="$OUT_DIR/mutable_array_qualifier_minimal.ll"
MUTABLE_ARRAY_QUALIFIER_O="$OUT_DIR/mutable_array_qualifier_minimal.o"
STRUCT_OPTIONAL_FIELD_LL="$OUT_DIR/struct_optional_field_minimal.ll"
STRUCT_OPTIONAL_FIELD_O="$OUT_DIR/struct_optional_field_minimal.o"
STRUCT_OPTIONAL_FIELD_OMITTED_LL="$OUT_DIR/struct_optional_field_omitted_minimal.ll"
STRUCT_OPTIONAL_FIELD_OMITTED_O="$OUT_DIR/struct_optional_field_omitted_minimal.o"
STRUCT_INIT_LL="$OUT_DIR/struct_init_minimal.ll"
STRUCT_INIT_O="$OUT_DIR/struct_init_minimal.o"
STRUCT_CONSTRUCTOR_SUGAR_LL="$OUT_DIR/struct_constructor_sugar_minimal.ll"
STRUCT_CONSTRUCTOR_SUGAR_O="$OUT_DIR/struct_constructor_sugar_minimal.o"
STRUCT_NEW_CONSTRUCTOR_LL="$OUT_DIR/struct_new_constructor_minimal.ll"
STRUCT_NEW_CONSTRUCTOR_O="$OUT_DIR/struct_new_constructor_minimal.o"
STRUCT_INIT_DEFAULTS_LL="$OUT_DIR/struct_init_with_defaults_minimal.ll"
STRUCT_INIT_DEFAULTS_O="$OUT_DIR/struct_init_with_defaults_minimal.o"
STRUCT_INIT_MUTABLE_DEFAULT_OVERRIDE_LL="$OUT_DIR/struct_init_mutable_default_override_minimal.ll"
STRUCT_INIT_MUTABLE_DEFAULT_OVERRIDE_O="$OUT_DIR/struct_init_mutable_default_override_minimal.o"
STRUCT_INIT_OPTIONAL_OMITTED_LL="$OUT_DIR/struct_init_optional_omitted_minimal.ll"
STRUCT_INIT_OPTIONAL_OMITTED_O="$OUT_DIR/struct_init_optional_omitted_minimal.o"
STRUCT_INIT_BRANCH_COMPLETE_LL="$OUT_DIR/struct_init_branch_complete_minimal.ll"
STRUCT_INIT_BRANCH_COMPLETE_O="$OUT_DIR/struct_init_branch_complete_minimal.o"
EMPTY_TUPLE_RETURN_LL="$OUT_DIR/empty_tuple_return_minimal.ll"
EMPTY_TUPLE_RETURN_O="$OUT_DIR/empty_tuple_return_minimal.o"
EMPTY_TUPLE_BARE_RETURN_LL="$OUT_DIR/empty_tuple_bare_return_minimal.ll"
EMPTY_TUPLE_BARE_RETURN_O="$OUT_DIR/empty_tuple_bare_return_minimal.o"
UNARY_TUPLE_RETURN_LL="$OUT_DIR/unary_tuple_return_minimal.ll"
UNARY_TUPLE_RETURN_O="$OUT_DIR/unary_tuple_return_minimal.o"

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/minimal.jiang" > "$MINIMAL_LL"
rg -q '^define i64 @add\(i64 %0, i64 %1\)' "$MINIMAL_LL"
rg -q '^define i32 @main\(\)' "$MINIMAL_LL"
rg -q 'add i64' "$MINIMAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MINIMAL_LL" -o "$MINIMAL_O"
set +e
"$LLVM_LLI" "$MINIMAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/while_minimal.jiang" > "$WHILE_LL"
rg -q '^define i32 @main\(\)' "$WHILE_LL"
rg -q 'br i1' "$WHILE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$WHILE_LL" -o "$WHILE_O"
set +e
"$LLVM_LLI" "$WHILE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 10 ]]; then
    echo "stage2 llvm smoke expected while_minimal exit code 10, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/break_continue_minimal.jiang" > "$BREAK_CONTINUE_LL"
rg -q '^define i32 @main\(\)' "$BREAK_CONTINUE_LL"
rg -q 'br label %while\.cond' "$BREAK_CONTINUE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$BREAK_CONTINUE_LL" -o "$BREAK_CONTINUE_O"
set +e
"$LLVM_LLI" "$BREAK_CONTINUE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 8 ]]; then
    echo "stage2 llvm smoke expected break_continue_minimal exit code 8, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/for_range_minimal.jiang" > "$FOR_RANGE_LL"
rg -q '^define i32 @main\(\)' "$FOR_RANGE_LL"
rg -q 'for\.cond' "$FOR_RANGE_LL"
rg -q 'for\.step' "$FOR_RANGE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FOR_RANGE_LL" -o "$FOR_RANGE_O"
set +e
"$LLVM_LLI" "$FOR_RANGE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 8 ]]; then
    echo "stage2 llvm smoke expected for_range_minimal exit code 8, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/for_infer_range_minimal.jiang" > "$FOR_INFER_RANGE_LL"
rg -q '^define i32 @main\(\)' "$FOR_INFER_RANGE_LL"
rg -q 'for\.cond' "$FOR_INFER_RANGE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FOR_INFER_RANGE_LL" -o "$FOR_INFER_RANGE_O"
set +e
"$LLVM_LLI" "$FOR_INFER_RANGE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 5 ]]; then
    echo "stage2 llvm smoke expected for_infer_range_minimal exit code 5, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/for_item_array_minimal.jiang" > "$FOR_ITEM_ARRAY_LL"
rg -q '^define i32 @main\(\)' "$FOR_ITEM_ARRAY_LL"
rg -q 'define i64 @.*sum_items\(\)' "$FOR_ITEM_ARRAY_LL"
rg -q '%Slice_int64_t = type \{ ptr, i64 \}' "$FOR_ITEM_ARRAY_LL"
rg -q 'extractvalue %Slice_int64_t .*?, 1' "$FOR_ITEM_ARRAY_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FOR_ITEM_ARRAY_LL" -o "$FOR_ITEM_ARRAY_O"
set +e
"$LLVM_LLI" "$FOR_ITEM_ARRAY_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected for_item_array_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/for_indexed_minimal.jiang" > "$FOR_INDEXED_LL"
rg -q '^define i32 @main\(\)' "$FOR_INDEXED_LL"
rg -q 'store i64 %for.index.cur, ptr %i' "$FOR_INDEXED_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FOR_INDEXED_LL" -o "$FOR_INDEXED_O"
set +e
"$LLVM_LLI" "$FOR_INDEXED_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 40 ]]; then
    echo "stage2 llvm smoke expected for_indexed_minimal exit code 40, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/for_indexed_typed_minimal.jiang" > "$FOR_INDEXED_TYPED_LL"
rg -q '^define i32 @main\(\)' "$FOR_INDEXED_TYPED_LL"
rg -q 'store i64 %for.index.cur, ptr %i' "$FOR_INDEXED_TYPED_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FOR_INDEXED_TYPED_LL" -o "$FOR_INDEXED_TYPED_O"
set +e
"$LLVM_LLI" "$FOR_INDEXED_TYPED_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 40 ]]; then
    echo "stage2 llvm smoke expected for_indexed_typed_minimal exit code 40, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/for_indexed_tuple_binding_minimal.jiang" > "$FOR_INDEXED_TUPLE_BINDING_LL"
rg -q '^define i32 @main\(\)' "$FOR_INDEXED_TUPLE_BINDING_LL"
rg -q 'store i64 %for.index.cur, ptr %i' "$FOR_INDEXED_TUPLE_BINDING_LL"
rg -q 'extractvalue %Tuple_' "$FOR_INDEXED_TUPLE_BINDING_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FOR_INDEXED_TUPLE_BINDING_LL" -o "$FOR_INDEXED_TUPLE_BINDING_O"
set +e
"$LLVM_LLI" "$FOR_INDEXED_TUPLE_BINDING_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected for_indexed_tuple_binding_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/for_mutable_binding_minimal.jiang" > "$FOR_MUTABLE_BINDING_LL"
rg -q '^define i32 @main\(\)' "$FOR_MUTABLE_BINDING_LL"
rg -q 'store i64 %for.item, ptr %value' "$FOR_MUTABLE_BINDING_LL"
rg -q 'store i64 %addtmp, ptr %value' "$FOR_MUTABLE_BINDING_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FOR_MUTABLE_BINDING_LL" -o "$FOR_MUTABLE_BINDING_O"
set +e
"$LLVM_LLI" "$FOR_MUTABLE_BINDING_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected for_mutable_binding_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/for_indexed_mutable_tuple_binding_minimal.jiang" > "$FOR_INDEXED_MUTABLE_TUPLE_BINDING_LL"
rg -q '^define i32 @main\(\)' "$FOR_INDEXED_MUTABLE_TUPLE_BINDING_LL"
rg -q 'store i64 %for.index.cur, ptr %i' "$FOR_INDEXED_MUTABLE_TUPLE_BINDING_LL"
rg -q 'extractvalue %Tuple_' "$FOR_INDEXED_MUTABLE_TUPLE_BINDING_LL"
rg -q 'store i64 %addtmp, ptr %left' "$FOR_INDEXED_MUTABLE_TUPLE_BINDING_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FOR_INDEXED_MUTABLE_TUPLE_BINDING_LL" -o "$FOR_INDEXED_MUTABLE_TUPLE_BINDING_O"
set +e
"$LLVM_LLI" "$FOR_INDEXED_MUTABLE_TUPLE_BINDING_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected for_indexed_mutable_tuple_binding_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/for_tuple_binding_minimal.jiang" > "$FOR_TUPLE_BINDING_LL"
rg -q '^define i32 @main\(\)' "$FOR_TUPLE_BINDING_LL"
rg -q 'extractvalue %Tuple_' "$FOR_TUPLE_BINDING_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FOR_TUPLE_BINDING_LL" -o "$FOR_TUPLE_BINDING_O"
set +e
"$LLVM_LLI" "$FOR_TUPLE_BINDING_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected for_tuple_binding_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/for_tuple_binding_typed_minimal.jiang" > "$FOR_TUPLE_BINDING_TYPED_LL"
rg -q '^define i32 @main\(\)' "$FOR_TUPLE_BINDING_TYPED_LL"
rg -q 'extractvalue %Tuple_' "$FOR_TUPLE_BINDING_TYPED_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FOR_TUPLE_BINDING_TYPED_LL" -o "$FOR_TUPLE_BINDING_TYPED_O"
set +e
"$LLVM_LLI" "$FOR_TUPLE_BINDING_TYPED_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected for_tuple_binding_typed_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/switch_enum_minimal.jiang" > "$SWITCH_ENUM_LL"
rg -q '^define i32 @main\(\)' "$SWITCH_ENUM_LL"
rg -q 'switch\.case' "$SWITCH_ENUM_LL"
rg -q 'switch\.end' "$SWITCH_ENUM_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$SWITCH_ENUM_LL" -o "$SWITCH_ENUM_O"
set +e
"$LLVM_LLI" "$SWITCH_ENUM_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected switch_enum_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/enum_shorthand_minimal.jiang" > "$ENUM_SHORTHAND_LL"
rg -q '^define i32 @main\(\)' "$ENUM_SHORTHAND_LL"
rg -q 'ret i64 42' "$ENUM_SHORTHAND_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ENUM_SHORTHAND_LL" -o "$ENUM_SHORTHAND_O"
set +e
"$LLVM_LLI" "$ENUM_SHORTHAND_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected enum_shorthand_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/enum_shorthand_arg_minimal.jiang" > "$ENUM_SHORTHAND_ARG_LL"
rg -q '^define i32 @main\(\)' "$ENUM_SHORTHAND_ARG_LL"
rg -q 'HttpStatus_ok' "$ENUM_SHORTHAND_ARG_LL" || rg -q 'i64 42' "$ENUM_SHORTHAND_ARG_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ENUM_SHORTHAND_ARG_LL" -o "$ENUM_SHORTHAND_ARG_O"
set +e
"$LLVM_LLI" "$ENUM_SHORTHAND_ARG_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected enum_shorthand_arg_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/enum_switch_shorthand_minimal.jiang" > "$ENUM_SWITCH_SHORTHAND_LL"
rg -q '^define i32 @main\(\)' "$ENUM_SWITCH_SHORTHAND_LL"
rg -q 'Priority_medium' "$ENUM_SWITCH_SHORTHAND_LL" || rg -q 'i64 42' "$ENUM_SWITCH_SHORTHAND_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ENUM_SWITCH_SHORTHAND_LL" -o "$ENUM_SWITCH_SHORTHAND_O"
set +e
"$LLVM_LLI" "$ENUM_SWITCH_SHORTHAND_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected enum_switch_shorthand_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_enum_field_shorthand_minimal.jiang" > "$STRUCT_ENUM_FIELD_SHORTHAND_LL"
rg -q '^%Config = type \{ i64 \}' "$STRUCT_ENUM_FIELD_SHORTHAND_LL"
rg -q 'Priority_medium' "$STRUCT_ENUM_FIELD_SHORTHAND_LL" || rg -q 'i64 42' "$STRUCT_ENUM_FIELD_SHORTHAND_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_ENUM_FIELD_SHORTHAND_LL" -o "$STRUCT_ENUM_FIELD_SHORTHAND_O"
set +e
"$LLVM_LLI" "$STRUCT_ENUM_FIELD_SHORTHAND_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected struct_enum_field_shorthand_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_minimal.jiang" > "$UNION_LL"
rg -q '^%MyUnion = type \{ i64, i64, i64 \}' "$UNION_LL"
rg -q 'switch\.union\.tag' "$UNION_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_LL" -o "$UNION_O"
set +e
"$LLVM_LLI" "$UNION_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_shorthand_minimal.jiang" > "$UNION_SHORTHAND_LL"
rg -q '^%MyUnion = type ' "$UNION_SHORTHAND_LL"
rg -q 'i64 42' "$UNION_SHORTHAND_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_SHORTHAND_LL" -o "$UNION_SHORTHAND_O"
set +e
"$LLVM_LLI" "$UNION_SHORTHAND_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_shorthand_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_bind_minimal.jiang" > "$UNION_BIND_LL"
rg -q '^%MyUnion = type \{ i64, i64, i64, i8 \}' "$UNION_BIND_LL"
rg -q 'union\.payload' "$UNION_BIND_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_BIND_LL" -o "$UNION_BIND_O"
set +e
"$LLVM_LLI" "$UNION_BIND_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_bind_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_tuple_bind_minimal.jiang" > "$UNION_TUPLE_BIND_LL"
rg -q 'tuple\.item' "$UNION_TUPLE_BIND_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_TUPLE_BIND_LL" -o "$UNION_TUPLE_BIND_O"
set +e
"$LLVM_LLI" "$UNION_TUPLE_BIND_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_tuple_bind_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_if_pattern_minimal.jiang" > "$UNION_IF_PATTERN_LL"
rg -q 'if\.pattern\.tag' "$UNION_IF_PATTERN_LL"
rg -q 'pattern\.bind' "$UNION_IF_PATTERN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_IF_PATTERN_LL" -o "$UNION_IF_PATTERN_O"
set +e
"$LLVM_LLI" "$UNION_IF_PATTERN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_if_pattern_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_if_mutable_binding_minimal.jiang" > "$UNION_IF_MUTABLE_BINDING_LL"
rg -q 'if\.pattern\.tag' "$UNION_IF_MUTABLE_BINDING_LL"
rg -q 'pattern\.bind' "$UNION_IF_MUTABLE_BINDING_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_IF_MUTABLE_BINDING_LL" -o "$UNION_IF_MUTABLE_BINDING_O"
set +e
"$LLVM_LLI" "$UNION_IF_MUTABLE_BINDING_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_if_mutable_binding_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_if_shorthand_pattern_minimal.jiang" > "$UNION_IF_SHORTHAND_PATTERN_LL"
rg -q 'if\.pattern\.tag' "$UNION_IF_SHORTHAND_PATTERN_LL"
rg -q 'pattern\.bind' "$UNION_IF_SHORTHAND_PATTERN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_IF_SHORTHAND_PATTERN_LL" -o "$UNION_IF_SHORTHAND_PATTERN_O"
set +e
"$LLVM_LLI" "$UNION_IF_SHORTHAND_PATTERN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_if_shorthand_pattern_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_tuple_if_shorthand_pattern_minimal.jiang" > "$UNION_TUPLE_IF_SHORTHAND_PATTERN_LL"
rg -q 'tuple\.item' "$UNION_TUPLE_IF_SHORTHAND_PATTERN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_TUPLE_IF_SHORTHAND_PATTERN_LL" -o "$UNION_TUPLE_IF_SHORTHAND_PATTERN_O"
set +e
"$LLVM_LLI" "$UNION_TUPLE_IF_SHORTHAND_PATTERN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_tuple_if_shorthand_pattern_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_tuple_if_mutable_shorthand_pattern_minimal.jiang" > "$UNION_TUPLE_IF_MUTABLE_SHORTHAND_PATTERN_LL"
rg -q 'tuple\.item' "$UNION_TUPLE_IF_MUTABLE_SHORTHAND_PATTERN_LL"
rg -q 'store i64 %addtmp, ptr %pattern.bind' "$UNION_TUPLE_IF_MUTABLE_SHORTHAND_PATTERN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_TUPLE_IF_MUTABLE_SHORTHAND_PATTERN_LL" -o "$UNION_TUPLE_IF_MUTABLE_SHORTHAND_PATTERN_O"
set +e
"$LLVM_LLI" "$UNION_TUPLE_IF_MUTABLE_SHORTHAND_PATTERN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_tuple_if_mutable_shorthand_pattern_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_switch_shorthand_pattern_minimal.jiang" > "$UNION_SWITCH_SHORTHAND_PATTERN_LL"
rg -q 'switch\.union\.tag' "$UNION_SWITCH_SHORTHAND_PATTERN_LL"
rg -q 'union\.payload' "$UNION_SWITCH_SHORTHAND_PATTERN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_SWITCH_SHORTHAND_PATTERN_LL" -o "$UNION_SWITCH_SHORTHAND_PATTERN_O"
set +e
"$LLVM_LLI" "$UNION_SWITCH_SHORTHAND_PATTERN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_switch_shorthand_pattern_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_switch_mutable_binding_minimal.jiang" > "$UNION_SWITCH_MUTABLE_BINDING_LL"
rg -q 'switch\.union\.tag' "$UNION_SWITCH_MUTABLE_BINDING_LL"
rg -q 'union\.payload' "$UNION_SWITCH_MUTABLE_BINDING_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_SWITCH_MUTABLE_BINDING_LL" -o "$UNION_SWITCH_MUTABLE_BINDING_O"
set +e
"$LLVM_LLI" "$UNION_SWITCH_MUTABLE_BINDING_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_switch_mutable_binding_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/union_tuple_switch_mutable_binding_minimal.jiang" > "$UNION_TUPLE_SWITCH_MUTABLE_BINDING_LL"
rg -q 'switch\.union\.tag' "$UNION_TUPLE_SWITCH_MUTABLE_BINDING_LL"
rg -q 'tuple\.item' "$UNION_TUPLE_SWITCH_MUTABLE_BINDING_LL"
rg -q 'store i64 %addtmp, ptr %left' "$UNION_TUPLE_SWITCH_MUTABLE_BINDING_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNION_TUPLE_SWITCH_MUTABLE_BINDING_LL" -o "$UNION_TUPLE_SWITCH_MUTABLE_BINDING_O"
set +e
"$LLVM_LLI" "$UNION_TUPLE_SWITCH_MUTABLE_BINDING_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected union_tuple_switch_mutable_binding_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_union_field_shorthand_minimal.jiang" > "$STRUCT_UNION_FIELD_SHORTHAND_LL"
rg -q '^%Box = type \{ %MyUnion \}' "$STRUCT_UNION_FIELD_SHORTHAND_LL"
rg -q 'union\.payload' "$STRUCT_UNION_FIELD_SHORTHAND_LL"
rg -q 'i64 42' "$STRUCT_UNION_FIELD_SHORTHAND_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_UNION_FIELD_SHORTHAND_LL" -o "$STRUCT_UNION_FIELD_SHORTHAND_O"
set +e
"$LLVM_LLI" "$STRUCT_UNION_FIELD_SHORTHAND_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected struct_union_field_shorthand_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/uint8_minimal.jiang" > "$UINT8_LL"
rg -q '^define i8 @id\(i8 %0\)' "$UINT8_LL"
rg -q '^define i32 @main\(\)' "$UINT8_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UINT8_LL" -o "$UINT8_O"

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/uint8_slice_minimal.jiang" > "$UINT8_SLICE_LL"
rg -q '^%Slice_uint8_t = type \{ ptr, i64 \}' "$UINT8_SLICE_LL"
rg -q '^define %Slice_uint8_t @id\(%Slice_uint8_t %0\)' "$UINT8_SLICE_LL"
rg -q '^define i32 @main\(\)' "$UINT8_SLICE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UINT8_SLICE_LL" -o "$UINT8_SLICE_O"

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/slice_length_minimal.jiang" > "$SLICE_LENGTH_LL"
rg -q '^define i64 @len\(%Slice_uint8_t %0\)' "$SLICE_LENGTH_LL"
rg -q 'extractvalue %Slice_uint8_t .*?, 1' "$SLICE_LENGTH_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$SLICE_LENGTH_LL" -o "$SLICE_LENGTH_O"
set +e
"$LLVM_LLI" "$SLICE_LENGTH_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 llvm smoke expected slice_length_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/slice_index_minimal.jiang" > "$SLICE_INDEX_LL"
rg -q '^define i8 @pick\(%Slice_uint8_t %0\)' "$SLICE_INDEX_LL"
rg -q 'getelementptr i8, ptr' "$SLICE_INDEX_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$SLICE_INDEX_LL" -o "$SLICE_INDEX_O"

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/slice_assign_minimal.jiang" > "$SLICE_ASSIGN_LL"
rg -q '^define void @poke\(%Slice_uint8_t %0, i8 %1\)' "$SLICE_ASSIGN_LL"
rg -q 'store i8 %' "$SLICE_ASSIGN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$SLICE_ASSIGN_LL" -o "$SLICE_ASSIGN_O"

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/slice_return_length_minimal.jiang" > "$SLICE_RETURN_LENGTH_LL"
rg -q '^define %Slice_uint8_t @id\(%Slice_uint8_t %0\)' "$SLICE_RETURN_LENGTH_LL"
rg -q 'extractvalue %Slice_uint8_t .*?, 1' "$SLICE_RETURN_LENGTH_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$SLICE_RETURN_LENGTH_LL" -o "$SLICE_RETURN_LENGTH_O"
set +e
"$LLVM_LLI" "$SLICE_RETURN_LENGTH_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 llvm smoke expected slice_return_length_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_minimal.jiang" > "$ARRAY_LL"
rg -q '^define i32 @main\(\)' "$ARRAY_LL"
rg -q '\[5 x i64\]' "$ARRAY_LL"
rg -q 'store \[5 x i64\] \[i64 1, i64 2, i64 3, i64 4, i64 42\]' "$ARRAY_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_LL" -o "$ARRAY_O"
set +e
"$LLVM_LLI" "$ARRAY_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected array_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/infer_array_length_minimal.jiang" > "$INFER_ARRAY_LENGTH_LL"
rg -q '\[2 x i64\]' "$INFER_ARRAY_LENGTH_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$INFER_ARRAY_LENGTH_LL" -o "$INFER_ARRAY_LENGTH_O"
set +e
"$LLVM_LLI" "$INFER_ARRAY_LENGTH_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected infer_array_length_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/typed_array_constructor_minimal.jiang" > "$TYPED_ARRAY_CONSTRUCTOR_LL"
rg -q '\[2 x i64\]' "$TYPED_ARRAY_CONSTRUCTOR_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TYPED_ARRAY_CONSTRUCTOR_LL" -o "$TYPED_ARRAY_CONSTRUCTOR_O"
set +e
"$LLVM_LLI" "$TYPED_ARRAY_CONSTRUCTOR_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected typed_array_constructor_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/typed_array_constructor_infer_minimal.jiang" > "$TYPED_ARRAY_CONSTRUCTOR_INFER_LL"
rg -q '\[2 x i64\]' "$TYPED_ARRAY_CONSTRUCTOR_INFER_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TYPED_ARRAY_CONSTRUCTOR_INFER_LL" -o "$TYPED_ARRAY_CONSTRUCTOR_INFER_O"
set +e
"$LLVM_LLI" "$TYPED_ARRAY_CONSTRUCTOR_INFER_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected typed_array_constructor_infer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_assign_minimal.jiang" > "$ARRAY_ASSIGN_LL"
rg -q 'getelementptr \[3 x i64\], ptr' "$ARRAY_ASSIGN_LL"
rg -q 'store i64 10' "$ARRAY_ASSIGN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_ASSIGN_LL" -o "$ARRAY_ASSIGN_O"
set +e
"$LLVM_LLI" "$ARRAY_ASSIGN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 10 ]]; then
    echo "stage2 llvm smoke expected array_assign_minimal exit code 10, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/uint8_array_string_minimal.jiang" > "$UINT8_ARRAY_STRING_LL"
rg -q '\[3 x i8\]' "$UINT8_ARRAY_STRING_LL"
rg -q 'store \[3 x i8\] c"abc"' "$UINT8_ARRAY_STRING_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UINT8_ARRAY_STRING_LL" -o "$UINT8_ARRAY_STRING_O"
set +e
"$LLVM_LLI" "$UINT8_ARRAY_STRING_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 llvm smoke expected uint8_array_string_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/infer_uint8_array_string_minimal.jiang" > "$INFER_UINT8_ARRAY_STRING_LL"
rg -q '\[3 x i8\]' "$INFER_UINT8_ARRAY_STRING_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$INFER_UINT8_ARRAY_STRING_LL" -o "$INFER_UINT8_ARRAY_STRING_O"
set +e
"$LLVM_LLI" "$INFER_UINT8_ARRAY_STRING_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 llvm smoke expected infer_uint8_array_string_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/nested_array_minimal.jiang" > "$NESTED_ARRAY_LL"
rg -q '\[3 x \[2 x i64\]\]' "$NESTED_ARRAY_LL"
rg -q 'getelementptr \[3 x \[2 x i64\]\], ptr' "$NESTED_ARRAY_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NESTED_ARRAY_LL" -o "$NESTED_ARRAY_O"
set +e
"$LLVM_LLI" "$NESTED_ARRAY_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected nested_array_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_array_field_minimal.jiang" > "$STRUCT_ARRAY_FIELD_LL"
rg -q '^%Buffer = type \{ \[3 x i8\] \}' "$STRUCT_ARRAY_FIELD_LL"
rg -q 'getelementptr %Buffer, ptr' "$STRUCT_ARRAY_FIELD_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_ARRAY_FIELD_LL" -o "$STRUCT_ARRAY_FIELD_O"
set +e
"$LLVM_LLI" "$STRUCT_ARRAY_FIELD_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 llvm smoke expected struct_array_field_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/enum_minimal.jiang" > "$ENUM_LL"
rg -q '^define i64 @code\(i64 %0\)' "$ENUM_LL"
rg -q 'icmp eq i64' "$ENUM_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ENUM_LL" -o "$ENUM_O"
set +e
"$LLVM_LLI" "$ENUM_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 2 ]]; then
    echo "stage2 llvm smoke expected enum_minimal exit code 2, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/enum_value_minimal.jiang" > "$ENUM_VALUE_LL"
rg -q '^define i32 @main\(\)' "$ENUM_VALUE_LL"
rg -q 'ret i32 42' "$ENUM_VALUE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ENUM_VALUE_LL" -o "$ENUM_VALUE_O"
set +e
"$LLVM_LLI" "$ENUM_VALUE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected enum_value_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_minimal.jiang" > "$STRUCT_LL"
rg -q '^%Pair = type \{ i64, i64 \}' "$STRUCT_LL"
rg -q '^define i64 @sum_pair\(%Pair %0\)' "$STRUCT_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_LL" -o "$STRUCT_O"
set +e
"$LLVM_LLI" "$STRUCT_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected struct_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/fields_minimal.jiang" > "$FIELDS_LL"
rg -q 'extractvalue %Pair' "$FIELDS_LL"
rg -q 'getelementptr %Pair, ptr' "$FIELDS_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FIELDS_LL" -o "$FIELDS_O"
set +e
"$LLVM_LLI" "$FIELDS_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 llvm smoke expected fields_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/call_result_field_minimal.jiang" > "$CALL_FIELD_LL"
rg -q '^define %Box @make_box\(\)' "$CALL_FIELD_LL"
rg -q 'extractvalue %Box' "$CALL_FIELD_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$CALL_FIELD_LL" -o "$CALL_FIELD_O"
set +e
"$LLVM_LLI" "$CALL_FIELD_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected call_result_field_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/nested_fields_minimal.jiang" > "$NESTED_FIELDS_LL"
rg -q '^%Box = type \{ %Point \}' "$NESTED_FIELDS_LL"
rg -q 'extractvalue %Point' "$NESTED_FIELDS_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NESTED_FIELDS_LL" -o "$NESTED_FIELDS_O"
set +e
"$LLVM_LLI" "$NESTED_FIELDS_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected nested_fields_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_minimal.jiang" > "$MULTI_FILE_LL"
rg -q '^define i64 @helper_add\(i64 %0, i64 %1\)' "$MULTI_FILE_LL"
rg -q 'call i64 @helper_add' "$MULTI_FILE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_FILE_LL" -o "$MULTI_FILE_O"
set +e
"$LLVM_LLI" "$MULTI_FILE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected multi_file_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_import_minimal.jiang" > "$NAMESPACED_LL"
rg -q '^define i64 @helper_add\(i64 %0, i64 %1\)' "$NAMESPACED_LL"
rg -q 'call i64 @helper_add' "$NAMESPACED_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NAMESPACED_LL" -o "$NAMESPACED_O"
set +e
"$LLVM_LLI" "$NAMESPACED_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected namespaced_import_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/alias_import_function_minimal.jiang" > "$ALIAS_IMPORT_FUNCTION_LL"
rg -q '^define i64 @public_add\(i64 %0, i64 %1\)' "$ALIAS_IMPORT_FUNCTION_LL"
rg -q 'call i64 @public_add' "$ALIAS_IMPORT_FUNCTION_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ALIAS_IMPORT_FUNCTION_LL" -o "$ALIAS_IMPORT_FUNCTION_O"
set +e
"$LLVM_LLI" "$ALIAS_IMPORT_FUNCTION_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected alias_import_function_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/public_alias_function_minimal.jiang" > "$PUBLIC_ALIAS_FUNCTION_LL"
rg -q '^define i64 @public_add\(i64 %0, i64 %1\)' "$PUBLIC_ALIAS_FUNCTION_LL"
rg -q 'call i64 @public_add' "$PUBLIC_ALIAS_FUNCTION_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$PUBLIC_ALIAS_FUNCTION_LL" -o "$PUBLIC_ALIAS_FUNCTION_O"
set +e
"$LLVM_LLI" "$PUBLIC_ALIAS_FUNCTION_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected public_alias_function_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/alias_import_type_minimal.jiang" > "$ALIAS_IMPORT_TYPE_LL"
rg -q '^%PublicPair = type \{ i64, i64 \}' "$ALIAS_IMPORT_TYPE_LL"
rg -q 'store %PublicPair \{ i64 20, i64 22 \}' "$ALIAS_IMPORT_TYPE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ALIAS_IMPORT_TYPE_LL" -o "$ALIAS_IMPORT_TYPE_O"
set +e
"$LLVM_LLI" "$ALIAS_IMPORT_TYPE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected alias_import_type_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/public_alias_type_minimal.jiang" > "$PUBLIC_ALIAS_TYPE_LL"
rg -q '^%PublicPair = type \{ i64, i64 \}' "$PUBLIC_ALIAS_TYPE_LL"
rg -q 'store %PublicPair \{ i64 20, i64 22 \}' "$PUBLIC_ALIAS_TYPE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$PUBLIC_ALIAS_TYPE_LL" -o "$PUBLIC_ALIAS_TYPE_O"
set +e
"$LLVM_LLI" "$PUBLIC_ALIAS_TYPE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected public_alias_type_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_struct_minimal.jiang" > "$MULTI_STRUCT_LL"
rg -q '^%Pair = type \{ i64, i64 \}' "$MULTI_STRUCT_LL"
rg -q 'extractvalue %Pair' "$MULTI_STRUCT_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_STRUCT_LL" -o "$MULTI_STRUCT_O"
set +e
"$LLVM_LLI" "$MULTI_STRUCT_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected multi_file_struct_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_struct_return_minimal.jiang" > "$MULTI_STRUCT_RETURN_LL"
rg -q '^define %Pair @make_pair\(\)' "$MULTI_STRUCT_RETURN_LL"
rg -q 'extractvalue %Pair' "$MULTI_STRUCT_RETURN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_STRUCT_RETURN_LL" -o "$MULTI_STRUCT_RETURN_O"
set +e
"$LLVM_LLI" "$MULTI_STRUCT_RETURN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected multi_file_struct_return_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_struct_array_minimal.jiang" > "$MULTI_STRUCT_ARRAY_LL"
rg -q '^%Buffer = type \{ \[3 x i8\] \}' "$MULTI_STRUCT_ARRAY_LL"
rg -q '^define i8 @middle\(%Buffer %0\)' "$MULTI_STRUCT_ARRAY_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_STRUCT_ARRAY_LL" -o "$MULTI_STRUCT_ARRAY_O"
set +e
"$LLVM_LLI" "$MULTI_STRUCT_ARRAY_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 llvm smoke expected multi_file_struct_array_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_struct_import_minimal.jiang" > "$NS_STRUCT_LL"
rg -q '^%Pair = type \{ i64, i64 \}' "$NS_STRUCT_LL"
rg -q 'extractvalue %Pair' "$NS_STRUCT_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_STRUCT_LL" -o "$NS_STRUCT_O"
set +e
"$LLVM_LLI" "$NS_STRUCT_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected namespaced_struct_import_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_struct_return_minimal.jiang" > "$NS_STRUCT_RETURN_LL"
rg -q '^define %Pair @make_pair\(\)' "$NS_STRUCT_RETURN_LL"
rg -q 'extractvalue %Pair' "$NS_STRUCT_RETURN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_STRUCT_RETURN_LL" -o "$NS_STRUCT_RETURN_O"
set +e
"$LLVM_LLI" "$NS_STRUCT_RETURN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected namespaced_struct_return_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_struct_array_minimal.jiang" > "$NS_STRUCT_ARRAY_LL"
rg -q '^%Buffer = type \{ \[3 x i8\] \}' "$NS_STRUCT_ARRAY_LL"
rg -q '^define i8 @middle\(%Buffer %0\)' "$NS_STRUCT_ARRAY_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_STRUCT_ARRAY_LL" -o "$NS_STRUCT_ARRAY_O"
set +e
"$LLVM_LLI" "$NS_STRUCT_ARRAY_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 llvm smoke expected namespaced_struct_array_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_enum_minimal.jiang" > "$MULTI_ENUM_LL"
rg -q 'icmp eq i64' "$MULTI_ENUM_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_ENUM_LL" -o "$MULTI_ENUM_O"
set +e
"$LLVM_LLI" "$MULTI_ENUM_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 llvm smoke expected multi_file_enum_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_enum_shorthand_minimal.jiang" > "$MULTI_ENUM_SHORTHAND_LL"
rg -q 'icmp eq i64' "$MULTI_ENUM_SHORTHAND_LL"
rg -q 'Mode_read' "$MULTI_ENUM_SHORTHAND_LL" || rg -q 'i64 0' "$MULTI_ENUM_SHORTHAND_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_ENUM_SHORTHAND_LL" -o "$MULTI_ENUM_SHORTHAND_O"
set +e
"$LLVM_LLI" "$MULTI_ENUM_SHORTHAND_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 llvm smoke expected multi_file_enum_shorthand_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_enum_shorthand_arg_minimal.jiang" > "$MULTI_ENUM_SHORTHAND_ARG_LL"
rg -q '^define i64 @code\(i64 %0\)' "$MULTI_ENUM_SHORTHAND_ARG_LL"
rg -q 'call i64 @code\(i64 0\)' "$MULTI_ENUM_SHORTHAND_ARG_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_ENUM_SHORTHAND_ARG_LL" -o "$MULTI_ENUM_SHORTHAND_ARG_O"
set +e
"$LLVM_LLI" "$MULTI_ENUM_SHORTHAND_ARG_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 llvm smoke expected multi_file_enum_shorthand_arg_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_enum_field_shorthand_minimal.jiang" > "$MULTI_ENUM_FIELD_SHORTHAND_LL"
rg -q '^%Config = type \{ i64 \}' "$MULTI_ENUM_FIELD_SHORTHAND_LL"
rg -q 'store %Config zeroinitializer' "$MULTI_ENUM_FIELD_SHORTHAND_LL"
rg -q 'icmp eq i64 %fieldtmp, 0' "$MULTI_ENUM_FIELD_SHORTHAND_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_ENUM_FIELD_SHORTHAND_LL" -o "$MULTI_ENUM_FIELD_SHORTHAND_O"
set +e
"$LLVM_LLI" "$MULTI_ENUM_FIELD_SHORTHAND_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 llvm smoke expected multi_file_enum_field_shorthand_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_enum_value_minimal.jiang" > "$MULTI_ENUM_VALUE_LL"
rg -q 'store i64 7, ptr %mode' "$MULTI_ENUM_VALUE_LL"
rg -q 'ret i32 %main.ret' "$MULTI_ENUM_VALUE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_ENUM_VALUE_LL" -o "$MULTI_ENUM_VALUE_O"
set +e
"$LLVM_LLI" "$MULTI_ENUM_VALUE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 7 ]]; then
    echo "stage2 llvm smoke expected multi_file_enum_value_minimal exit code 7, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_enum_import_minimal.jiang" > "$NS_ENUM_LL"
rg -q 'icmp eq i64' "$NS_ENUM_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_ENUM_LL" -o "$NS_ENUM_O"
set +e
"$LLVM_LLI" "$NS_ENUM_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 llvm smoke expected namespaced_enum_import_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_enum_shorthand_minimal.jiang" > "$NS_ENUM_SHORTHAND_LL"
rg -q 'icmp eq i64' "$NS_ENUM_SHORTHAND_LL"
rg -q 'Mode_read' "$NS_ENUM_SHORTHAND_LL" || rg -q 'i64 0' "$NS_ENUM_SHORTHAND_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_ENUM_SHORTHAND_LL" -o "$NS_ENUM_SHORTHAND_O"
set +e
"$LLVM_LLI" "$NS_ENUM_SHORTHAND_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 llvm smoke expected namespaced_enum_shorthand_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_enum_field_shorthand_minimal.jiang" > "$NS_ENUM_FIELD_SHORTHAND_LL"
rg -q '^%Config = type \{ i64 \}' "$NS_ENUM_FIELD_SHORTHAND_LL"
rg -q 'store %Config zeroinitializer' "$NS_ENUM_FIELD_SHORTHAND_LL"
rg -q 'icmp eq i64 %fieldtmp, 0' "$NS_ENUM_FIELD_SHORTHAND_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_ENUM_FIELD_SHORTHAND_LL" -o "$NS_ENUM_FIELD_SHORTHAND_O"
set +e
"$LLVM_LLI" "$NS_ENUM_FIELD_SHORTHAND_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 llvm smoke expected namespaced_enum_field_shorthand_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_enum_value_minimal.jiang" > "$NS_ENUM_VALUE_LL"
rg -q 'ret i32 3' "$NS_ENUM_VALUE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_ENUM_VALUE_LL" -o "$NS_ENUM_VALUE_O"
set +e
"$LLVM_LLI" "$NS_ENUM_VALUE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 llvm smoke expected namespaced_enum_value_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_slice_return_minimal.jiang" > "$MULTI_SLICE_RETURN_LL"
rg -q '^define %Slice_uint8_t @expose\(\)' "$MULTI_SLICE_RETURN_LL"
rg -q 'extractvalue %Slice_uint8_t .*?, 1' "$MULTI_SLICE_RETURN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_SLICE_RETURN_LL" -o "$MULTI_SLICE_RETURN_O"
set +e
"$LLVM_LLI" "$MULTI_SLICE_RETURN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 llvm smoke expected multi_file_slice_return_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_slice_return_minimal.jiang" > "$NS_SLICE_RETURN_LL"
rg -q '^define %Slice_uint8_t @expose\(\)' "$NS_SLICE_RETURN_LL"
rg -q 'extractvalue %Slice_uint8_t .*?, 1' "$NS_SLICE_RETURN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_SLICE_RETURN_LL" -o "$NS_SLICE_RETURN_O"
set +e
"$LLVM_LLI" "$NS_SLICE_RETURN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 llvm smoke expected namespaced_slice_return_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_slice_index_minimal.jiang" > "$MULTI_SLICE_INDEX_LL"
rg -q 'getelementptr i8, ptr' "$MULTI_SLICE_INDEX_LL"
rg -q 'icmp eq i8' "$MULTI_SLICE_INDEX_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_SLICE_INDEX_LL" -o "$MULTI_SLICE_INDEX_O"
set +e
"$LLVM_LLI" "$MULTI_SLICE_INDEX_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected multi_file_slice_index_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_slice_index_minimal.jiang" > "$NS_SLICE_INDEX_LL"
rg -q 'getelementptr i8, ptr' "$NS_SLICE_INDEX_LL"
rg -q 'icmp eq i8' "$NS_SLICE_INDEX_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_SLICE_INDEX_LL" -o "$NS_SLICE_INDEX_O"
set +e
"$LLVM_LLI" "$NS_SLICE_INDEX_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected namespaced_slice_index_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/pointer_minimal.jiang" > "$POINTER_LL"
rg -q '^define i64 @read\(ptr %0\)' "$POINTER_LL"
rg -q 'store ptr %' "$POINTER_LL"
rg -q 'load i64, ptr %' "$POINTER_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$POINTER_LL" -o "$POINTER_O"
set +e
"$LLVM_LLI" "$POINTER_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected pointer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_pointer_minimal.jiang" > "$MULTI_POINTER_LL"
rg -q '^define i64 @read\(ptr %0\)' "$MULTI_POINTER_LL"
rg -q '^define void @bump\(ptr %0\)' "$MULTI_POINTER_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_POINTER_LL" -o "$MULTI_POINTER_O"
set +e
"$LLVM_LLI" "$MULTI_POINTER_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected multi_file_pointer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_pointer_minimal.jiang" > "$NS_POINTER_LL"
rg -q '^define i64 @read\(ptr %0\)' "$NS_POINTER_LL"
rg -q '^define void @bump\(ptr %0\)' "$NS_POINTER_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_POINTER_LL" -o "$NS_POINTER_O"
set +e
"$LLVM_LLI" "$NS_POINTER_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected namespaced_pointer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_to_slice_arg_minimal.jiang" > "$ARRAY_TO_SLICE_ARG_LL"
rg -q '^define i1 @is_b\(%Slice_uint8_t %0\)' "$ARRAY_TO_SLICE_ARG_LL"
rg -q 'store i8 %indextmp, ptr %probe' "$ARRAY_TO_SLICE_ARG_LL"
rg -q 'insertvalue %Slice_uint8_t' "$ARRAY_TO_SLICE_ARG_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_TO_SLICE_ARG_LL" -o "$ARRAY_TO_SLICE_ARG_O"
set +e
"$LLVM_LLI" "$ARRAY_TO_SLICE_ARG_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected array_to_slice_arg_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_to_slice_local_minimal.jiang" > "$ARRAY_TO_SLICE_LOCAL_LL"
rg -q 'insertvalue %Slice_uint8_t' "$ARRAY_TO_SLICE_LOCAL_LL"
rg -q 'store i8 %indextmp, ptr %probe' "$ARRAY_TO_SLICE_LOCAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_TO_SLICE_LOCAL_LL" -o "$ARRAY_TO_SLICE_LOCAL_O"
set +e
"$LLVM_LLI" "$ARRAY_TO_SLICE_LOCAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected array_to_slice_local_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_to_slice_assign_minimal.jiang" > "$ARRAY_TO_SLICE_ASSIGN_LL"
rg -q 'store %Slice_uint8_t' "$ARRAY_TO_SLICE_ASSIGN_LL"
rg -q 'store i8 %indextmp, ptr %probe' "$ARRAY_TO_SLICE_ASSIGN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_TO_SLICE_ASSIGN_LL" -o "$ARRAY_TO_SLICE_ASSIGN_O"
set +e
"$LLVM_LLI" "$ARRAY_TO_SLICE_ASSIGN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected array_to_slice_assign_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_to_slice_return_minimal.jiang" > "$ARRAY_TO_SLICE_RETURN_LL"
rg -q '^define %Slice_uint8_t @expose\(\)' "$ARRAY_TO_SLICE_RETURN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_TO_SLICE_RETURN_LL" -o "$ARRAY_TO_SLICE_RETURN_O"

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/global_minimal.jiang" > "$GLOBAL_LL"
rg -q '^@answer = global i64 42$' "$GLOBAL_LL"
rg -q '^define i32 @main\(\)' "$GLOBAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$GLOBAL_LL" -o "$GLOBAL_O"
set +e
"$LLVM_LLI" "$GLOBAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected global_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/unary_tuple_global_decl_minimal.jiang" > "$UNARY_TUPLE_GLOBAL_LL"
rg -q '^@answer = global i64 42$' "$UNARY_TUPLE_GLOBAL_LL"
rg -q '^define i32 @main\(\)' "$UNARY_TUPLE_GLOBAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNARY_TUPLE_GLOBAL_LL" -o "$UNARY_TUPLE_GLOBAL_O"
set +e
"$LLVM_LLI" "$UNARY_TUPLE_GLOBAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected unary_tuple_global_decl_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/infer_local_minimal.jiang" > "$INFER_LOCAL_LL"
rg -q '^define i32 @main\(\)' "$INFER_LOCAL_LL"
rg -q 'alloca i64' "$INFER_LOCAL_LL"
rg -q 'store i64 42' "$INFER_LOCAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$INFER_LOCAL_LL" -o "$INFER_LOCAL_O"
set +e
"$LLVM_LLI" "$INFER_LOCAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected infer_local_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/infer_mutable_local_minimal.jiang" > "$INFER_MUTABLE_LOCAL_LL"
rg -q '^define i32 @main\(\)' "$INFER_MUTABLE_LOCAL_LL"
rg -q 'store i64 41' "$INFER_MUTABLE_LOCAL_LL"
rg -q 'store i64 .*%value' "$INFER_MUTABLE_LOCAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$INFER_MUTABLE_LOCAL_LL" -o "$INFER_MUTABLE_LOCAL_O"
set +e
"$LLVM_LLI" "$INFER_MUTABLE_LOCAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected infer_mutable_local_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/unary_tuple_local_decl_minimal.jiang" > "$UNARY_TUPLE_LOCAL_LL"
rg -q '^define i64 @forty_two\(\)' "$UNARY_TUPLE_LOCAL_LL"
rg -q 'call i64 @forty_two\(\)' "$UNARY_TUPLE_LOCAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNARY_TUPLE_LOCAL_LL" -o "$UNARY_TUPLE_LOCAL_O"
set +e
"$LLVM_LLI" "$UNARY_TUPLE_LOCAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected unary_tuple_local_decl_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/unary_tuple_infer_local_decl_minimal.jiang" > "$UNARY_TUPLE_INFER_LOCAL_LL"
rg -q '^define i32 @main\(\)' "$UNARY_TUPLE_INFER_LOCAL_LL"
rg -q 'alloca i64' "$UNARY_TUPLE_INFER_LOCAL_LL"
rg -q 'store i64 42' "$UNARY_TUPLE_INFER_LOCAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNARY_TUPLE_INFER_LOCAL_LL" -o "$UNARY_TUPLE_INFER_LOCAL_O"
set +e
"$LLVM_LLI" "$UNARY_TUPLE_INFER_LOCAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected unary_tuple_infer_local_decl_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/tuple_destructure_minimal.jiang" > "$TUPLE_DESTRUCTURE_LL"
rg -q '^define i32 @main\(\)' "$TUPLE_DESTRUCTURE_LL"
rg -q 'store i64 40' "$TUPLE_DESTRUCTURE_LL"
rg -q 'store i64 2' "$TUPLE_DESTRUCTURE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TUPLE_DESTRUCTURE_LL" -o "$TUPLE_DESTRUCTURE_O"
set +e
"$LLVM_LLI" "$TUPLE_DESTRUCTURE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected tuple_destructure_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/tuple_destructure_infer_minimal.jiang" > "$TUPLE_DESTRUCTURE_INFER_LL"
rg -q '^define i32 @main\(\)' "$TUPLE_DESTRUCTURE_INFER_LL"
rg -q 'store i64 40' "$TUPLE_DESTRUCTURE_INFER_LL"
rg -q 'store i64 2' "$TUPLE_DESTRUCTURE_INFER_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TUPLE_DESTRUCTURE_INFER_LL" -o "$TUPLE_DESTRUCTURE_INFER_O"
set +e
"$LLVM_LLI" "$TUPLE_DESTRUCTURE_INFER_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected tuple_destructure_infer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/tuple_destructure_mutable_infer_minimal.jiang" > "$TUPLE_DESTRUCTURE_MUTABLE_INFER_LL"
rg -q '^define i32 @main\(\)' "$TUPLE_DESTRUCTURE_MUTABLE_INFER_LL"
rg -q 'store i64 1' "$TUPLE_DESTRUCTURE_MUTABLE_INFER_LL"
rg -q 'store i64 41' "$TUPLE_DESTRUCTURE_MUTABLE_INFER_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TUPLE_DESTRUCTURE_MUTABLE_INFER_LL" -o "$TUPLE_DESTRUCTURE_MUTABLE_INFER_O"
set +e
"$LLVM_LLI" "$TUPLE_DESTRUCTURE_MUTABLE_INFER_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected tuple_destructure_mutable_infer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/tuple_destructure_global_minimal.jiang" > "$TUPLE_DESTRUCTURE_GLOBAL_LL"
rg -q '^@left = global i64 40$' "$TUPLE_DESTRUCTURE_GLOBAL_LL"
rg -q '^@right = global i64 2$' "$TUPLE_DESTRUCTURE_GLOBAL_LL"
rg -q '^define i32 @main\(\)' "$TUPLE_DESTRUCTURE_GLOBAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TUPLE_DESTRUCTURE_GLOBAL_LL" -o "$TUPLE_DESTRUCTURE_GLOBAL_O"
set +e
"$LLVM_LLI" "$TUPLE_DESTRUCTURE_GLOBAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected tuple_destructure_global_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/tuple_destructure_return_minimal.jiang" > "$TUPLE_DESTRUCTURE_RETURN_LL"
rg -q '^define .*@pair\(\)' "$TUPLE_DESTRUCTURE_RETURN_LL"
rg -q 'extractvalue .* 0' "$TUPLE_DESTRUCTURE_RETURN_LL"
rg -q 'extractvalue .* 1' "$TUPLE_DESTRUCTURE_RETURN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TUPLE_DESTRUCTURE_RETURN_LL" -o "$TUPLE_DESTRUCTURE_RETURN_O"
set +e
"$LLVM_LLI" "$TUPLE_DESTRUCTURE_RETURN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected tuple_destructure_return_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/tuple_value_minimal.jiang" > "$TUPLE_VALUE_LL"
rg -q '^%Tuple_[0-9][0-9]* = type \{ i64, i64 \}$' "$TUPLE_VALUE_LL"
rg -q '^@pair = global %Tuple_[0-9][0-9]* \{ i64 40, i64 2 \}$' "$TUPLE_VALUE_LL"
rg -q 'extractvalue %Tuple_[0-9][0-9]* .* 0' "$TUPLE_VALUE_LL"
rg -q 'extractvalue %Tuple_[0-9][0-9]* .* 1' "$TUPLE_VALUE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TUPLE_VALUE_LL" -o "$TUPLE_VALUE_O"
set +e
"$LLVM_LLI" "$TUPLE_VALUE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected tuple_value_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/ternary_minimal.jiang" > "$TERNARY_LL"
rg -q 'br i1 ' "$TERNARY_LL"
rg -q 'ternary.then' "$TERNARY_LL"
rg -q 'ternary.else' "$TERNARY_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TERNARY_LL" -o "$TERNARY_O"
set +e
"$LLVM_LLI" "$TERNARY_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected ternary_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/ternary_enum_minimal.jiang" > "$TERNARY_ENUM_LL"
rg -q 'ternary.then' "$TERNARY_ENUM_LL"
rg -q 'ternary.else' "$TERNARY_ENUM_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TERNARY_ENUM_LL" -o "$TERNARY_ENUM_O"
set +e
"$LLVM_LLI" "$TERNARY_ENUM_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected ternary_enum_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/tuple_return_minimal.jiang" > "$TUPLE_RETURN_LL"
rg -q '^%Tuple_[0-9][0-9]* = type \{ i64, i64 \}$' "$TUPLE_RETURN_LL"
rg -q '^define %Tuple_[0-9][0-9]* @pair\(\)' "$TUPLE_RETURN_LL"
rg -q 'call %Tuple_[0-9][0-9]* @pair\(\)' "$TUPLE_RETURN_LL"
rg -q 'extractvalue %Tuple_[0-9][0-9]* .* 0' "$TUPLE_RETURN_LL"
rg -q 'extractvalue %Tuple_[0-9][0-9]* .* 1' "$TUPLE_RETURN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TUPLE_RETURN_LL" -o "$TUPLE_RETURN_O"
set +e
"$LLVM_LLI" "$TUPLE_RETURN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected tuple_return_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/tuple_infer_minimal.jiang" > "$TUPLE_INFER_LL"
rg -q '^%Tuple_[0-9][0-9]* = type \{ i64, i64 \}$' "$TUPLE_INFER_LL"
rg -q 'alloca %Tuple_[0-9][0-9]*' "$TUPLE_INFER_LL"
rg -q 'extractvalue %Tuple_[0-9][0-9]* .* 0' "$TUPLE_INFER_LL"
rg -q 'extractvalue %Tuple_[0-9][0-9]* .* 1' "$TUPLE_INFER_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$TUPLE_INFER_LL" -o "$TUPLE_INFER_O"
set +e
"$LLVM_LLI" "$TUPLE_INFER_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected tuple_infer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/infer_global_minimal.jiang" > "$INFER_GLOBAL_LL"
rg -q '^@answer = global i64 42$' "$INFER_GLOBAL_LL"
rg -q '^define i32 @main\(\)' "$INFER_GLOBAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$INFER_GLOBAL_LL" -o "$INFER_GLOBAL_O"
set +e
"$LLVM_LLI" "$INFER_GLOBAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected infer_global_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/optional_minimal.jiang" > "$OPTIONAL_LL"
rg -q '^%Optional_[0-9][0-9]*_t = type \{ i1, i64 \}$' "$OPTIONAL_LL"
rg -q 'call %Optional_[0-9][0-9]*_t @pass\(%Optional_[0-9][0-9]*_t' "$OPTIONAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$OPTIONAL_LL" -o "$OPTIONAL_O"
set +e
"$LLVM_LLI" "$OPTIONAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected optional_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/optional_null_compare_minimal.jiang" > "$OPTIONAL_NULL_COMPARE_LL"
rg -q '^%Optional_[0-9][0-9]*_t = type \{ i1, i64 \}$' "$OPTIONAL_NULL_COMPARE_LL"
rg -q 'extractvalue %Optional_[0-9][0-9]*_t' "$OPTIONAL_NULL_COMPARE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$OPTIONAL_NULL_COMPARE_LL" -o "$OPTIONAL_NULL_COMPARE_O"
set +e
"$LLVM_LLI" "$OPTIONAL_NULL_COMPARE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected optional_null_compare_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/optional_coalesce_minimal.jiang" > "$OPTIONAL_COALESCE_LL"
rg -q 'coalesce.some' "$OPTIONAL_COALESCE_LL"
rg -q 'coalesce.none' "$OPTIONAL_COALESCE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$OPTIONAL_COALESCE_LL" -o "$OPTIONAL_COALESCE_O"
set +e
"$LLVM_LLI" "$OPTIONAL_COALESCE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected optional_coalesce_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/optional_if_narrow_minimal.jiang" > "$OPTIONAL_IF_NARROW_LL"
rg -q 'extractvalue %Optional_[0-9][0-9]*_t' "$OPTIONAL_IF_NARROW_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$OPTIONAL_IF_NARROW_LL" -o "$OPTIONAL_IF_NARROW_O"
set +e
"$LLVM_LLI" "$OPTIONAL_IF_NARROW_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected optional_if_narrow_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/optional_else_narrow_minimal.jiang" > "$OPTIONAL_ELSE_NARROW_LL"
rg -q 'extractvalue %Optional_[0-9][0-9]*_t' "$OPTIONAL_ELSE_NARROW_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$OPTIONAL_ELSE_NARROW_LL" -o "$OPTIONAL_ELSE_NARROW_O"
set +e
"$LLVM_LLI" "$OPTIONAL_ELSE_NARROW_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected optional_else_narrow_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/optional_nested_array_minimal.jiang" > "$OPTIONAL_NESTED_ARRAY_LL"
rg -q '^%Optional_[0-9][0-9]*_t = type \{ i1, i64 \}$' "$OPTIONAL_NESTED_ARRAY_LL"
rg -q '\[3 x \[2 x %Optional_[0-9][0-9]*_t\]\]' "$OPTIONAL_NESTED_ARRAY_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$OPTIONAL_NESTED_ARRAY_LL" -o "$OPTIONAL_NESTED_ARRAY_O"
set +e
"$LLVM_LLI" "$OPTIONAL_NESTED_ARRAY_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected optional_nested_array_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/optional_some_pattern_minimal.jiang" > "$OPTIONAL_SOME_PATTERN_LL"
rg -q 'extractvalue' "$OPTIONAL_SOME_PATTERN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$OPTIONAL_SOME_PATTERN_LL" -o "$OPTIONAL_SOME_PATTERN_O"
set +e
"$LLVM_LLI" "$OPTIONAL_SOME_PATTERN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected optional_some_pattern_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/optional_switch_pattern_minimal.jiang" > "$OPTIONAL_SWITCH_PATTERN_LL"
rg -q 'extractvalue' "$OPTIONAL_SWITCH_PATTERN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$OPTIONAL_SWITCH_PATTERN_LL" -o "$OPTIONAL_SWITCH_PATTERN_O"
set +e
"$LLVM_LLI" "$OPTIONAL_SWITCH_PATTERN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected optional_switch_pattern_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/optional_chain_member_minimal.jiang" > "$OPTIONAL_CHAIN_MEMBER_LL"
rg -q 'extractvalue' "$OPTIONAL_CHAIN_MEMBER_LL"
rg -q 'and i1' "$OPTIONAL_CHAIN_MEMBER_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$OPTIONAL_CHAIN_MEMBER_LL" -o "$OPTIONAL_CHAIN_MEMBER_O"
set +e
"$LLVM_LLI" "$OPTIONAL_CHAIN_MEMBER_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected optional_chain_member_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/optional_chain_index_minimal.jiang" > "$OPTIONAL_CHAIN_INDEX_LL"
rg -q 'extractvalue' "$OPTIONAL_CHAIN_INDEX_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$OPTIONAL_CHAIN_INDEX_LL" -o "$OPTIONAL_CHAIN_INDEX_O"
set +e
"$LLVM_LLI" "$OPTIONAL_CHAIN_INDEX_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 40 ]]; then
    echo "stage2 llvm smoke expected optional_chain_index_minimal exit code 40, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/optional_chain_nested_pure_base_minimal.jiang" > "$OPTIONAL_CHAIN_NESTED_PURE_BASE_LL"
rg -q 'extractvalue' "$OPTIONAL_CHAIN_NESTED_PURE_BASE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$OPTIONAL_CHAIN_NESTED_PURE_BASE_LL" -o "$OPTIONAL_CHAIN_NESTED_PURE_BASE_O"
set +e
"$LLVM_LLI" "$OPTIONAL_CHAIN_NESTED_PURE_BASE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected optional_chain_nested_pure_base_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/mutable_qualifier_minimal.jiang" > "$MUTABLE_QUALIFIER_LL"
rg -q '^define i64 @bump\(i64 %0\)' "$MUTABLE_QUALIFIER_LL"
rg -q 'store i64 %0, ptr %value' "$MUTABLE_QUALIFIER_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MUTABLE_QUALIFIER_LL" -o "$MUTABLE_QUALIFIER_O"
set +e
"$LLVM_LLI" "$MUTABLE_QUALIFIER_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected mutable_qualifier_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/mutable_array_qualifier_minimal.jiang" > "$MUTABLE_ARRAY_QUALIFIER_LL"
rg -q '^define i32 @main\(\)' "$MUTABLE_ARRAY_QUALIFIER_LL"
rg -q '\[2 x i64\]' "$MUTABLE_ARRAY_QUALIFIER_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MUTABLE_ARRAY_QUALIFIER_LL" -o "$MUTABLE_ARRAY_QUALIFIER_O"
set +e
"$LLVM_LLI" "$MUTABLE_ARRAY_QUALIFIER_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected mutable_array_qualifier_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_optional_field_minimal.jiang" > "$STRUCT_OPTIONAL_FIELD_LL"
rg -q '^%Optional_[0-9][0-9]*_t = type \{ i1, i64 \}$' "$STRUCT_OPTIONAL_FIELD_LL"
rg -q '^%User = type \{ i64, %Optional_[0-9][0-9]*_t \}$' "$STRUCT_OPTIONAL_FIELD_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_OPTIONAL_FIELD_LL" -o "$STRUCT_OPTIONAL_FIELD_O"
set +e
"$LLVM_LLI" "$STRUCT_OPTIONAL_FIELD_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected struct_optional_field_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_optional_field_omitted_minimal.jiang" > "$STRUCT_OPTIONAL_FIELD_OMITTED_LL"
rg -q '^%Optional_[0-9][0-9]*_t = type \{ i1, i64 \}$' "$STRUCT_OPTIONAL_FIELD_OMITTED_LL"
rg -q '^%User = type \{ i64, %Optional_[0-9][0-9]*_t \}$' "$STRUCT_OPTIONAL_FIELD_OMITTED_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_OPTIONAL_FIELD_OMITTED_LL" -o "$STRUCT_OPTIONAL_FIELD_OMITTED_O"
set +e
"$LLVM_LLI" "$STRUCT_OPTIONAL_FIELD_OMITTED_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 18 ]]; then
    echo "stage2 llvm smoke expected struct_optional_field_omitted_minimal exit code 18, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_init_minimal.jiang" > "$STRUCT_INIT_LL"
rg -q '_User_init_into' "$STRUCT_INIT_LL"
rg -q '_User_init' "$STRUCT_INIT_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_INIT_LL" -o "$STRUCT_INIT_O"
set +e
"$LLVM_LLI" "$STRUCT_INIT_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected struct_init_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_constructor_sugar_minimal.jiang" > "$STRUCT_CONSTRUCTOR_SUGAR_LL"
rg -q '_User_init' "$STRUCT_CONSTRUCTOR_SUGAR_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_CONSTRUCTOR_SUGAR_LL" -o "$STRUCT_CONSTRUCTOR_SUGAR_O"
set +e
"$LLVM_LLI" "$STRUCT_CONSTRUCTOR_SUGAR_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected struct_constructor_sugar_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_new_constructor_minimal.jiang" > "$STRUCT_NEW_CONSTRUCTOR_LL"
rg -q '_User_new' "$STRUCT_NEW_CONSTRUCTOR_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_NEW_CONSTRUCTOR_LL" -o "$STRUCT_NEW_CONSTRUCTOR_O"
set +e
"$LLVM_LLI" "$STRUCT_NEW_CONSTRUCTOR_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected struct_new_constructor_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_init_with_defaults_minimal.jiang" > "$STRUCT_INIT_DEFAULTS_LL"
rg -q '^%User = type \{ i64, i64 \}$' "$STRUCT_INIT_DEFAULTS_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_INIT_DEFAULTS_LL" -o "$STRUCT_INIT_DEFAULTS_O"
set +e
"$LLVM_LLI" "$STRUCT_INIT_DEFAULTS_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 18 ]]; then
    echo "stage2 llvm smoke expected struct_init_with_defaults_minimal exit code 18, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_init_mutable_default_override_minimal.jiang" > "$STRUCT_INIT_MUTABLE_DEFAULT_OVERRIDE_LL"
rg -q '^%User = type \{ i64, i64 \}$' "$STRUCT_INIT_MUTABLE_DEFAULT_OVERRIDE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_INIT_MUTABLE_DEFAULT_OVERRIDE_LL" -o "$STRUCT_INIT_MUTABLE_DEFAULT_OVERRIDE_O"
set +e
"$LLVM_LLI" "$STRUCT_INIT_MUTABLE_DEFAULT_OVERRIDE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 19 ]]; then
    echo "stage2 llvm smoke expected struct_init_mutable_default_override_minimal exit code 19, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_init_optional_omitted_minimal.jiang" > "$STRUCT_INIT_OPTIONAL_OMITTED_LL"
rg -q '^%Optional_[0-9][0-9]*_t = type \{ i1, i64 \}$' "$STRUCT_INIT_OPTIONAL_OMITTED_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_INIT_OPTIONAL_OMITTED_LL" -o "$STRUCT_INIT_OPTIONAL_OMITTED_O"
set +e
"$LLVM_LLI" "$STRUCT_INIT_OPTIONAL_OMITTED_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 llvm smoke expected struct_init_optional_omitted_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_init_branch_complete_minimal.jiang" > "$STRUCT_INIT_BRANCH_COMPLETE_LL"
rg -q '^%Pair = type \{ i64, i64 \}$' "$STRUCT_INIT_BRANCH_COMPLETE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_INIT_BRANCH_COMPLETE_LL" -o "$STRUCT_INIT_BRANCH_COMPLETE_O"
set +e
"$LLVM_LLI" "$STRUCT_INIT_BRANCH_COMPLETE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected struct_init_branch_complete_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/empty_tuple_return_minimal.jiang" > "$EMPTY_TUPLE_RETURN_LL"
rg -q '^define void @noop\(\)' "$EMPTY_TUPLE_RETURN_LL"
rg -q 'ret void' "$EMPTY_TUPLE_RETURN_LL"
rg -q 'call void @noop\(\)' "$EMPTY_TUPLE_RETURN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$EMPTY_TUPLE_RETURN_LL" -o "$EMPTY_TUPLE_RETURN_O"
set +e
"$LLVM_LLI" "$EMPTY_TUPLE_RETURN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 0 ]]; then
    echo "stage2 llvm smoke expected empty_tuple_return_minimal exit code 0, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/empty_tuple_bare_return_minimal.jiang" > "$EMPTY_TUPLE_BARE_RETURN_LL"
rg -q '^define void @noop\(\)' "$EMPTY_TUPLE_BARE_RETURN_LL"
rg -q 'ret void' "$EMPTY_TUPLE_BARE_RETURN_LL"
rg -q 'call void @noop\(\)' "$EMPTY_TUPLE_BARE_RETURN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$EMPTY_TUPLE_BARE_RETURN_LL" -o "$EMPTY_TUPLE_BARE_RETURN_O"
set +e
"$LLVM_LLI" "$EMPTY_TUPLE_BARE_RETURN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 0 ]]; then
    echo "stage2 llvm smoke expected empty_tuple_bare_return_minimal exit code 0, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/unary_tuple_return_minimal.jiang" > "$UNARY_TUPLE_RETURN_LL"
rg -q '^define i64 @forty_two\(\)' "$UNARY_TUPLE_RETURN_LL"
rg -q 'call i64 @forty_two\(\)' "$UNARY_TUPLE_RETURN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UNARY_TUPLE_RETURN_LL" -o "$UNARY_TUPLE_RETURN_O"
set +e
"$LLVM_LLI" "$UNARY_TUPLE_RETURN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected unary_tuple_return_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

echo "stage2 llvm smoke passed"
