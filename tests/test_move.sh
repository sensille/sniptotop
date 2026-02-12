#!/bin/bash
# Test: Right-click drag to move snippet.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/helpers.sh"

setup_tmpdir
start_helper
start_sniptotop -n

create_snippet

# Record initial position
initial_pos=$(get_window_pos "$SNIPPET_WID")
initial_x=$(echo "$initial_pos" | awk '{print $1}')
initial_y=$(echo "$initial_pos" | awk '{print $2}')

# Get snippet center for the drag start point
size=$(get_window_size "$SNIPPET_WID")
sw=$(echo "$size" | awk '{print $1}')
sh=$(echo "$size" | awk '{print $2}')
cx=$((initial_x + sw / 2))
cy=$((initial_y + sh / 2))

# Right-click drag 50px right, 30px down (hold shift to bypass snapping)
xdotool mousemove --sync "$cx" "$cy"
sleep 0.05
xdotool keydown shift
xdotool mousedown 3
sleep 0.05
xdotool mousemove --sync $((cx + 50)) $((cy + 30))
sleep 0.05
xdotool mouseup 3
xdotool keyup shift
sleep 0.3

# Check new position
new_pos=$(get_window_pos "$SNIPPET_WID")
new_x=$(echo "$new_pos" | awk '{print $1}')
new_y=$(echo "$new_pos" | awk '{print $2}')

dx=$((new_x - initial_x))
dy=$((new_y - initial_y))

assert_near "$dx" 50 5 "moved ~50px right" || fail "dx=$dx expected ~50"
assert_near "$dy" 30 5 "moved ~30px down" || fail "dy=$dy expected ~30"

echo "test_move: all assertions passed"
cleanup
