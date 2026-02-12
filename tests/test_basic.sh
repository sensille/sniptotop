#!/bin/bash
# Test: Create and verify a snippet window.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/helpers.sh"

setup_tmpdir
start_helper
start_sniptotop -n

create_snippet

assert_window_exists "$SNIPPET_WID" "snippet window exists" || fail "snippet not created"

# Check snippet geometry is approximately 60x60
size=$(get_window_size "$SNIPPET_WID")
w=$(echo "$size" | awk '{print $1}')
h=$(echo "$size" | awk '{print $2}')

# The snippet includes 2*border_width (2px each side = 4px total)
# Capture area is 60x60, window size should be 60+4=64 or close
assert_near "$w" 60 8 "snippet width ~60" || fail "bad width $w"
assert_near "$h" 60 8 "snippet height ~60" || fail "bad height $h"

echo "test_basic: all assertions passed"
cleanup
