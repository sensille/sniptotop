#!/bin/bash
# Test: Edge snapping when moving snippets near screen edges.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/helpers.sh"

setup_tmpdir
start_helper
start_sniptotop -n

create_snippet

# Get snippet info
size=$(get_window_size "$SNIPPET_WID")
sw=$(echo "$size" | awk '{print $1}')
sh=$(echo "$size" | awk '{print $2}')
pos=$(get_window_pos "$SNIPPET_WID")
cur_x=$(echo "$pos" | awk '{print $1}')
cur_y=$(echo "$pos" | awk '{print $2}')

# Move snippet near left edge (x~1) — should snap to x=0
# Right-click drag from current center toward left edge
cx=$((cur_x + sw / 2))
cy=$((cur_y + sh / 2))
# Target: window x=1, so center would be at 1+sw/2
target_cx=$((1 + sw / 2))

rclick_drag "$cx" "$cy" "$target_cx" "$cy"
sleep 0.3

new_pos=$(get_window_pos "$SNIPPET_WID")
new_x=$(echo "$new_pos" | awk '{print $1}')
assert_eq "$new_x" "0" "snapped to left edge" || fail "x=$new_x expected 0"

# Now move snippet near top edge (y~1) — should snap to y=0
pos=$(get_window_pos "$SNIPPET_WID")
cur_x=$(echo "$pos" | awk '{print $1}')
cur_y=$(echo "$pos" | awk '{print $2}')
cx=$((cur_x + sw / 2))
cy=$((cur_y + sh / 2))
# Target: window y=1, so center would be at 1+sh/2
target_cy=$((1 + sh / 2))

rclick_drag "$cx" "$cy" "$cx" "$target_cy"
sleep 0.3

new_pos=$(get_window_pos "$SNIPPET_WID")
new_y=$(echo "$new_pos" | awk '{print $2}')
assert_eq "$new_y" "0" "snapped to top edge" || fail "y=$new_y expected 0"

echo "test_snapping: all assertions passed"
cleanup
