#!/bin/bash
# Test: Disconnect when target closes, reconnect when new window with same title appears.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/helpers.sh"

setup_tmpdir
start_helper
start_sniptotop -n

create_snippet
local_wid="$SNIPPET_WID"

assert_window_exists "$local_wid" "snippet exists before disconnect" || fail "snippet not created"

# Destroy helper window via SIGUSR2
kill -USR2 "$HELPER_PID"
wait "$HELPER_PID" 2>/dev/null || true
HELPER_PID=""
sleep 0.5

# Snippet should still exist (disconnected, greyed out)
assert_window_exists "$local_wid" "snippet survives disconnect" || fail "snippet died on disconnect"

# Start a new helper with the same window title
start_helper
sleep 1.0

# Snippet should still exist and have reconnected
assert_window_exists "$local_wid" "snippet still exists after reconnect" || fail "snippet gone after reconnect"

# Verify snippet is showing content (not solid grey)
# Sample a pixel in the content area (inside border). Should match helper's color.
# Helper started fresh with red (FF0000). Content area starts at border_width=2.
# Give time for redraw
sleep 0.5
color=$(get_pixel_color "$local_wid" 5 5)

# After reconnect, content should not be grey (808080 or similar).
# It should be some shade of the helper's fill color.
if [ "$color" = "808080" ] || [ "$color" = "C0C0C0" ] || [ "$color" = "000000" ]; then
	# Might be a timing issue, wait more
	sleep 1
	color=$(get_pixel_color "$local_wid" 5 5)
fi

# We just check it's not grey/black (which would mean disconnected still)
if [ "$color" != "808080" ] && [ "$color" != "C0C0C0" ]; then
	echo "  ok: snippet content is not grey ($color)"
else
	fail "snippet still showing grey after reconnect: $color"
fi

echo "test_disconnect: all assertions passed"
cleanup
