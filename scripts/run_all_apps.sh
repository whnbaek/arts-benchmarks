#!/bin/bash
# Don't exit on errors - we want to run all apps and report results
# set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

export artsConfig="$SCRIPT_DIR/arts.cfg"
export LD_LIBRARY_PATH="$PROJECT_ROOT/install/lib:$LD_LIBRARY_PATH"

APPS_ROOT="$PROJECT_ROOT/external/ocr-apps/apps"
PASSED=0
FAILED=0
SKIPPED=0
FAILED_APPS=""
SKIPPED_APPS=""

# Mode: "debug" (verbose) or "benchmark" (timing, minimal output)
MODE="${1:-debug}"

# Arrays to store benchmark results
declare -a BENCH_APPS
declare -a BENCH_TIMES
declare -a BENCH_STATUS

usage() {
    echo "Usage: $0 [debug|benchmark]"
    echo "  debug     - Verbose output with detailed logs (default)"
    echo "  benchmark - Measure execution time, minimal output, show results table"
    exit 1
}

if [[ "$MODE" != "debug" && "$MODE" != "benchmark" ]]; then
    usage
fi

run_app() {
    local app_dir="$1"
    local exec_name="$2"
    shift 2
    
    # Parse args and optional verification command (separated by --verify)
    local args=""
    local verify_cmd=""
    
    while [[ $# -gt 0 ]]; do
        if [[ "$1" == "--verify" ]]; then
            shift
            verify_cmd="$*"
            break
        else
            if [[ -z "$args" ]]; then
                args="$1"
            else
                args="$args $1"
            fi
            shift
        fi
    done
    
    local install_dir="$APPS_ROOT/$app_dir/install/x86"
    local exec_path="$install_dir/$exec_name"

    if [[ "$MODE" == "debug" ]]; then
        echo "========================================"
        echo "Running: artsConfig=$artsConfig $exec_path $args"
        echo "Directory: $app_dir"
        echo "========================================"
    fi

    if [ ! -f "$exec_path" ]; then
        if [[ "$MODE" == "debug" ]]; then
            echo "SKIPPED: Executable not found: $exec_path"
            echo ""
        fi
        SKIPPED=$((SKIPPED + 1))
        SKIPPED_APPS="$SKIPPED_APPS $exec_name"
        BENCH_APPS+=("$exec_name")
        BENCH_TIMES+=("-")
        BENCH_STATUS+=("SKIPPED")
        return 0
    fi

    cd "$install_dir"
    
    # Measure wall clock time in both modes
    local start_time=$(date +%s.%N)
    local exec_result=0
    
    if [[ "$MODE" == "debug" ]]; then
        "$exec_path" $args || exec_result=$?
    else
        printf "Running %-40s ... " "$exec_name"
        "$exec_path" $args > /dev/null 2>&1 || exec_result=$?
    fi
    
    local end_time=$(date +%s.%N)
    local elapsed=$(echo "$end_time - $start_time" | bc)
    
    # Determine pass/fail
    local passed=false
    local fail_reason=""
    
    if [[ $exec_result -ne 0 ]]; then
        fail_reason="execution error (exit code: $exec_result)"
    elif [[ -n "$verify_cmd" ]]; then
        # Run verification command
        if [[ "$MODE" == "debug" ]]; then
            echo "Running verification: $verify_cmd"
        fi
        if eval "$verify_cmd" > /dev/null 2>&1; then
            passed=true
        else
            fail_reason="verification failed"
        fi
    else
        passed=true
    fi
    
    # Report results
    if [[ "$passed" == "true" ]]; then
        if [[ "$MODE" == "debug" ]]; then
            printf "SUCCESS: %s completed (%.3fs)\n" "$exec_name" "$elapsed"
        else
            printf "PASS (%.3fs)\n" "$elapsed"
        fi
        PASSED=$((PASSED + 1))
        BENCH_APPS+=("$exec_name")
        BENCH_TIMES+=("$(printf "%.3f" "$elapsed")")
        BENCH_STATUS+=("PASS")
    else
        if [[ "$MODE" == "debug" ]]; then
            printf "FAILED: %s - %s (%.3fs)\n" "$exec_name" "$fail_reason" "$elapsed"
        else
            printf "FAIL (%.3fs)\n" "$elapsed"
        fi
        FAILED=$((FAILED + 1))
        FAILED_APPS="$FAILED_APPS $exec_name"
        BENCH_APPS+=("$exec_name")
        BENCH_TIMES+=("$(printf "%.3f" "$elapsed")")
        BENCH_STATUS+=("FAIL")
    fi
    
    if [[ "$MODE" == "debug" ]]; then
        echo ""
    fi
}

run_app "basicIO/ocr" "basicIO" 0 10 "$APPS_ROOT/basicIO/ocr/input_10.txt" --verify diff -b "$APPS_ROOT/basicIO/ocr/input_10.txt" "$APPS_ROOT/basicIO/ocr/install/x86/basicIO_output.txt"
run_app "basicIO/ocr" "basicIO" 0 1000000 "$APPS_ROOT/basicIO/ocr/input_1000000.txt" --verify diff -b "$APPS_ROOT/basicIO/ocr/input_1000000.txt" "$APPS_ROOT/basicIO/ocr/install/x86/basicIO_output.txt"
run_app "cholesky/ocr" "cholesky" --ds 50 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_50.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_50.txt" "$APPS_ROOT/cholesky/ocr/install/x86/cholesky.out"
run_app "cholesky/ocr" "cholesky" --ds 100 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_100.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_100.txt" "$APPS_ROOT/cholesky/ocr/install/x86/cholesky.out"
run_app "cholesky/ocr" "cholesky" --ds 200 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_200.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_200.txt" "$APPS_ROOT/cholesky/ocr/install/x86/cholesky.out"
run_app "cholesky/ocr" "cholesky" --ds 300 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_300.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_300.txt" "$APPS_ROOT/cholesky/ocr/install/x86/cholesky.out"
run_app "cholesky/ocr" "cholesky" --ds 400 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_400.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_400.txt" "$APPS_ROOT/cholesky/ocr/install/x86/cholesky.out"
run_app "cholesky/ocr" "cholesky" --ds 500 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_500.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_500.txt" "$APPS_ROOT/cholesky/ocr/install/x86/cholesky.out"
run_app "cholesky/ocr" "cholesky" --ds 1000 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_1000.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_1000.txt" "$APPS_ROOT/cholesky/ocr/install/x86/cholesky.out"
run_app "cholesky/ocr" "cholesky" --ds 2000 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_2000.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_2000.txt" "$APPS_ROOT/cholesky/ocr/install/x86/cholesky.out"
run_app "cholesky/ocr-mkl" "ocr_mkl_cholesky" --ds 50 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_50.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_50.txt" "$APPS_ROOT/cholesky/ocr-mkl/install/x86/ocr_mkl_cholesky.out"
run_app "cholesky/ocr-mkl" "ocr_mkl_cholesky" --ds 100 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_100.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_100.txt" "$APPS_ROOT/cholesky/ocr-mkl/install/x86/ocr_mkl_cholesky.out"
run_app "cholesky/ocr-mkl" "ocr_mkl_cholesky" --ds 200 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_200.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_200.txt" "$APPS_ROOT/cholesky/ocr-mkl/install/x86/ocr_mkl_cholesky.out"
run_app "cholesky/ocr-mkl" "ocr_mkl_cholesky" --ds 300 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_300.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_300.txt" "$APPS_ROOT/cholesky/ocr-mkl/install/x86/ocr_mkl_cholesky.out"
run_app "cholesky/ocr-mkl" "ocr_mkl_cholesky" --ds 400 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_400.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_400.txt" "$APPS_ROOT/cholesky/ocr-mkl/install/x86/ocr_mkl_cholesky.out"
run_app "cholesky/ocr-mkl" "ocr_mkl_cholesky" --ds 500 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_500.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_500.txt" "$APPS_ROOT/cholesky/ocr-mkl/install/x86/ocr_mkl_cholesky.out"
run_app "cholesky/ocr-mkl" "ocr_mkl_cholesky" --ds 1000 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_1000.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_1000.txt" "$APPS_ROOT/cholesky/ocr-mkl/install/x86/ocr_mkl_cholesky.out"
run_app "cholesky/ocr-mkl" "ocr_mkl_cholesky" --ds 2000 --ts 50 --fi "$APPS_ROOT/cholesky/datasets/m_2000.in" --ol 1 --verify diff -b "$APPS_ROOT/cholesky/datasets/cholesky_out_2000.txt" "$APPS_ROOT/cholesky/ocr-mkl/install/x86/ocr_mkl_cholesky.out"
# ! Currently only comd-ocrd works correctly, others don't
# run_app "CoMD/refactored/ocr/intel-chandra" "comd-ocr2" -x 5 -y 5 -z 5
# run_app "CoMD/refactored/ocr/intel-chandra-tiled" "comd" -x 5 -y 5 -z 5
run_app "CoMD/refactored/ocr/sdsc" "comd-ocrd" -x 24 -y 24 -z 24
# run_app "CoMD/refactored/ocr/sdsc2" "comd-ocr2" -x 5 -y 5 -z 5
run_app "curvefit/ocr/intel" "curveFit" 4 .01 .5 10000
run_app "examples/cache-offset" "cache-offset" 
run_app "examples/highbw" "highbw"
run_app "examples/multigen" "multigen" 
run_app "examples/multigen_2" "multigen" 
run_app "examples/task-priorities" "task-priorities" 
run_app "examples/testlibs" "testlibs"
run_app "examples/xeonNumaSize" "xeonNumaSize" -dcpu
run_app "fft/ocr" "fft" 10
run_app "fibonacci/ocr" "fib" 10
run_app "globalsum/refactored/ocr/intel" "cgShim"
run_app "globalsum/refactored/ocr/intel" "cgNoShim"
run_app "globalsum/refactored/ocr/intel" "pcg"
run_app "graph500" "graph500" 20 16 8 8
run_app "hpcg/refactored/ocr/intel" "hpcg" 3 4 5 16 50 0
run_app "hpcg/refactored/ocr/intel-Eager" "hpcgEager" 2 2 2 64 10 0
run_app "hpcg/refactored/ocr/intel-Eager-Collective" "hpcgEagerRedevt" 2 2 2 64 10 0
run_app "hpgmg/refactored/ocr/sdsc" "hpgmg" 6 64
run_app "kernels/dbctrl/ocr" "dbctrl" 100 1000 256
run_app "kernels/prodcon/ocr" "prodcon"

# TODO: make other applications work
# run_app "LCS/refactored/ocr/intel-jesmin-lcs_all_db_distributed" "lcs" 16 8
# run_app "LCS/refactored/ocr/intel-jesmin-lcs_distributed_ST_datablocks" "lcs" 16 8
# run_app "LCS/refactored/ocr/intel-jesmin-lcs_shared_datablocks" "lcs" 16 8
# run_app "nekbone/refactored/ocr_src" "z_nekbone_inOcr" 
# run_app "npb-cg/sdsc-ocr" "cg" -t T
# run_app "nqueens/refactored/ocr" "nqueens" 13 5
# run_app "p2p/refactored/ocr/intel" "p2p" 3 4 5 16 50 0
# run_app "printf/ocr" "printf" 
# run_app "quicksort/ocr" "quicksort" 
# run_app "reduction/refactored/ocr/intel" "driver" 4 4 10
# run_app "reduction/refactored/ocr/intel-chandra" "driver" 16
# run_app "RSBench/refactored/ocr/intel" "RSBench" -d -s small -l 10000
# run_app "RSBench/refactored/ocr/intel-sharedDB" "RSBench" -d -s small -t 4 -l 10000
# run_app "sar/ocr/tiny" "sscp" 0
# run_app "Stencil1D/refactored/ocr/intel-chandra" "stencil_1d" 6 2 4
# run_app "Stencil1D/refactored/ocr/intel-david" "stencil" 4 100 10
# run_app "Stencil2D/refactored/ocr/intel-chandra" "stencil_2d" 6 4 4
# run_app "Stencil2D/refactored/ocr/intel-channelEVTs" "stencil_2d" 6 4 4
# run_app "Stencil2D/refactored/ocr/intel-jiri" "spmd_stencil2d" 
# run_app "tempest/refactored/ocr/intel-bryan" "tempestCommunication" 
# run_app "triangle/refactored/ocr/intel" "triangle" 
# run_app "XSBench/refactored/ocr/intel" "XSBench" -s small -g 100 -l 10000
# run_app "XSBench/refactored/ocr/intel-sharedDB" "XSBench" -s small -g 100 -t 4 -l 10000

if [[ "$MODE" == "debug" ]]; then
    echo "========================================"
    echo "SUMMARY"
    echo "========================================"
    echo "Passed:  $PASSED"
    echo "Failed:  $FAILED"
    echo "Skipped: $SKIPPED"
    if [ $FAILED -gt 0 ]; then
        echo "Failed apps:$FAILED_APPS"
    fi
    if [ $SKIPPED -gt 0 ]; then
        echo "Skipped apps:$SKIPPED_APPS"
    fi
    echo "========================================"
else
    # Benchmark mode: print results table
    echo ""
    echo "================================================================================"
    echo "                           BENCHMARK RESULTS"
    echo "================================================================================"
    printf "%-40s %12s %10s\n" "Application" "Time (s)" "Status"
    echo "--------------------------------------------------------------------------------"
    for i in "${!BENCH_APPS[@]}"; do
        printf "%-40s %12s %10s\n" "${BENCH_APPS[$i]}" "${BENCH_TIMES[$i]}" "${BENCH_STATUS[$i]}"
    done
    echo "--------------------------------------------------------------------------------"
    printf "%-40s %12s %10s\n" "TOTAL" "" "P:$PASSED F:$FAILED S:$SKIPPED"
    echo "================================================================================"
    if [ $FAILED -gt 0 ]; then
        echo "Failed apps:$FAILED_APPS"
    fi
    if [ $SKIPPED -gt 0 ]; then
        echo "Skipped apps:$SKIPPED_APPS"
    fi
fi
exit $FAILED
