# sniptotop

A small tool to mark snippets of any window on your desktop, display them
in their own window and keep them always on top.

## Usage

Click into the main window once to start selection. Click and drag with
the crosshair-cursor to select a snippet. The snippet will be displayed
in its own window.
Move the window by right-clicking and dragging it with the mouse.
Discard the window by hitting escape in it.
A left-click in a snippet-window will bring the source window into the
foreground.

For it to work the source window has to be on the desktop (not minimized),
but it can be covered by other windows.

Built for X11 desktops.

## Building
Prerequisites (on Debian/Ubuntu): libx11-dev libx11-xcb-dev
    libxcb-damage0-dev libxcb-icccm4-dev libyaml-dev
