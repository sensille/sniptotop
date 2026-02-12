#!/bin/bash
# Test: Escape closes a snippet window.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/helpers.sh"

setup_tmpdir
start_helper
start_sniptotop -n

create_snippet
local_wid="$SNIPPET_WID"

assert_window_exists "$local_wid" "snippet exists before close" || fail "snippet not created"

# Focus snippet and press Escape
press_key "Escape" "$local_wid"
sleep 0.3

assert_window_gone "$local_wid" "snippet closed by Escape" || fail "snippet not closed"

# State file should have no snippet entries (only comment line)
state_file="$TEST_TMPDIR/.config/sniptotop/state"
if [ -f "$state_file" ]; then
	entries=$(grep -v '^#' "$state_file" | grep -c '[^ ]' || true)
	assert_eq "$entries" "0" "state file has no entries" || fail "state not empty"
fi

echo "test_close: all assertions passed"
cleanup
