all: sniptotop

sniptotop: main.c
	gcc main.c -Wall -g -lX11 -lxcb -lX11-xcb -lxcb-icccm -lxcb-damage -o sniptotop
