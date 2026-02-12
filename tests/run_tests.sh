#!/bin/bash
# Main test runner for sniptotop integration tests.
# Starts Xvfb, builds binaries, runs all test_*.sh, reports results.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Find a free display number
find_free_display() {
	for d in $(seq 99 120); do
		if ! [ -e "/tmp/.X${d}-lock" ]; then
			echo ":$d"
			return
		fi
	done
	echo ":99"
}

XVFB_DISPLAY=$(find_free_display)
XVFB_PID=""
WM_PID=""

cleanup_runner() {
	if [ -n "$WM_PID" ]; then
		kill "$WM_PID" 2>/dev/null || true
		wait "$WM_PID" 2>/dev/null || true
	fi
	if [ -n "$XVFB_PID" ]; then
		kill "$XVFB_PID" 2>/dev/null || true
		wait "$XVFB_PID" 2>/dev/null || true
	fi
}
trap cleanup_runner EXIT

# --- Build ---
echo "Building sniptotop..."
make -C "$PROJECT_DIR" sniptotop || { echo "FAIL: build sniptotop"; exit 1; }

echo "Building test_helper..."
gcc "$SCRIPT_DIR/helper.c" -Wall -g -lxcb -o "$SCRIPT_DIR/test_helper" \
	|| { echo "FAIL: build test_helper"; exit 1; }

# --- Start Xvfb ---
echo "Starting Xvfb on $XVFB_DISPLAY..."
Xvfb "$XVFB_DISPLAY" -screen 0 1280x720x24 +extension DAMAGE &
XVFB_PID=$!
sleep 0.5

# Verify Xvfb is running
if ! kill -0 "$XVFB_PID" 2>/dev/null; then
	echo "FAIL: Xvfb did not start"
	exit 1
fi

export DISPLAY="$XVFB_DISPLAY"

# --- Start window manager ---
# sniptotop requires a WM (needs WM_STATE property on windows)
if command -v xfwm4 >/dev/null 2>&1; then
	xfwm4 --replace &
	WM_PID=$!
	sleep 0.5
	echo "Window manager started (xfwm4)"
elif command -v openbox >/dev/null 2>&1; then
	openbox &
	WM_PID=$!
	sleep 0.5
	echo "Window manager started (openbox)"
else
	echo "WARNING: No window manager found (xfwm4, openbox). Tests may fail."
fi

# --- Run tests ---
passed=0
failed=0
failures=""

tests=$(find "$SCRIPT_DIR" -name 'test_*.sh' -type f | sort)

for test_script in $tests; do
	test_name=$(basename "$test_script" .sh)
	echo ""
	echo "--- $test_name ---"

	if bash "$test_script"; then
		echo "PASS: $test_name"
		passed=$((passed + 1))
	else
		echo "FAIL: $test_name"
		failed=$((failed + 1))
		failures="$failures $test_name"
	fi
done

# --- Summary ---
total=$((passed + failed))
echo ""
echo "================================"
echo "$passed/$total tests passed"
if [ $failed -gt 0 ]; then
	echo "Failed:$failures"
	exit 1
else
	echo "All tests passed."
	exit 0
fi
