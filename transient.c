/* cc transient.c -o transient -lX11 */

#include "cbase.h"
#include "dwm.h"

int32
main(void) {
    Display *display2;
    Window root2;
    Window floating_window;
    Window transient_window = None;
    XSizeHints size_hints;
    XEvent event;

    display2 = XOpenDisplay(NULL);
    if (display2 == NULL) {
        exit(EXIT_FAILURE);
    }
    root2 = DefaultRootWindow(display2);

    floating_window = XCreateSimpleWindow(display2, root2, 100, 100, 400, 400,
                                          0, 0, 0);
    size_hints.min_width = 400;
    size_hints.max_width = 400;
    size_hints.min_height = 400;
    size_hints.max_height = 400;
    size_hints.flags = PMinSize | PMaxSize;
    XSetWMNormalHints(display2, floating_window, &size_hints);
    XStoreName(display2, floating_window, "floating");
    XMapWindow(display2, floating_window);

    XSelectInput(display2, floating_window, ExposureMask);
    while (true) {
        XNextEvent(display2, &event);

        if (transient_window == None) {
            sleep(5);
            transient_window = XCreateSimpleWindow(display2, root2, 50, 50,
                                                   100, 100, 0, 0, 0);
            XSetTransientForHint(display2, transient_window, floating_window);
            XStoreName(display2, transient_window, "transient");
            XMapWindow(display2, transient_window);
            XSelectInput(display2, transient_window, ExposureMask);
        }
    }

    XCloseDisplay(display2);
    exit(EXIT_SUCCESS);
}
