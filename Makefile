all: sniptotop

sniptotop: main.c
	gcc main.c -Wall -g -lX11 -lxcb -lX11-xcb -lxcb-icccm -lxcb-damage -o sniptotop

tests/test_helper: tests/helper.c
	gcc tests/helper.c -Wall -g -lxcb -o tests/test_helper

test: sniptotop tests/test_helper
	tests/run_tests.sh
