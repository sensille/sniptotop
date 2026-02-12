#!/bin/bash
# Shared helper functions for sniptotop integration tests.
# Source this file from each test script.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SNIPTOTOP="$PROJECT_DIR/sniptotop"
TEST_HELPER="$SCRIPT_DIR/test_helper"

HELPER_PID=""
SNIPTOTOP_PID=""
TEST_TMPDIR=""
SNIPPET_WID=""
WM_PID=""

# Ensure a window manager is running (required by sniptotop for WM_STATE).
ensure_wm() {
	# Check if a WM is already running by looking for a WM selection owner
	if xprop -root _NET_SUPPORTING_WM_CHECK >/dev/null 2>&1; then
		return 0
	fi
	if command -v xfwm4 >/dev/null 2>&1; then
		xfwm4 --replace &
		WM_PID=$!
		sleep 0.5
	elif command -v openbox >/dev/null 2>&1; then
		openbox &
		WM_PID=$!
		sleep 0.5
	fi
}

# Set up a temporary HOME so sniptotop writes state there.
setup_tmpdir() {
	TEST_TMPDIR=$(mktemp -d /tmp/sniptotop-test.XXXXXX)
	mkdir -p "$TEST_TMPDIR/.config/sniptotop"
	export HOME="$TEST_TMPDIR"
}

# Start the test helper window. Sets HELPER_PID and HELPER_WID.
start_helper() {
	"$TEST_HELPER" &
	HELPER_PID=$!
	# Wait for the helper window to appear
	HELPER_WID=""
	for i in $(seq 1 30); do
		HELPER_WID=$(xdotool search --name "^sniptotop-test-target$" 2>/dev/null | head -1 || true)
		if [ -n "$HELPER_WID" ]; then
			break
		fi
		sleep 0.1
	done
	if [ -z "$HELPER_WID" ]; then
		echo "FAIL: helper window not found"
		cleanup
		exit 1
	fi
}

# Start sniptotop. Extra args passed through. Sets SNIPTOTOP_PID.
start_sniptotop() {
	ensure_wm
	"$SNIPTOTOP" "$@" &
	SNIPTOTOP_PID=$!
	# Wait for main window to appear
	for i in $(seq 1 30); do
		if xdotool search --name "^sniptotop$" >/dev/null 2>&1; then
			break
		fi
		sleep 0.1
	done
	sleep 0.2
}

# Wait for a window whose name exactly matches NAME (3s timeout).
# Outputs the window ID.
wait_for_window() {
	local name="$1"
	local wid=""
	for i in $(seq 1 30); do
		wid=$(xdotool search --name "^${name}$" 2>/dev/null | head -1 || true)
		if [ -n "$wid" ]; then
			echo "$wid"
			return 0
		fi
		sleep 0.1
	done
	return 1
}

# Wait for a window with WM_CLASS matching pattern (3s timeout).
# Outputs window IDs (one per line).
wait_for_class() {
	local cls="$1"
	local wids=""
	for i in $(seq 1 30); do
		wids=$(xdotool search --class "$cls" 2>/dev/null || true)
		if [ -n "$wids" ]; then
			echo "$wids"
			return 0
		fi
		sleep 0.1
	done
	return 1
}

# Click at screen coordinates X Y with left button.
click_at() {
	local x=$1 y=$2
	xdotool mousemove --sync "$x" "$y"
	sleep 0.05
	xdotool mousedown 1
	sleep 0.05
	xdotool mouseup 1
	sleep 0.1
}

# Right-click drag from (X1,Y1) to (X2,Y2).
rclick_drag() {
	local x1=$1 y1=$2 x2=$3 y2=$4
	xdotool mousemove --sync "$x1" "$y1"
	sleep 0.05
	xdotool mousedown 3
	sleep 0.05
	xdotool mousemove --sync "$x2" "$y2"
	sleep 0.05
	xdotool mouseup 3
	sleep 0.1
}

# Left-click drag from (X1,Y1) to (X2,Y2).
lclick_drag() {
	local x1=$1 y1=$2 x2=$3 y2=$4
	xdotool mousemove --sync "$x1" "$y1"
	sleep 0.05
	xdotool mousedown 1
	sleep 0.05
	xdotool mousemove --sync "$x2" "$y2"
	sleep 0.05
	xdotool mouseup 1
	sleep 0.1
}

# Press a key. Optional second arg is a window ID to focus first.
press_key() {
	local key="$1"
	local wid="${2:-}"
	if [ -n "$wid" ]; then
		xdotool windowfocus --sync "$wid" 2>/dev/null || true
		sleep 0.1
	fi
	xdotool key "$key"
	sleep 0.1
}

# Create a snippet from the helper window.
# Full interaction sequence:
#   1. Move windows so they don't overlap
#   2. Click main window (enter select mode)
#   3. Click helper window (select target) — pointer gets grabbed
#   4. Drag rectangle on helper
# Sets SNIPPET_WID to the new snippet window ID.
create_snippet() {
	local main_wid
	main_wid=$(wait_for_window "sniptotop") || fail "sniptotop main window not found"

	# Move windows to known non-overlapping positions
	# Main window to top-left, helper to right side
	xdotool windowmove --sync "$main_wid" 10 10
	xdotool windowmove --sync "$HELPER_WID" 400 300
	sleep 0.3

	# Record all windows before snippet creation
	local before_wids
	before_wids=$(xdotool search --onlyvisible --name "" 2>/dev/null | sort || true)

	# Re-read main window position after move
	local main_info
	main_info=$(xwininfo -id "$main_wid" 2>/dev/null)
	local mx my mw mh
	mx=$(echo "$main_info" | grep "Absolute upper-left X:" | awk '{print $NF}')
	my=$(echo "$main_info" | grep "Absolute upper-left Y:" | awk '{print $NF}')
	mw=$(echo "$main_info" | grep "Width:" | awk '{print $NF}')
	mh=$(echo "$main_info" | grep "Height:" | awk '{print $NF}')

	# Step 1: Click main window center to enter select mode
	local cx=$(( mx + mw / 2 ))
	local cy=$(( my + mh / 2 ))
	click_at "$cx" "$cy"
	sleep 0.3

	# Step 2+3: Single press-drag-release on helper window.
	# The PRESS selects the target, the DRAG defines the rectangle,
	# the RELEASE creates the view. This is one continuous gesture.
	local helper_info
	helper_info=$(xwininfo -id "$HELPER_WID" 2>/dev/null)
	local hx hy hw hh
	hx=$(echo "$helper_info" | grep "Absolute upper-left X:" | awk '{print $NF}')
	hy=$(echo "$helper_info" | grep "Absolute upper-left Y:" | awk '{print $NF}')
	hw=$(echo "$helper_info" | grep "Width:" | awk '{print $NF}')
	hh=$(echo "$helper_info" | grep "Height:" | awk '{print $NF}')
	local drag_x1=$(( hx + 20 ))
	local drag_y1=$(( hy + 20 ))
	local drag_x2=$(( hx + 80 ))
	local drag_y2=$(( hy + 80 ))
	lclick_drag "$drag_x1" "$drag_y1" "$drag_x2" "$drag_y2"
	sleep 0.5

	# Find the new snippet window by diffing visible windows before/after.
	# View windows don't have WM_CLASS or _NET_WM_PID.
	local after_wids
	after_wids=$(xdotool search --onlyvisible --name "" 2>/dev/null | sort || true)

	SNIPPET_WID=""
	for wid in $after_wids; do
		if ! echo "$before_wids" | grep -qx "$wid"; then
			SNIPPET_WID="$wid"
		fi
	done

	if [ -z "$SNIPPET_WID" ]; then
		fail "Could not find new snippet window"
	fi
}

# Get window position. Outputs "X Y".
get_window_pos() {
	local wid=$1
	local info
	info=$(xwininfo -id "$wid" 2>/dev/null)
	local x y
	x=$(echo "$info" | grep "Absolute upper-left X:" | awk '{print $NF}')
	y=$(echo "$info" | grep "Absolute upper-left Y:" | awk '{print $NF}')
	echo "$x $y"
}

# Get window size. Outputs "W H".
get_window_size() {
	local wid=$1
	local info
	info=$(xwininfo -id "$wid" 2>/dev/null)
	local w h
	w=$(echo "$info" | grep "Width:" | awk '{print $NF}')
	h=$(echo "$info" | grep "Height:" | awk '{print $NF}')
	echo "$w $h"
}

# Get pixel color at (X,Y) in window WID. Outputs hex like "FF0000".
get_pixel_color() {
	local wid=$1 x=$2 y=$3
	local tmpfile
	tmpfile=$(mktemp /tmp/sniptotop-pixel.XXXXXX.png)
	# Use import from ImageMagick to capture a single pixel
	import -window "$wid" -crop "1x1+${x}+${y}" -depth 8 "$tmpfile" 2>/dev/null
	local color
	color=$(convert "$tmpfile" txt:- 2>/dev/null \
		| tail -1 | grep -oP '#[0-9A-Fa-f]{6}' | head -1 | tr -d '#' | tr 'a-f' 'A-F')
	rm -f "$tmpfile"
	echo "$color"
}

# Assert two values are equal.
assert_eq() {
	local actual="$1" expected="$2" msg="$3"
	if [ "$actual" = "$expected" ]; then
		echo "  ok: $msg ($actual)"
	else
		echo "  FAIL: $msg (expected '$expected', got '$actual')"
		return 1
	fi
}

# Assert actual is within delta of expected.
assert_near() {
	local actual=$1 expected=$2 delta=$3 msg="$4"
	local diff=$(( actual - expected ))
	if [ $diff -lt 0 ]; then diff=$(( -diff )); fi
	if [ $diff -le "$delta" ]; then
		echo "  ok: $msg ($actual ~ $expected ±$delta)"
	else
		echo "  FAIL: $msg (expected ~$expected ±$delta, got $actual)"
		return 1
	fi
}

# Assert window exists (is mapped).
assert_window_exists() {
	local wid=$1 msg="$2"
	if xwininfo -id "$wid" >/dev/null 2>&1; then
		echo "  ok: $msg (window $wid exists)"
	else
		echo "  FAIL: $msg (window $wid does not exist)"
		return 1
	fi
}

# Assert window no longer exists.
assert_window_gone() {
	local wid=$1 msg="$2"
	# Give it a moment to close
	sleep 0.2
	if xwininfo -id "$wid" >/dev/null 2>&1; then
		echo "  FAIL: $msg (window $wid still exists)"
		return 1
	else
		echo "  ok: $msg (window $wid gone)"
	fi
}

# Clean up all test processes and temp files.
cleanup() {
	if [ -n "$SNIPTOTOP_PID" ]; then
		kill "$SNIPTOTOP_PID" 2>/dev/null || true
		wait "$SNIPTOTOP_PID" 2>/dev/null || true
		SNIPTOTOP_PID=""
	fi
	if [ -n "$HELPER_PID" ]; then
		kill "$HELPER_PID" 2>/dev/null || true
		wait "$HELPER_PID" 2>/dev/null || true
		HELPER_PID=""
	fi
	if [ -n "$WM_PID" ]; then
		kill "$WM_PID" 2>/dev/null || true
		wait "$WM_PID" 2>/dev/null || true
		WM_PID=""
	fi
	if [ -n "$TEST_TMPDIR" ]; then
		rm -rf "$TEST_TMPDIR"
		TEST_TMPDIR=""
	fi
}

# Print FAIL message, clean up, and exit.
fail() {
	echo "  FAIL: $1"
	cleanup
	exit 1
}

# Trap to clean up on exit
trap cleanup EXIT
