#!/bin/bash
# Test: Arrow key resize of snippet.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/helpers.sh"

setup_tmpdir
start_helper
start_sniptotop -n

create_snippet

# Record initial size
initial_size=$(get_window_size "$SNIPPET_WID")
initial_w=$(echo "$initial_size" | awk '{print $1}')
initial_h=$(echo "$initial_size" | awk '{print $2}')

# Focus snippet
xdotool windowfocus --sync "$SNIPPET_WID" 2>/dev/null || true
sleep 0.1

# Press Right 5 times
for i in $(seq 1 5); do
	press_key "Right" "$SNIPPET_WID"
done
sleep 0.3

# Check width increased by 5
new_size=$(get_window_size "$SNIPPET_WID")
new_w=$(echo "$new_size" | awk '{print $1}')
expected_w=$((initial_w + 5))
assert_near "$new_w" "$expected_w" 2 "width increased by 5" || fail "width: expected ~$expected_w got $new_w"

# Press Down 3 times
for i in $(seq 1 3); do
	press_key "Down" "$SNIPPET_WID"
done
sleep 0.3

# Check height increased by 3
final_size=$(get_window_size "$SNIPPET_WID")
final_h=$(echo "$final_size" | awk '{print $2}')
expected_h=$((initial_h + 3))
assert_near "$final_h" "$expected_h" 2 "height increased by 3" || fail "height: expected ~$expected_h got $final_h"

echo "test_resize: all assertions passed"
cleanup
