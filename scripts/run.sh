#!/usr/bin/env bash
#
# Benchmark runner for arts-benchmarks
# Runs all 19 installed benchmarks with appropriate arguments and datasets
#

set -euo pipefail

script_dir="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd -- "${script_dir}/.." && pwd -P)"

if [[ "$PWD" != "$repo_root" ]]; then
	printf 'Please run scripts/run.sh from the repository root (%s)\n' "$repo_root" >&2
	exit 1
fi

bin_dir="${repo_root}/.install/bin/ocr-apps"
lib_dir="${repo_root}/.install/lib"
log_dir="${repo_root}/logs/apps"

mkdir -p "$log_dir"

if [[ ! -d "$bin_dir" ]]; then
	printf 'Missing %s. Run "cmake --build build --target install" first.\n' "$bin_dir" >&2
	exit 1
fi

export ARTS_CONFIG="${repo_root}/arts.cfg"
if [[ ! -f "$ARTS_CONFIG" ]]; then
	printf 'Expected arts.cfg at %s. Copy scripts/arts.cfg or create a symlink first.\n' "$ARTS_CONFIG" >&2
	exit 1
fi

export LD_LIBRARY_PATH="${lib_dir}:${LD_LIBRARY_PATH:-}"

log_info() { printf '[INFO] %s\n' "$*"; }
log_warn() { printf '[WARN] %s\n' "$*" >&2; }

declare -A STATS=([passed]=0 [failed]=0 [skipped]=0)

sanitize_label() {
	local label="$1"
	label="${label// /_}"
	label="${label//[^A-Za-z0-9_.-]/_}"
	printf '%s' "$label"
}

mark_skip() {
	((STATS[skipped]+=1))
}

# Run a benchmark case
# Usage: run_case <binary_path_relative_to_bin_dir> <label> [args...]
run_case() {
	local binary="$1"
	local label="$2"
	shift 2

	local exe="${bin_dir}/${binary}"
	local safe_label
	safe_label="$(sanitize_label "$label")"
	# Create a safe log filename from the binary path
	local log_binary="${binary//\//__}"
	local logfile="${log_dir}/${log_binary}__${safe_label}.log"

	if [[ ! -x "$exe" ]]; then
		log_warn "Skipping ${binary} (${label}): missing executable ${exe}"
		mark_skip
		return
	fi

	local -a args=("$@")

	{
		printf 'Timestamp: %s\n' "$(date --iso-8601=seconds)"
		printf 'Command:'
		printf ' %q' "$exe" "${args[@]}"
		printf '\n'
		printf 'Working Directory: %s\n' "$PWD"
		printf 'ARTS_CONFIG=%s\n' "$ARTS_CONFIG"
		printf 'LD_LIBRARY_PATH=%s\n' "$LD_LIBRARY_PATH"
		printf -- '--- stdout/stderr ---\n'
	} >"$logfile"

	set +e
	"$exe" "${args[@]}" &>>"$logfile"
	local exit_code=$?
	set -e

	if [[ $exit_code -eq 0 ]]; then
		log_info "PASS ${binary} (${label})"
		((STATS[passed]+=1))
	else
		log_warn "FAIL ${binary} (${label}) [exit ${exit_code}] — see ${logfile}"
		((STATS[failed]+=1))
	fi

	return 0
}

# Run a benchmark case from a specific working directory
# Usage: run_case_in_dir <working_dir> <binary_path_relative_to_bin_dir> <label> [args...]
run_case_in_dir() {
	local work_dir="$1"
	local binary="$2"
	local label="$3"
	shift 3

	if [[ ! -d "$work_dir" ]]; then
		log_warn "Skipping ${binary} (${label}): working directory ${work_dir} missing"
		mark_skip
		return
	fi

	# Copy arts.cfg to the working directory if it doesn't exist there
	if [[ ! -f "${work_dir}/arts.cfg" ]]; then
		cp "$ARTS_CONFIG" "${work_dir}/arts.cfg"
	fi

	pushd "$work_dir" > /dev/null
	run_case "$binary" "$label" "$@"
	popd > /dev/null
}

log_info "Running all 19 benchmarks from ${bin_dir}"

###############################################################################
# 1. basicIO
# Usage: ./basicIO <offset> <size> <fileName>
###############################################################################
log_info "=== 1. basicIO ==="
basic_io_dir="${bin_dir}/basicIO"
for input_file in input_10.txt input_1000000.txt; do
	size="${input_file#input_}"
	size="${size%.txt}"
	data_path="${basic_io_dir}/${input_file}"
	if [[ -f "$data_path" ]]; then
		run_case "basicIO/basicIO" "input_${size}" 1 "$size" "$data_path"
	else
		log_warn "Skipping BasicIO dataset ${input_file}: ${data_path} not found"
		mark_skip
	fi
done

###############################################################################
# 2. cholesky (2 variants: cholesky, cholesky_mkl)
# Usage: ./cholesky --ds <size> --ts <tile_size> --fi <input_file> [--ps <0|1>] [--ol <0-5>]
###############################################################################
log_info "=== 2. cholesky ==="
declare -A CHOLESKY_TILE_SIZES=(
	[10]=5
	[50]=10
	[100]=10
	[200]=20
	[300]=30
	[400]=20
	[500]=20
	[1000]=100
	[2000]=100
)
cholesky_dir="${bin_dir}/cholesky/datasets"
for size in "${!CHOLESKY_TILE_SIZES[@]}"; do
	matrix_path="${cholesky_dir}/m_${size}.in"
	if [[ ! -f "$matrix_path" ]]; then
		log_warn "Skipping Cholesky dataset ${size}: ${matrix_path} missing"
		mark_skip
		continue
	fi
	args=(--ds "$size" --ts "${CHOLESKY_TILE_SIZES[$size]}" --fi "$matrix_path")
	run_case "cholesky/cholesky" "m${size}" "${args[@]}"
	run_case "cholesky/cholesky_mkl" "m${size}" "${args[@]}"
done

###############################################################################
# 3. CoMD
# Usage: ./CoMD [-x nx] [-y ny] [-z nz] [-N steps] [-n period] [-d pot_dir] ...
###############################################################################
log_info "=== 3. CoMD ==="
comd_dataset="${bin_dir}/CoMD/datasets/pots"
if [[ -d "$comd_dataset" ]]; then
	run_case "CoMD/CoMD" "default" -x 5 -y 5 -z 5 -d "$comd_dataset"
else
	log_warn "Skipping CoMD: dataset directory ${comd_dataset} missing"
	mark_skip
fi

###############################################################################
# 4. fft
# Usage: ./fft <power>  (N = 2^power)
###############################################################################
log_info "=== 4. fft ==="
run_case "fft/fft" "power_10" 10
run_case "fft/fft" "power_15" 15

###############################################################################
# 5. fibonacci
# Usage: ./fibonacci [num]  (default: 10)
###############################################################################
log_info "=== 5. fibonacci ==="
run_case "fibonacci/fibonacci" "n10" 10
run_case "fibonacci/fibonacci" "n15" 15

###############################################################################
# 6. globalsum (3 variants)
# Usage: ./globalsum_* (no arguments, uses compile-time constants)
###############################################################################
log_info "=== 6. globalsum ==="
run_case "globalsum/globalsum_cgShim" "default"
run_case "globalsum/globalsum_cgNoShim" "default"
run_case "globalsum/globalsum_pcg" "default"

###############################################################################
# 7. hpcg
# Usage: ./hpcg (no arguments, uses compile-time constants)
###############################################################################
# log_info "=== 7. hpcg ==="
# run_case "hpcg/hpcg" "default"

###############################################################################
# 8. hpgmg
# Usage: ./hpgmg <log2_box_dim> <target_boxes>
###############################################################################
log_info "=== 8. hpgmg ==="
run_case "hpgmg/hpgmg" "4x8" 4 8
run_case "hpgmg/hpgmg" "5x4" 5 4

###############################################################################
# 9. npb-cg
# Usage: ./npb_cg [-t class] [-b blocking]
# Classes: T, S, W, A, B, C, D, E
###############################################################################
log_info "=== 9. npb-cg ==="
run_case "npb-cg/npb_cg" "class_S" -t S
run_case "npb-cg/npb_cg" "class_W" -t W

###############################################################################
# 10. p2p
# Usage: ./p2p (no arguments, uses compile-time constants)
###############################################################################
log_info "=== 10. p2p ==="
run_case "p2p/p2p" "default"

###############################################################################
# 11. quicksort
# Usage: ./quicksort (no arguments, uses compile-time constants)
###############################################################################
log_info "=== 11. quicksort ==="
run_case "quicksort/quicksort" "default"

###############################################################################
# 12. reduction
# Usage: ./reduction (no arguments, uses compile-time constants)
###############################################################################
# log_info "=== 12. reduction ==="
# run_case "reduction/reduction" "default"

###############################################################################
# 13. sar (6 variants: tiny, small, medium, large, huge, problem_size_scaling)
# Usage: Run from variant directory (reads Parameters.txt from cwd)
###############################################################################
log_info "=== 13. sar ==="
for variant in tiny small medium large huge; do
	sar_dir="${bin_dir}/sar/${variant}"
	if [[ -d "$sar_dir" && -f "${sar_dir}/Parameters.txt" ]]; then
		run_case_in_dir "$sar_dir" "sar/${variant}/${variant}" "$variant"
	else
		log_warn "Skipping SAR ${variant}: directory or Parameters.txt missing"
		mark_skip
	fi
done
# problem_size_scaling variant uses Parameter0.txt - Parameter9.txt
sar_pss_dir="${bin_dir}/sar/problem_size_scaling"
if [[ -d "$sar_pss_dir" && -f "${sar_pss_dir}/Parameter0.txt" ]]; then
	run_case_in_dir "$sar_pss_dir" "sar/problem_size_scaling/problem_size_scaling" "problem_size_scaling"
else
	log_warn "Skipping SAR problem_size_scaling: directory or Parameter files missing"
	mark_skip
fi

###############################################################################
# 14. skel (printf)
# Usage: ./printf (no arguments, "Hello World" example)
###############################################################################
log_info "=== 14. skel ==="
run_case "skel/printf" "default"

###############################################################################
# 15. smithwaterman
# Usage: ./smithwaterman <tileWidth> <tileHeight> <file1> <file2> <scoreFile>
###############################################################################
log_info "=== 15. smithwaterman ==="
smithwaterman_dir="${bin_dir}/smithwaterman/datasets"
for label in tiny small medium medium-large; do
	s1="${smithwaterman_dir}/string1-${label}.txt"
	s2="${smithwaterman_dir}/string2-${label}.txt"
	score="${smithwaterman_dir}/score-${label}.txt"
	if [[ -f "$s1" && -f "$s2" && -f "$score" ]]; then
		run_case "smithwaterman/smithwaterman" "${label}" 10 10 "$s1" "$s2" "$score"
	else
		log_warn "Skipping Smith-Waterman ${label}: missing dataset files"
		mark_skip
	fi
done

###############################################################################
# 16. Stencil1D (7 variants)
# intel-chandra: ./Stencil1D <npoints> <nranks> <ntimesteps> <ntimesteps_sync> <itimestep0> <halo_radius>
# intel-david variants: no arguments (compile-time constants)
###############################################################################
log_info "=== 16. Stencil1D ==="
run_case "Stencil1D/intel-chandra/Stencil1D" "default" 1000 100 500 5 -1 -1

stencil1d_david_variants=(
	stencil1Dguid
	stencil1DguidPI
	stencil1Donce
	stencil1DoncePI
	stencil1Dsticky
	stencil1DstickyLG
)
for variant in "${stencil1d_david_variants[@]}"; do
	run_case "Stencil1D/intel-david/${variant}" "$variant"
done

###############################################################################
# 17. Stencil2D
# Usage: ./Stencil2D <npoints> <nranks> <ntimesteps> <ntimesteps_sync> <itimestep0> <halo_radius>
###############################################################################
log_info "=== 17. Stencil2D ==="
run_case "Stencil2D/Stencil2D" "default" 6 4 4 2 -1 -1

###############################################################################
# 18. triangle
# Usage: ./triangle (no arguments, 14-peg puzzle solver)
###############################################################################
log_info "=== 18. triangle ==="
run_case "triangle/triangle" "default"

###############################################################################
# 19. XSBench
# Usage: ./XSBench [-t threads] [-s size] [-g gridpoints] [-l lookups]
###############################################################################
log_info "=== 19. XSBench ==="
run_case "XSBench/XSBench" "small" -s small -l 100000
run_case "XSBench/XSBench" "large" -s large -l 100000

###############################################################################
# Summary
###############################################################################
total=$((STATS[passed] + STATS[failed] + STATS[skipped]))
printf '\n========================================\n'
printf 'Run summary: %d total | %d passed | %d failed | %d skipped\n' \
	"$total" "${STATS[passed]}" "${STATS[failed]}" "${STATS[skipped]}"
printf '========================================\n'

if (( STATS[failed] > 0 )); then
	exit 1
fi

