#!/bin/bash
# Test: State persistence — save on exit, restore on restart.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/helpers.sh"

setup_tmpdir
start_helper
start_sniptotop -n

create_snippet

# Enable notify mode
press_key "n" "$SNIPPET_WID"
sleep 0.3

# Kill sniptotop gracefully (SIGTERM triggers atexit save)
kill "$SNIPTOTOP_PID" 2>/dev/null
wait "$SNIPTOTOP_PID" 2>/dev/null || true
SNIPTOTOP_PID=""
sleep 0.3

# Check state file exists and has correct content
state_file="$TEST_TMPDIR/.config/sniptotop/state"
if [ ! -f "$state_file" ]; then
	fail "state file not found at $state_file"
fi

# State should contain helper window name and notify=1
entries=$(grep -v '^#' "$state_file" | grep -v '^$')
entry_count=$(echo "$entries" | wc -l)
assert_eq "$entry_count" "1" "state has one entry" || fail "wrong entry count: $entry_count"

# Check the entry contains the target name and ends with notify=1
if echo "$entries" | grep -q "sniptotop-test-target"; then
	echo "  ok: state contains target name"
else
	fail "state missing target name"
fi

# Last field should be 1 (notify enabled)
last_field=$(echo "$entries" | awk '{print $NF}')
assert_eq "$last_field" "1" "notify=1 in state" || fail "notify field wrong: $last_field"

# Record windows before restart
before_restart=$(xdotool search --onlyvisible --name "" 2>/dev/null | sort || true)

# Restart sniptotop (without -n, so it restores state)
start_sniptotop
sleep 1

# Find restored snippet window by diffing
main_wid=$(wait_for_window "sniptotop") || fail "main window not found on restart"
after_restart=$(xdotool search --onlyvisible --name "" 2>/dev/null | sort || true)

SNIPPET_WID=""
for wid in $after_restart; do
	if ! echo "$before_restart" | grep -qx "$wid"; then
		if [ "$wid" != "$main_wid" ]; then
			SNIPPET_WID="$wid"
		fi
	fi
done

if [ -z "$SNIPPET_WID" ]; then
	fail "snippet not restored"
fi

assert_window_exists "$SNIPPET_WID" "restored snippet exists" || fail "restored snippet not found"

# Check border is green (notify mode restored)
color=$(get_pixel_color "$SNIPPET_WID" 0 0)
assert_eq "$color" "00FF00" "restored notify border is green" || fail "restored border not green: $color"

echo "test_state: all assertions passed"
cleanup
