from guppi_utils import guppi_status
import curses, curses.wrapper
import time

def display_status(stdscr,stat):
    # Set non-blocking input
    stdscr.nodelay(1)
    run = 1

    # Look like gbtstatus (why not?)
    curses.init_pair(1, curses.COLOR_CYAN, curses.COLOR_BLACK)
    curses.init_pair(2, curses.COLOR_GREEN, curses.COLOR_BLACK)
    curses.init_pair(3, curses.COLOR_WHITE, curses.COLOR_RED)
    keycol = curses.color_pair(1)
    valcol = curses.color_pair(2)
    errcol = curses.color_pair(3)

    # Loop 
    while (run):
        # Refresh status info
        stat.read()

        # Reset screen
        stdscr.erase()

        # Draw border
        stdscr.border()

        # Get dimensions
        (ymax,xmax) = stdscr.getmaxyx()

        # Display main status info
        onecol = False # Set True for one-column format
        col = 2
        curline = 0
        stdscr.addstr(curline,col,"Current pipeline status:", keycol);
        curline += 2
        flip=0
        keys = stat.hdr.keys()
        keys.sort()
        for k in keys:
            v = stat.hdr[k]
            if (curline < ymax-3):
                stdscr.addstr(curline,col,"%8s : "%k, keycol)
                stdscr.addstr("%s" % v, valcol)
            else:
                stdscr.addstr(ymax-3,col, "-- Increase window size --", errcol);
            if (flip or onecol):
                curline += 1
                col = 2
                flip = 0
            else:
                col = 40
                flip = 1
        col = 2
        if (flip and not onecol):
            curline += 1

        # Bottom info line
        stdscr.addstr(ymax-2,col,"Last update: " + time.asctime() \
                + "  -  Press 'q' to quit")

        # Redraw screen
        stdscr.refresh()

        # Sleep a bit
        time.sleep(0.25)

        # Look for input
        c = stdscr.getch()
        while (c != curses.ERR):
            if (c==ord('q')):
                run = 0
            c = stdscr.getch()

# Connect to guppi status, data bufs
g = guppi_status()

# Wrapper calls the main func
try:
    curses.wrapper(display_status,g)
except KeyboardInterrupt:
    print "Exiting..."


