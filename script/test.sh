#!/bin/bash

# Jiang 编译器自动化批量测试脚本

set -e # 遇到系统级错误立即退出

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
TESTS_DIR="$PROJECT_ROOT/tests"

echo "--- 1. 编译 Jiang 编译器 ---"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

echo -e "\n--- 1.5. 运行 Stage0 HIR 冒烟测试 ---"
bash "$PROJECT_ROOT/script/stage0_hir_smoke.sh"

echo -e "\n--- 1.6. 运行 Build System 冒烟测试 ---"
bash "$PROJECT_ROOT/script/build_system_smoke.sh"

echo -e "\n--- 1.65. 运行 Build System 错误路径冒烟测试 ---"
bash "$PROJECT_ROOT/script/build_system_error_smoke.sh"

echo -e "\n--- 1.66. 运行 Runtime 边界冒烟测试 ---"
bash "$PROJECT_ROOT/script/runtime_boundary_smoke.sh"

echo -e "\n--- 1.67. 运行 LLVM Spike 冒烟测试 ---"
bash "$PROJECT_ROOT/script/llvm_spike_smoke.sh"

echo -e "\n--- 1.68. 运行 LLVM Backend 回归测试 ---"
bash "$PROJECT_ROOT/script/test_llvm_backend.sh"

echo -e "\n--- 1.69. 运行 Bootstrap LLVM 冒烟测试 ---"
bash "$PROJECT_ROOT/script/bootstrap_llvm_smoke.sh"

echo -e "\n--- 1.695. 运行默认 LLVM 后端候选评估 ---"
bash "$PROJECT_ROOT/script/evaluate_default_llvm.sh"

echo -e "\n--- 1.7. 运行 Stage1 完成验收冒烟测试 ---"
bash "$PROJECT_ROOT/script/stage1_complete_smoke.sh"

echo -e "\n--- 1.75. 运行 Stage2 emit-c 冒烟测试 ---"
bash "$PROJECT_ROOT/script/stage2_emit_c_smoke.sh"

echo -e "\n--- 1.76. 运行 Stage2 support 冒烟测试 ---"
bash "$PROJECT_ROOT/script/stage2_support_smoke.sh"

echo -e "\n--- 1.77. 运行 Stage2 error 冒烟测试 ---"
bash "$PROJECT_ROOT/script/stage2_error_smoke.sh"

echo -e "\n--- 2. 运行所有单元测试 ---"

# 关闭 set -e，以便我们自己处理测试用例的失败
set +e

SUCCESS_COUNT=0
FAIL_COUNT=0

cd "$PROJECT_ROOT"

for TEST_FILE in "$TESTS_DIR"/*.jiang; do
    BASENAME=$(basename "$TEST_FILE")
    TEST_NAME="${BASENAME%.jiang}"
    echo -n "正在测试 $BASENAME ... "

    # 清理上一次的产物
    rm -f "$BUILD_DIR/$TEST_NAME"
    rm -f "$BUILD_DIR/out.c"
    
    # 运行编译器（它会自动调用 gcc 并在 build/ 目录下生成二进制文件）
    "$BUILD_DIR/jiangc" "$TEST_FILE" > "$BUILD_DIR/compiler_output.log" 2>&1
    COMPILE_STATUS=$?

    # 针对预期失败的用例 (文件名以 fail_ 开头)
    if [[ "$BASENAME" == fail_* ]]; then
        if [ $COMPILE_STATUS -ne 0 ]; then
            echo -e "\033[32m通过 (成功拦截预期内的错误)\033[0m"
            SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        else
            echo -e "\033[31m失败 (预期会编译报错，但编译器成功退出了)\033[0m"
            FAIL_COUNT=$((FAIL_COUNT + 1))
        fi
    # 针对正常的正确用例
    else
        if [ $COMPILE_STATUS -ne 0 ]; then
            echo -e "\033[31m失败 (编译过程中出错)\033[0m"
            FAIL_COUNT=$((FAIL_COUNT + 1))
            echo "--- 编译器输出 ---"
            cat "$BUILD_DIR/compiler_output.log"
            echo "------------------"
        else
            OUTPUT_BIN="$BUILD_DIR/$TEST_NAME"
            if [ -f "$OUTPUT_BIN" ]; then
                # 运行生成的二进制文件
                if "$OUTPUT_BIN" > "$BUILD_DIR/bin_output.log" 2>&1; then
                    echo -e "\033[32m通过\033[0m"
                    SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
                else
                    if [[ "$TEST_NAME" == "assert_fail" ]]; then
                        echo -e "\033[32m通过 (成功触发预期内的断言失败)\033[0m"
                        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
                    else
                        echo -e "\033[31m失败 (二进制文件运行崩溃)\033[0m"
                        FAIL_COUNT=$((FAIL_COUNT + 1))
                        echo "--- 运行输出 ---"
                        cat "$BUILD_DIR/bin_output.log"
                        echo "----------------"
                    fi
                fi
            else
                echo -e "\033[31m失败 (未找到预期的二进制文件 $OUTPUT_BIN)\033[0m"
                FAIL_COUNT=$((FAIL_COUNT + 1))
            fi
        fi
    fi
done

# 清理根目录下的残留（如果有）
rm -f "$PROJECT_ROOT/union_test"

echo -e "\n--- 测试总结 ---"
echo -e "\033[32m总计通过: $SUCCESS_COUNT\033[0m"
if [ "$FAIL_COUNT" -gt 0 ]; then
    echo -e "\033[31m总计失败: $FAIL_COUNT\033[0m"
    exit 1
else
    echo -e "\033[32m🎉 所有测试均已通过！产物全部位于 build/ 目录。\033[0m"
    exit 0
fi
