from sys import argv
from guppi_utils import guppi_status
import curses, curses.wrapper
import time

def display_status(stdscr, stat, instance_id):
    # Set non-blocking input
    stdscr.nodelay(1)
    run = 1

    # Hide the cursor
    curses.curs_set(0)

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

        # Get instance_id (as a string) from status buffer
        try:
            instance_str = stat['INSTANCE']
        except KeyError:
            instance_str = '?'

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
        stdscr.addstr(curline,col, \
            " Current Status: Instance %s " % instance_str, keycol);
        curline += 2
        flip=0
        keys = stat.hdr.keys()
        keys.sort()
        for k in keys:
            # Don't show INSTANCE record in the list
            # (it's already in the title)
            if k == 'INSTANCE': continue

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
        stdscr.addstr(ymax-2,col,"Last update: " + time.asctime() + "  -  " \
                + "Press 'q' to quit, " \
                + "0-9 to select")

        # Redraw screen
        stdscr.refresh()

        # Sleep a bit
        time.sleep(0.25)

        # Look for input
        c = stdscr.getch()
        while (c != curses.ERR):
            if (c==ord('q')):
                run = 0
            if (ord('0') <= c and c <= ord('9') and c != instance_id):
                c -= ord('0')
                try:
                    stat = guppi_status(c)
                    instance_id = c
                except:
                    stdscr.addstr(ymax-2,col, \
                        "Error connecting to status buffer for instance %d"%c,
                        errcol)
                    stdscr.clrtoeol()
                    stdscr.refresh()
                    # Give time to read message, but could make UI feel
                    # non-responsive
                    time.sleep(1)
            if (c==ord('+') or c==ord('=')):
                try:
                    stat = guppi_status(instance_id+1)
                    instance_id += 1
                except:
                    pass
            if (c==ord('-')):
                try:
                    stat = guppi_status(instance_id-1)
                    instance_id -= 1
                except:
                    pass

            c = stdscr.getch()

# Get instance_id
try:
    instance_id = int(argv[1])
except:
    instance_id = 0

# Connect to guppi status, data bufs
try:
    g = guppi_status(instance_id)
except:
    print "Error connecting to status buffer for instance %d" % instance_id
    exit(1)

# Wrapper calls the main func
try:
    curses.wrapper(display_status,g,instance_id)
except KeyboardInterrupt:
    print "Exiting..."
except:
    print "Error reading from status buffer %d" % instance_id
