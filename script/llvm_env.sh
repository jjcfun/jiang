#!/bin/bash

JIANG_LLVM_REQUIRED_SERIES="${JIANG_LLVM_REQUIRED_SERIES:-21.1}"
JIANG_LLVM_ROOT="${JIANG_LLVM_ROOT:-$HOME/.jiang/toolchains/llvm-$JIANG_LLVM_REQUIRED_SERIES}"

resolve_llvm_config() {
    if [ -n "${LLVM_CONFIG:-}" ]; then
        if [ -x "$LLVM_CONFIG" ]; then
            echo "$LLVM_CONFIG"
            return 0
        fi
        echo "LLVM_CONFIG is set but not executable: $LLVM_CONFIG" >&2
        return 1
    fi

    if [ -x "$JIANG_LLVM_ROOT/bin/llvm-config" ]; then
        echo "$JIANG_LLVM_ROOT/bin/llvm-config"
        return 0
    fi

    if command -v llvm-config >/dev/null 2>&1; then
        command -v llvm-config
        return 0
    fi

    echo "llvm-config not found; expected LLVM $JIANG_LLVM_REQUIRED_SERIES.x" >&2
    echo "set JIANG_LLVM_ROOT to a pinned LLVM toolchain root, or set LLVM_CONFIG explicitly" >&2
    return 1
}

export_llvm_env() {
    LLVM_CONFIG="$(resolve_llvm_config)" || return 1
    LLVM_VERSION_RAW="$("$LLVM_CONFIG" --version)"

    case "$LLVM_VERSION_RAW" in
        "$JIANG_LLVM_REQUIRED_SERIES".*)
            ;;
        *)
            echo "LLVM version mismatch: expected $JIANG_LLVM_REQUIRED_SERIES.x, got $LLVM_VERSION_RAW" >&2
            echo "resolved llvm-config: $LLVM_CONFIG" >&2
            return 1
            ;;
    esac

    LLVM_BINDIR="$("$LLVM_CONFIG" --bindir)"
    LLVM_CLANG="$LLVM_BINDIR/clang"
    LLVM_LLI="$LLVM_BINDIR/lli"
    LLVM_OPT="$LLVM_BINDIR/opt"
    LLVM_CFLAGS="$("$LLVM_CONFIG" --cflags)"
    LLVM_LDFLAGS="$("$LLVM_CONFIG" --ldflags)"
    LLVM_LIBS="$("$LLVM_CONFIG" --libs core analysis --system-libs)"

    export JIANG_LLVM_REQUIRED_SERIES
    export JIANG_LLVM_ROOT
    export LLVM_CONFIG
    export LLVM_VERSION_RAW
    export LLVM_BINDIR
    export LLVM_CLANG
    export LLVM_LLI
    export LLVM_OPT
    export LLVM_CFLAGS
    export LLVM_LDFLAGS
    export LLVM_LIBS
}
