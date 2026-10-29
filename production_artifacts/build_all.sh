#!/usr/bin/env bash
set -e

# build_all.sh - Unified multi-compiler build and test verification runner
# Usage: ./production_artifacts/build_all.sh [custom | default] [release | debug | deep]

TYPE="custom"
MODE="release"

if [ "$#" -eq 1 ]; then
    arg=$(echo "$1" | tr '[:upper:]' '[:lower:]')
    if [ "$arg" = "release" ] || [ "$arg" = "debug" ] || [ "$arg" = "deep" ] || [ "$arg" = "debug_deep" ]; then
        MODE="$arg"
    elif [ "$arg" = "custom" ] || [ "$arg" = "default" ]; then
        TYPE="$arg"
    fi
elif [ "$#" -eq 2 ]; then
    TYPE=$(echo "$1" | tr '[:upper:]' '[:lower:]')
    MODE=$(echo "$2" | tr '[:upper:]' '[:lower:]')
fi

if [ "$TYPE" = "custom" ]; then
    P_TYPE="Custom"
else
    P_TYPE="Default"
fi

if [ "$MODE" = "debug" ]; then
    P_MODE="Debug"
elif [ "$MODE" = "deep" ] || [ "$MODE" = "debug_deep" ]; then
    P_MODE="Debug_Deep"
else
    P_MODE="Release"
fi

run_workflow() {
    local preset=$1

    echo -e "\n-------------------------------------------------------------"
    echo "Executing Workflow: $preset"
    echo "-------------------------------------------------------------"

    if cmake --workflow --preset "$preset"; then
        echo -e "[SUCCESS] Workflow $preset completed successfully."
        return 0
    else
        echo -e "\n[ERROR] Workflow $preset failed."
        return 1
    fi
}

echo "================================================================="
echo "  Starting Verification ($P_TYPE - $P_MODE): GNU -> Clang"
echo "================================================================="

run_workflow "GNU_${P_TYPE}_${P_MODE}_Verify"
run_workflow "Clang_${P_TYPE}_${P_MODE}_Verify"

echo -e "\n================================================================="
echo "  All Compiler Verification Workflows Processed Successfully!"
echo "================================================================="
