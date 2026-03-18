#!/bin/bash

# Jiang 编译器自动化批量测试脚本

set -e # 遇到系统级错误立即退出

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUTPUT_C="$BUILD_DIR/out.c"
TESTS_DIR="$PROJECT_ROOT/tests"

echo "--- 1. 编译 Jiang 编译器 ---"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

echo -e "\n--- 2. 运行所有单元测试 ---"

# 关闭 set -e，以便我们自己处理测试用例的失败
set +e

SUCCESS_COUNT=0
FAIL_COUNT=0
SKIPPED_COUNT=0

for TEST_FILE in "$TESTS_DIR"/*.jiang; do
    BASENAME=$(basename "$TEST_FILE")
    echo -n "正在测试 $BASENAME ... "

    # 清理上一次的产物
    rm -f "$OUTPUT_C"
    
    # 运行编译器并捕获输出
    ./jiangc "$TEST_FILE" > compiler_output.log 2>&1

    # 针对预期失败的用例 (文件名以 fail_ 开头)
    if [[ "$BASENAME" == fail_* ]]; then
        if grep -qiE "Semantic Error|failed due to" compiler_output.log; then
            echo -e "\033[32m通过 (成功拦截预期内的错误)\033[0m"
            SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        else
            echo -e "\033[31m失败 (预期会编译报错，但编译器未报错)\033[0m"
            FAIL_COUNT=$((FAIL_COUNT + 1))
            echo "--- 编译器输出 ---"
            cat compiler_output.log
            echo "------------------"
        fi
    # 针对正常的正确用例
    else
        if [ ! -f "$OUTPUT_C" ]; then
            echo -e "\033[31m失败 (未成功生成 out.c)\033[0m"
            FAIL_COUNT=$((FAIL_COUNT + 1))
            echo "--- 编译器输出 ---"
            cat compiler_output.log
            echo "------------------"
        else
            OUTPUT_BIN="$BUILD_DIR/${BASENAME%.jiang}_bin"
            # 尝试编译生成的 C 代码
            if gcc "$OUTPUT_C" -o "$OUTPUT_BIN" > gcc_output.log 2>&1; then
                # 运行生成的二进制文件以测试崩溃/运行时错误
                # 这里目前不测试输出的一致性，只检查是否正常运行
                if "$OUTPUT_BIN" > bin_output.log 2>&1; then
                    echo -e "\033[32m通过\033[0m"
                    SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
                else
                    echo -e "\033[31m失败 (生成的二进制文件运行时崩溃)\033[0m"
                    FAIL_COUNT=$((FAIL_COUNT + 1))
                    echo "--- 运行输出 ---"
                    cat bin_output.log
                    echo "----------------"
                fi
            else
                echo -e "\033[31m失败 (生成的 C 代码无法被 GCC 编译)\033[0m"
                FAIL_COUNT=$((FAIL_COUNT + 1))
                echo "--- GCC 输出 ---"
                cat gcc_output.log
                echo "----------------"
            fi
        fi
    fi
done

echo -e "\n--- 测试总结 ---"
echo -e "\033[32m总计通过: $SUCCESS_COUNT\033[0m"
if [ "$FAIL_COUNT" -gt 0 ]; then
    echo -e "\033[31m总计失败: $FAIL_COUNT\033[0m"
    exit 1
else
    echo -e "\033[32m🎉 所有测试均已通过！\033[0m"
    exit 0
fi
