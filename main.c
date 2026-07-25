/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */

#include "dwm.h"

#define CBASE_IMPLEMENT
#include "cbase.h"

#include "draw.h"
#include "draw.c"
#include "dwm_stuff.h"

static void (*handlers[LASTEvent])(XEvent *) = {
    [ButtonPress] = handler_button_press,
    [ButtonRelease] = NULL,
    [CirculateNotify] = handler_others,
    [CirculateRequest] = handler_others,
    [ClientMessage] = handler_client_message,
    [ColormapNotify] = handler_others,
    [ConfigureNotify] = handler_configure_notify,
    [ConfigureRequest] = handler_configure_request,
    [CreateNotify] = handler_others,
    [DestroyNotify] = handler_destroy_notify,
    [EnterNotify] = handler_enter_notify,
    [Expose] = handler_expose,
    [FocusIn] = handler_focus_in,
    [FocusOut] = handler_others,
    [GenericEvent] = handler_others,
    [GraphicsExpose] = handler_others,
    [GravityNotify] = handler_others,
    [KeyPress] = handler_key_press,
    [KeyRelease] = NULL,
    [KeymapNotify] = handler_others,
    [LeaveNotify] = handler_others,
    [MapNotify] = handler_others,
    [MapRequest] = handler_map_request,
    [MappingNotify] = handler_mapping_notify,
    [MotionNotify] = handler_motion_notify,
    [NoExpose] = handler_others,
    [PropertyNotify] = handler_property_notify,
    [ReparentNotify] = handler_others,
    [ResizeRequest] = handler_others,
    [SelectionClear] = handler_others,
    [SelectionNotify] = handler_others,
    [SelectionRequest] = handler_others,
    [UnmapNotify] = handler_unmap_notify,
    [VisibilityNotify] = handler_others,
};

#include "config.h"
#include "handlers.c"

struct Pertag {
    const Layout *layouts[LENGTH(tags) + 1][2];
    int32 number_masters[LENGTH(tags) + 1];
    float master_facts[LENGTH(tags) + 1];
    uint32 selected_layouts[LENGTH(tags) + 1];
    uint32 tag;
    uint32 old_tag;
    bool top_bars[LENGTH(tags) + 1];
    bool bottom_bars[LENGTH(tags) + 1];
};

static int32 tags_widths[LENGTH(tags)];

_Static_assert(LENGTH(tags) <= 31, "limit of 31 tags");

#include "user.c"
#include "client.c"

void
monitor_arrange_monitor(Monitor *monitor) {
    strncpy(monitor->layout_symbol, monitor->layout[monitor->lay_i]->symbol,
            SIZEOF(monitor->layout_symbol));
    if (monitor->layout[monitor->lay_i]->function) {
        monitor->layout[monitor->lay_i]->function(monitor);
    }
    return;
}

void
monitor_focus(Monitor *monitor, bool set_focus) {
    client_unfocus(live_monitor->selected_client, set_focus);
    live_monitor = monitor;
    client_focus(NULL);
    return;
}

void
monitor_cleanup_monitor(Monitor *monitor) {
    if (!monitor) {
        return;
    }

    if (monitor == monitors) {
        monitors = monitors->next;
    } else {
        Monitor *monitor_aux = monitors;
        while (monitor_aux && monitor_aux->next != monitor) {
            monitor_aux = monitor_aux->next;
        }
        if (!monitor_aux) {
            return;
        }
        monitor_aux->next = monitor->next;
    }
    XUnmapWindow(display, monitor->top_bar_window);

    XDestroyWindow(display, monitor->top_bar_window);
    XDestroyWindow(display, monitor->bottom_bar_window);

    free2(monitor->pertag, SIZEOF(*monitor->pertag));
    free2(monitor, SIZEOF(*monitor));
    return;
}

void
monitor_draw_bars(Monitor *monitor) {
    int32 draw_x;
    int32 w;
    int32 text_pixels = 0;
    int32 urgent = 0;
    uint32 padding = (uint32)text_padding / 2;
    char tags_display[TAG_DISPLAY_SIZE] = {0};
    char *masters_names[LENGTH(tags)] = {0};
    Client *clients_with_icon[LENGTH(tags)] = {0};

    // TODO: This skips drawing the bottom bar when only the top bar is hidden.
    if (!monitor->show_top_bar) {
        return;
    }

    /* bottom bar */
    draw_setscheme(draw, scheme[SCHEME_NORMAL]);
    draw_rect(draw, 0, 0, (uint32)monitor->win_w, bar_height, true, true);
    if (monitor == live_monitor) {
        draw_status_text(&status_bottom, monitor->win_w);
    }
    draw_map(draw, monitor->bottom_bar_window, 0, 0, (uint32)monitor->win_w,
             bar_height);

    /* top bar: draw status first so it can be overdrawn by tags later */
    /* only drawn status on selected monitor */
    if (monitor == live_monitor) {
        draw_setscheme(draw, scheme[SCHEME_NORMAL]);

        draw_status_text(&status_top, monitor->win_w);
        text_pixels = status_top.pixels;
    }

    for (Client *client = monitor->clients; client; client = client->next) {
        if (client->is_urgent) {
            urgent |= client->tags;
        }

        for (int32 i = 0; i < LENGTH(tags); i += 1) {
            if (client->icon && client->tags & (1 << i)) {
                clients_with_icon[i] = client;
            }

            if (!masters_names[i] && client->tags & (1 << i)) {
                XClassHint class_hint = {NULL, NULL};
                // TODO: XGetClassHint allocates both strings; this leaks them.
                XGetClassHint(display, client->window, &class_hint);
                masters_names[i] = class_hint.res_class;
            }
        }
    }

    draw_x = 0;
    for (int32 i = 0; i < LENGTH(tags); i += 1) {
        Client *client_with_icon = clients_with_icon[i];
        char *master_name = masters_names[i];

        if (master_name) {
            if (client_with_icon) {
                snprintf(tags_display, SIZEOF(tags_display), "%s", tags[i]);
            } else {
                ulong n = strcspn(master_name, tag_label_delim);
                master_name[n] = '\0';
                snprintf(tags_display, SIZEOF(tags_display), tag_label_format,
                         tags[i], master_name);
            }
        } else {
            snprintf(tags_display, SIZEOF(tags_display), tag_empty_format,
                     tags[i]);
        }
        tags_widths[i] = w = get_text_pixels(tags_display);

        if (monitor->tagset[monitor->selected_tags] & 1 << i) {
            draw_setscheme(draw, scheme[SCHEME_SELECTED]);
        } else {
            draw_setscheme(draw, scheme[SCHEME_NORMAL]);
        }

        draw_text(draw, draw_x, 0, (uint32)w, bar_height, padding, tags_display,
                  urgent & 1 << i);
        draw_x += w;

        if (client_with_icon) {
            Picture icon = client_with_icon->icon;
            uint32 icon_width = client_with_icon->icon_width;
            uint32 icon_height = client_with_icon->icon_height;

            draw_text(draw, draw_x, 0, icon_width + padding, bar_height, 0, " ",
                      urgent & 1 << i);
            draw_pic(draw, draw_x, (bar_height - icon_height) / 2, icon_width,
                     icon_height, icon);
            draw_x += icon_width + padding;
            tags_widths[i] += icon_width + padding;
        }
    }
    w = get_text_pixels(monitor->layout_symbol);
    draw_setscheme(draw, scheme[SCHEME_NORMAL]);
    draw_x = draw_text(draw, draw_x, 0, (uint32)w, bar_height, padding,
                       monitor->layout_symbol, false);

    if ((w = monitor->win_w - text_pixels - draw_x) > (int32)bar_height) {
        int32 boxs = draw->fonts->h / 9;
        int32 boxw = draw->fonts->h / 6 + 2;

        if (monitor->selected_client) {
            Client *client = monitor->selected_client;
            char buffer[SIZEOF(*(&client->name)) + 20];

            if (monitor == live_monitor) {
                draw_setscheme(draw, scheme[SCHEME_SELECTED]);
            } else {
                draw_setscheme(draw, scheme[SCHEME_NORMAL]);
            }

            snprintf(buffer, SIZEOF(buffer), "{%s%s%s%s%s%s } %s",
                     (client->tags & (1 << 0)) ? tags_space[0] : "",
                     (client->tags & (1 << 1)) ? tags_space[1] : "",
                     (client->tags & (1 << 2)) ? tags_space[2] : "",
                     (client->tags & (1 << 3)) ? tags_space[3] : "",
                     (client->tags & (1 << 4)) ? tags_space[4] : "",
                     (client->tags & (1 << 5)) ? tags_space[5] : "",
                     client->name);

            draw_text(draw, draw_x, 0, (uint32)w, bar_height, padding, buffer, 0);
            if (monitor->selected_client->is_floating) {
                draw_rect(draw, draw_x + boxs, boxs, (uint32)boxw, (uint32)boxw,
                          monitor->selected_client->is_fixed, 0);
            }
        } else {
            draw_setscheme(draw, scheme[SCHEME_NORMAL]);
            draw_rect(draw, draw_x, 0, (uint32)w, bar_height, true, true);
        }
    }
    draw_map(draw, monitor->top_bar_window, 0, 0, (uint32)monitor->win_w,
             bar_height);

    return;
}

void
monitor_layout_columns(Monitor *monitor) {
    int32 number_tiled = 0;
    int32 i = 0;
    int32 x = 0;
    int32 y = 0;
    int32 mon_w;
    int32 number_masters;

    for (Client *client_aux = client_next_tiled(monitor->clients); client_aux;
         client_aux = client_next_tiled(client_aux->next)) {
        number_tiled += 1;
    }
    if (number_tiled == 0) {
        return;
    }
    if (number_tiled > 0) {
        snprintf(monitor->layout_symbol, SIZEOF(monitor->layout_symbol), "|%d|",
                 number_tiled);
    }

    number_masters = (int32)MIN(number_tiled, MAX(monitor->number_masters, 0));
    if (number_tiled > number_masters) {
        if (number_masters != 0) {
            mon_w = (int32)((float)monitor->win_w*monitor->master_fact);
        } else {
            mon_w = 0;
        }
    } else {
        mon_w = monitor->win_w;
    }

    for (Client *client = client_next_tiled(monitor->clients); client;
         client = client_next_tiled(client->next)) {
        int32 w;
        int32 h;
        if (i < number_masters) {
            int32 remaining = number_masters - i;
            if (remaining <= 0) {
                break;
            }
            w = (mon_w - x) / remaining;
            client_resize(client, x + monitor->win_x, monitor->win_y,
                          w - (2*client->border_pixels),
                          monitor->win_h - (2*client->border_pixels), false);
            x += client_pixels_width(client);
        } else {
            int32 remaining = number_tiled - i;
            if (remaining <= 0) {
                break;
            }
            h = (monitor->win_h - y) / remaining;
            client_resize(client, x + monitor->win_x, monitor->win_y + y,
                          monitor->win_w - x - (2*client->border_pixels),
                          h - (2*client->border_pixels), false);
            y += client_pixels_height(client);
        }
        i += 1;
    }
    return;
}

void
monitor_layout_grid(Monitor *monitor) {
    int32 number_tiled = 0;
    int32 columns = 0;
    int32 rows;
    int32 col_i;
    int32 row_i;
    int32 column_width;
    int32 i = 0;

    for (Client *client = client_next_tiled(monitor->clients); client;
         client = client_next_tiled(client->next)) {
        number_tiled += 1;
    }
    if (number_tiled == 0) {
        return;
    }

    if (number_tiled > 0) {
        snprintf(monitor->layout_symbol, SIZEOF(monitor->layout_symbol), "#%d#",
                 number_tiled);
    }

    /* grid dimensions */
    while (columns*columns < number_tiled) {
        if (columns > (number_tiled / 2)) {
            break;
        }
        columns += 1;
    }

    if (number_tiled == 5) {
        /* set layout against the general calculation: not 1:2:2, but 2:3 */
        columns = 2;
    }
    rows = number_tiled / columns;

    if (columns == 0) {
        column_width = monitor->win_w;
    } else {
        column_width = monitor->win_w / columns;
    }

    col_i = 0;
    row_i = 0;
    for (Client *client = client_next_tiled(monitor->clients); client;
         client = client_next_tiled(client->next)) {
        int32 client_height;
        int32 new_x;
        int32 new_y;
        int32 new_w;
        int32 new_h;

        if ((i / rows + 1) > (columns - number_tiled % columns)) {
            rows = number_tiled / columns + 1;
        }

        client_height = monitor->win_h / rows;

        new_x = monitor->win_x + col_i*column_width;
        new_y = monitor->win_y + row_i*client_height;
        new_w = column_width - 2*client->border_pixels;
        new_h = client_height - 2*client->border_pixels;
        client_resize(client, new_x, new_y, new_w, new_h, false);

        row_i += 1;
        if (row_i >= rows) {
            row_i = 0;
            col_i += 1;
        }
        i += 1;
    }
    return;
}

void
monitor_layout_monocle(Monitor *monitor) {
    uint32 number_clients = 0;

    for (Client *client = monitor->clients; client; client = client->next) {
        if (client_is_visible(client)) {
            number_clients += 1;
        }
    }

    if (number_clients > 0) {
        snprintf(monitor->layout_symbol, SIZEOF(monitor->layout_symbol), "[%u]",
                 number_clients);
    }

    for (Client *client = client_next_tiled(monitor->clients); client;
         client = client_next_tiled(client->next)) {
        int32 new_x = monitor->win_x;
        int32 new_y = monitor->win_y;
        int32 new_w = monitor->win_w - 2*client->border_pixels;
        int32 new_h = monitor->win_h - 2*client->border_pixels;
        client_resize(client, new_x, new_y, new_w, new_h, false);
    }
    return;
}

void
monitor_layout_tile(Monitor *monitor) {
    int32 number_tiled = 0;
    int32 i = 0;
    int32 mon_w = 0;
    int32 mon_y = 0;
    int32 tile_y = 0;
    int32 number_masters;

    for (Client *client_aux = client_next_tiled(monitor->clients); client_aux;
         client_aux = client_next_tiled(client_aux->next)) {
        number_tiled += 1;
    }
    if (number_tiled == 0) {
        return;
    }
    if (number_tiled > 0) {
        snprintf(monitor->layout_symbol, SIZEOF(monitor->layout_symbol), "=%d|",
                 number_tiled);
    }

    number_masters = (int32)MIN(number_tiled, MAX(monitor->number_masters, 0));
    if (number_tiled > number_masters) {
        if (number_masters != 0) {
            mon_w = (int32)((float)monitor->win_w*monitor->master_fact);
        } else {
            mon_w = 0;
        }
    } else {
        mon_w = monitor->win_w;
    }

    for (Client *client = client_next_tiled(monitor->clients); client;
         client = client_next_tiled(client->next)) {
        int32 h;
        int32 borders = 2*client->border_pixels;

        if (i < number_masters) {
            int32 remaining = number_masters - i;
            if (remaining <= 0) {
                break;
            }
            h = (monitor->win_h - mon_y) / remaining;
            client_resize(client, monitor->win_x, monitor->win_y + mon_y,
                          mon_w - borders, h - borders, false);
            if (mon_y + client_pixels_height(client) < monitor->win_h) {
                mon_y += client_pixels_height(client);
            }
        } else {
            int32 remaining = number_tiled - i;
            if (remaining <= 0) {
                break;
            }
            h = (monitor->win_h - tile_y) / remaining;
            client_resize(client, monitor->win_x + mon_w,
                          monitor->win_y + tile_y,
                          monitor->win_w - mon_w - borders, h - borders, false);
            if (tile_y + client_pixels_height(client) < monitor->win_h) {
                tile_y += client_pixels_height(client);
            }
        }
        i += 1;
    }
    return;
}

void
monitor_restack(Monitor *m) {
    XEvent event;

    monitor_draw_bars(m);
    if (!m->selected_client) {
        return;
    }

    if (m->selected_client->is_floating || !m->layout[m->lay_i]->function) {
        XRaiseWindow(display, m->selected_client->window);
    }

    if (m->layout[m->lay_i]->function) {
        XWindowChanges window_changes;
        window_changes.stack_mode = Below;
        window_changes.sibling = m->top_bar_window;

        for (Client *client = m->stack; client; client = client->stack_next) {
            if (!client->is_floating && client_is_visible(client)) {
                XConfigureWindow(display, client->window,
                                 CWSibling | CWStackMode, &window_changes);
                window_changes.sibling = client->window;
            }
        }
    }
    XSync(display, False);
    while (XCheckMaskEvent(display, EnterWindowMask, &event))
        ;
    return;
}

void
monitor_update_bar_position(Monitor *monitor) {
    monitor->win_y = monitor->mon_y;
    monitor->win_h = monitor->mon_h;

    if (monitor->show_top_bar) {
        monitor->win_h -= bar_height;
        monitor->top_bar_y = monitor->win_y;
        monitor->win_y = monitor->win_y + (int32)bar_height;
    } else {
        monitor->top_bar_y = -(int32)bar_height;
    }

    if (monitor->show_bottom_bar) {
        monitor->win_h -= bar_height;
        monitor->bottom_bar_y = monitor->win_y + monitor->win_h;
        monitor->win_y = monitor->win_y;
    } else {
        monitor->bottom_bar_y = -(int32)bar_height;
    }
    return;
}

void
monitor_arrange(Monitor *monitor) {
    if (monitor) {
        client_show_hide(monitor->stack);
    } else {
        for (monitor = monitors; monitor; monitor = monitor->next) {
            client_show_hide(monitor->stack);
        }
    }
    if (monitor) {
        monitor_arrange_monitor(monitor);
        monitor_restack(monitor);
    } else {
        XEvent event;
        for (Monitor *monitor_aux = monitors; monitor_aux;
             monitor_aux = monitor_aux->next) {
            monitor_arrange_monitor(monitor_aux);
        }
        XSync(display, False);
        while (XCheckMaskEvent(display, EnterWindowMask, &event))
            ;
    }
    return;
}

Monitor *
monitor_create(void) {
    Monitor *monitor = malloc2_zero(SIZEOF(*monitor));
    Pertag *pertag = malloc2_zero(SIZEOF(*pertag));

    monitor->tagset[0] = monitor->tagset[1] = 1;
    monitor->master_fact = master_fact;
    monitor->number_masters = 1;
    monitor->show_top_bar = true;
    monitor->show_bottom_bar = true;

    monitor->layout[0] = &layouts[0];
    monitor->layout[1] = &layouts[1 % LENGTH(layouts)];
    strncpy(monitor->layout_symbol, layouts[0].symbol,
            SIZEOF(monitor->layout_symbol));

    pertag->tag = pertag->old_tag = 1;

    for (int32 i = 0; i <= LENGTH(tags); i += 1) {
        pertag->number_masters[i] = monitor->number_masters;
        pertag->master_facts[i] = monitor->master_fact;

        pertag->layouts[i][0] = monitor->layout[0];
        pertag->layouts[i][1] = monitor->layout[1];
        pertag->selected_layouts[i] = monitor->lay_i;

        pertag->top_bars[i] = monitor->show_top_bar;
        pertag->bottom_bars[i] = monitor->show_bottom_bar;
    }
    monitor->pertag = pertag;

    return monitor;
}

int32
get_text_pixels(char *text) {
    int32 width = (int32)(draw_fontset_getwidth(draw, text) + (uint32)text_padding);
    return width;
}

int32
get_root_pointer(int32 *x, int32 *y) {
    int32 di;
    uint32 dui;
    Window dummy;

    return XQueryPointer(display, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

int64
window_state(Window window) {
    int32 actual_format_return;
    int64 result = -1;
    uchar *prop_return = NULL;
    ulong nitems_return;
    ulong bytes_after_return;
    Atom actual_type_return;
    int32 success;

    success = XGetWindowProperty(display, window, wm_atoms[WM_STATE], 0L, 2L,
                                 False, wm_atoms[WM_STATE], &actual_type_return,
                                 &actual_format_return, &nitems_return,
                                 &bytes_after_return, (uchar **)&prop_return);
    if (success != Success) {
        return -1;
    }

    if (nitems_return != 0) {
        result = *prop_return;
    }
    XFree(prop_return);
    return result;
}

int32
window_text_property(Window window, Atom atom, char *text, uint32 size) {
    XTextProperty text_property;
    char **list_return = NULL;
    int32 count_return;
    int32 success;

    if (!text || size == 0) {
        return 0;
    }
    text[0] = '\0';

    success = XGetTextProperty(display, window, &text_property, atom);
    if (!success || !text_property.nitems) {
        return 0;
    }

    if (text_property.encoding == XA_STRING) {
        strncpy(text, (char *)text_property.value, size - 1);
        text[size - 1] = '\0';
        XFree(text_property.value);
        return 1;
    }

    success = XmbTextPropertyToTextList(display, &text_property, &list_return,
                                        &count_return);
    if (success >= Success && count_return > 0 && *list_return) {
        strncpy(text, *list_return, size - 1);
        XFreeStringList(list_return);
    }

    text[size - 1] = '\0';
    XFree(text_property.value);
    return 1;
}

void
grab_keys(void) {
    uint32 modifiers[] = {0, LockMask, numlock_mask, numlock_mask | LockMask};
    int32 first_keycode;
    int32 end;
    int32 keysyms_per_keycode_return;
    KeySym *key_sym;

    update_numlock_mask();

    XUngrabKey(display, AnyKey, AnyModifier, root);
    XDisplayKeycodes(display, &first_keycode, &end);

    key_sym = XGetKeyboardMapping(display, (uchar)first_keycode,
                                  (uchar)end - first_keycode + 1,
                                  &keysyms_per_keycode_return);
    if (!key_sym) {
        return;
    }

    for (int32 k = first_keycode; k <= end; k += 1) {
        for (int32 i = 0; i < LENGTH(keys); i += 1) {
            /* skip modifier codes, we do that ourselves */
            int32 index = keysyms_per_keycode_return*(k - first_keycode);
            if (keys[i].keysym == key_sym[index]) {
                for (int32 j = 0; j < LENGTH(modifiers); j += 1) {
                    XGrabKey(display, k, (uint32)keys[i].mod | modifiers[j], root,
                             True, GrabModeAsync, GrabModeAsync);
                }
            }
        }
    }
    XFree(key_sym);
    return;
}

void
handler_others(XEvent *event) {
    (void)event;
    /* switch (event->type) { */
    /* case CirculateNotify: */
    /*     error("CirculateNotify"); */
    /*     break; */
    /* case CirculateRequest: */
    /*     error("CirculateRequest"); */
    /*     break; */
    /* case ColormapNotify: */
    /*     error("ColormapNotify"); */
    /*     break; */
    /* case CreateNotify: */
    /*     error("CreateNotify"); */
    /*     break; */
    /* case FocusOut: */
    /*     error("FocusOut"); */
    /*     break; */
    /* case GenericEvent: */
    /*     error("GenericEvent"); */
    /*     break; */
    /* case GraphicsExpose: */
    /*     error("GraphicsExpose"); */
    /*     break; */
    /* case GravityNotify: */
    /*     error("GravityNotify"); */
    /*     break; */
    /* case KeymapNotify: */
    /*     error("KeymapNotify"); */
    /*     break; */
    /* case LeaveNotify: */
    /*     error("LeaveNotify"); */
    /*     break; */
    /* case MapNotify: */
    /*     error("MapNotify"); */
    /*     break; */
    /* case NoExpose: */
    /*     error("NoExpose"); */
    /*     break; */
    /* case ReparentNotify: */
    /*     error("ReparentNotify"); */
    /*     break; */
    /* case ResizeRequest: */
    /*     error("ResizeRequest"); */
    /*     break; */
    /* case SelectionClear: */
    /*     error("SelectionClear"); */
    /*     break; */
    /* case SelectionNotify: */
    /*     error("SelectionNotify"); */
    /*     break; */
    /* case SelectionRequest: */
    /*     error("SelectionRequest"); */
    /*     break; */
    /* case VisibilityNotify: */
    /*     error("VisibilityNotify"); */
    /*     break; */
    /* default: */
    /*     error("unkown event type"); */
    /*     break; */
    /* } */
    return;
}

void
handler_button_press(XEvent *event) {
    Arg arg = {0};
    Client *client;
    Monitor *monitor;
    XButtonPressedEvent *button_event = &event->xbutton;
    int32 button_x = button_event->x;
    uint32 click = CLICK_ROOT_WIN;

    /* focus monitor if necessary */
    monitor = window_to_monitor(button_event->window);
    if (monitor && monitor != live_monitor) {
        monitor_focus(monitor, true);
    }

    monitor = live_monitor;
    if (button_event->window == monitor->top_bar_window) {
        uint32 i = 0;
        int32 x = 0;

        do {
            x += tags_widths[i];
        } while (button_x >= x && ++i < LENGTH(tags));

        if (i < LENGTH(tags)) {
            click = CLICK_BAR_TAGS;
            arg.ui = 1 << i;
        } else if (button_x < x + get_text_pixels(monitor->layout_symbol)) {
            click = CLICK_BAR_LAYOUT_SYMBOL;
        } else if (button_x > monitor->win_w - status_top.pixels) {
            click = CLICK_BAR_STATUS;
            status_get_signal_number(status_top.blocks_signal, button_x);
        } else {
            click = CLICK_BAR_TITLE;
        }
    } else if (button_event->window == monitor->bottom_bar_window) {
        click = CLICK_BOTTOM_BAR;
        status_get_signal_number(status_bottom.blocks_signal, button_x);
    } else if ((client = window_to_client(button_event->window))) {
        client_focus(client);
        monitor_restack(monitor);
        XAllowEvents(display, ReplayPointer, CurrentTime);
        click = CLICK_CLIENT_WIN;
    }

    for (uint32 i = 0; i < LENGTH(buttons); i += 1) {
        if (click != buttons[i].click) {
            continue;
        }
        if (buttons[i].button != button_event->button) {
            continue;
        }
        if (CLEANMASK(buttons[i].mask) != CLEANMASK(button_event->state)) {
            continue;
        }

        if (buttons[i].function) {
            if (click == CLICK_BAR_TAGS && buttons[i].arg.i == 0) {
                buttons[i].function(&arg);
            } else {
                buttons[i].function(&buttons[i].arg);
            }
        }
    }
    return;
}

void
draw_status_text(StatusBar *status_bar, int32 monitor_width) {
    int32 pixels = 0;
    int32 x0;
    if (status_bar == &status_top) {
        x0 = monitor_width - status_bar->pixels;
    } else {
        x0 = (monitor_width - status_bar->pixels) / 2;
    }

    for (int32 i = 0; i < status_bar->number_blocks; i += 1) {
        BlockSignal *block = &status_bar->blocks_signal[i];
        int32 text_pixels = block->max_x - block->min_x;

        // TODO: This mutates parsed hitboxes on every redraw, accumulating x0.
        block->max_x += x0;
        block->min_x += x0;

        if (text_pixels) {
            draw_text(draw, x0 + pixels, 0, (uint32)text_pixels, bar_height, 0,
                      &(status_bar->text[block->text_i]), 0);
            pixels += text_pixels;
        }
    }
    return;
}

void
status_parse_text(StatusBar *status_bar) {
    BlockSignal *blocks = status_bar->blocks_signal;
    int32 i = 0;
    char *text = status_bar->text;
    char *status = status_bar->text;
    int32 total_pixels = 0;
    int32 text_pixels;
    char byte = *status;

    while (*status) {
        if ((uchar)(*status) < ' ') {
            // TODO: Check i against STATUS_MAX_BLOCKS before writing blocks[i].
            blocks[i].signal = byte;
            byte = *status;
            *status = '\0';

            text_pixels = get_text_pixels(text) - text_padding;

            blocks[i].min_x = total_pixels;
            blocks[i].max_x = blocks[i].min_x + text_pixels;
            blocks[i].text_i = (int32)(text - status_bar->text);

            total_pixels += text_pixels;
            i += 1;

            text = status + 1;
        }
        status += 1;
    }
    {
        // TODO: This final block also overflows if i reached the cap.
        blocks[i].signal = byte;

        text_pixels = get_text_pixels(text) - text_padding + 2;

        blocks[i].min_x = total_pixels;
        blocks[i].max_x = blocks[i].min_x + text_pixels;
        blocks[i].text_i = (int32)(text - status_bar->text);

        total_pixels += text_pixels;
    }
    status_bar->number_blocks = i + 1;
    status_bar->pixels = total_pixels;
    return;
}

void
status_get_signal_number(BlockSignal *blocks, int32 button_x) {
    status_signal = 0;

    // TODO: Iterate only over parsed blocks; unused entries keep
    // stale hitboxes.
    for (int32 i = 0; i < STATUS_MAX_BLOCKS; i += 1) {
        if (blocks[i].min_x <= button_x && button_x <= blocks[i].max_x) {
            status_signal = blocks[i].signal;
            break;
        }
    }
    return;
}

#ifdef XINERAMA
static int32
is_unique_geometry(XineramaScreenInfo *unique, int32 n,
                   XineramaScreenInfo *screen_info) {
    while (n--) {
        bool equal_x = unique[n].x_org == screen_info->x_org;
        bool equal_y = unique[n].y_org == screen_info->y_org;
        bool equal_w = unique[n].width == screen_info->width;
        bool equal_h = unique[n].height == screen_info->height;
        if (equal_x && equal_y && equal_w && equal_h) {
            return 0;
        }
    }
    return 1;
}
#endif /* XINERAMA */

Monitor *
monitor_from_rectangle(int32 x, int32 y, int32 w, int32 h) {
    Monitor *monitor = live_monitor;
    int32 a;
    int32 max_area = 0;

    for (Monitor *mon = monitors; mon; mon = mon->next) {
        int32 min_x = (int32)MIN(x + w, mon->win_x + mon->win_w);
        int32 min_y = (int32)MIN(y + h, mon->win_y + mon->win_h);

        int32 ax = (int32)MAX(0, min_x - (int32)MAX(x, mon->win_x));
        int32 ay = (int32)MAX(0, min_y - (int32)MAX(y, mon->win_y));

        if ((a = ax*ay) > max_area) {
            max_area = a;
            monitor = mon;
        }
    }
    return monitor;
}

Monitor *
monitor_from_direction(int32 step) {
    Monitor *monitor = NULL;

    if (step > 0) {
        if (!(monitor = live_monitor->next)) {
            monitor = monitors;
        }
    } else if (live_monitor == monitors) {
        for (monitor = monitors; monitor->next; monitor = monitor->next)
            ;
    } else {
        for (monitor = monitors; monitor->next != live_monitor;
             monitor = monitor->next)
            ;
    }
    return monitor;
}

Client *
window_to_client(Window window) {
    for (Monitor *mon = monitors; mon; mon = mon->next) {
        for (Client *client = mon->clients; client; client = client->next) {
            if (client->window == window) {
                return client;
            }
        }
    }
    return NULL;
}

Monitor *
window_to_monitor(Window window) {
    Client *client;

    if (window == root) {
        int32 x;
        int32 y;
        if (get_root_pointer(&x, &y)) {
            Monitor *monitor = monitor_from_rectangle(x, y, 1, 1);
            return monitor;
        }
    }
    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        if (window == monitor->top_bar_window
            || window == monitor->bottom_bar_window) {
            return monitor;
        }
    }
    if ((client = window_to_client(window))) {
        return client->monitor;
    }

    return live_monitor;
}

void
focus_direction(enum Direction direction) {
    Client *selected = live_monitor->selected_client;
    Client *client = NULL;
    Client *client_aux;
    Client *next;
    uint32 best_score = UINT_MAX;

    if (!selected) {
        return;
    }

    next = selected->next;
    if (!next) {
        next = selected->monitor->clients;
    }
    for (client_aux = next; client_aux != selected; client_aux = next) {
        int32 client_score;
        int32 dist;

        next = client_aux->next;
        if (!next) {
            next = selected->monitor->clients;
        }

        if (!client_is_visible(client_aux) || client_aux->is_floating) {
            continue;
        }

        switch (direction) {
        case DIRECTION_LEFT:
            dist = selected->x - client_aux->x - client_aux->w;
            client_score = (int32)MIN(abs(dist),
                                      abs(dist + selected->monitor->win_w));
            client_score += abs(selected->y - client_aux->y) - 1;
            break;
        case DIRECTION_RIGHT:
            dist = client_aux->x - selected->x - selected->w;
            client_score = (int32)MIN(abs(dist),
                                      abs(dist + selected->monitor->win_w));
            client_score += abs(client_aux->y - selected->y);
            break;
        case DIRECTION_UP:
            dist = selected->y - client_aux->y - client_aux->h;
            client_score = (int32)MIN(abs(dist),
                                      abs(dist + selected->monitor->win_h));
            client_score += abs(selected->x - client_aux->x) - 1;
            break;
        case DIRECTION_DOWN:
        default:
            dist = client_aux->y - selected->y - selected->h;
            client_score = (int32)MIN(abs(dist),
                                      abs(dist + selected->monitor->win_h));
            client_score += abs(client_aux->x - selected->x);
            break;
        }

        if ((uint32)client_score < best_score) {
            best_score = (uint32)client_score;
            client = client_aux;
        }
    }

    if (client && client != selected) {
        client_focus(client);
        monitor_restack(client->monitor);
    }
    return;
}

void
view_tag(uint32 arg_tags) {
    Monitor *monitor = live_monitor;
    Pertag *pertag = live_monitor->pertag;
    uint32 selected_tags = arg_tags & TAGMASK;

    if (selected_tags == monitor->tagset[monitor->selected_tags]) {
        return;
    }

    monitor->selected_tags ^= 1; /* toggle selected_client tagset */

    if (selected_tags) {
        monitor->tagset[monitor->selected_tags] = selected_tags;
        pertag->old_tag = pertag->tag;

        if (selected_tags == TAGMASK) {
            pertag->tag = 0;
        } else {
            uint32 i = 0;
            while (!(selected_tags & (1u << i))) {
                i += 1;
            }
            pertag->tag = i + 1;
        }
    } else {
        uint32 temp = pertag->old_tag;
        pertag->old_tag = pertag->tag;
        pertag->tag = temp;
    }

    monitor_restore_pertag(monitor, pertag);

    client_focus(NULL);
    monitor_arrange(monitor);
    return;
}

void
monitor_restore_pertag(Monitor *monitor, Pertag *pertag) {
    uint32 tag = pertag->tag;

    monitor->number_masters = pertag->number_masters[tag];
    monitor->master_fact = pertag->master_facts[tag];
    monitor->lay_i = pertag->selected_layouts[tag];
    monitor->layout[monitor->lay_i] = pertag->layouts[tag][monitor->lay_i];
    monitor->layout[monitor->lay_i ^ 1]
        = pertag->layouts[tag][monitor->lay_i ^ 1];

    if (monitor->show_top_bar != pertag->top_bars[tag]) {
        toggle_bar(BAR_TOP);
    }
    if (monitor->show_bottom_bar != pertag->bottom_bars[tag]) {
        toggle_bar(BAR_BOTTOM);
    }
    return;
}

void
toggle_bar(int32 which) {
    Monitor *monitor = live_monitor;
    Pertag *pertag = monitor->pertag;
    Window bar_window;
    int32 bar_y;

    if (which == BAR_TOP) {
        monitor->show_top_bar = !monitor->show_top_bar;
        pertag->top_bars[pertag->tag] = monitor->show_top_bar;

        monitor_update_bar_position(monitor);
        bar_window = monitor->top_bar_window;
        bar_y = monitor->top_bar_y;
    } else {
        monitor->show_bottom_bar = !monitor->show_bottom_bar;
        pertag->bottom_bars[pertag->tag] = monitor->show_bottom_bar;

        monitor_update_bar_position(monitor);
        bar_window = monitor->bottom_bar_window;
        bar_y = monitor->bottom_bar_y;
    }

    XMoveResizeWindow(display, bar_window, monitor->win_x, bar_y,
                      (uint32)monitor->win_w, bar_height);

    monitor_arrange(monitor);
    return;
}

void
set_layout(const Layout *layout) {
    Monitor *monitor = live_monitor;
    Pertag *pertag = monitor->pertag;

    if (!layout || layout != monitor->layout[monitor->lay_i]) {
        pertag->selected_layouts[pertag->tag] ^= 1;
        monitor->lay_i = pertag->selected_layouts[monitor->pertag->tag];
    }

    if (layout) {
        monitor->layout[monitor->lay_i] = layout;
        pertag->layouts[pertag->tag][monitor->lay_i] = layout;
    }

    strncpy(monitor->layout_symbol, monitor->layout[monitor->lay_i]->symbol,
            SIZEOF(monitor->layout_symbol));

    if (monitor->selected_client) {
        monitor_arrange(monitor);
    } else {
        monitor_draw_bars(monitor);
    }
    return;
}

void
focus_next(bool direction) {
    Monitor *monitor;
    Client *client;

    monitor = live_monitor;
    client = monitor->selected_client;
    while (client == NULL && monitor->next) {
        monitor = monitor->next;
        monitor_focus(monitor, true);
        client = monitor->selected_client;
    }
    if (client == NULL) {
        return;
    }

    if (direction) {
        if (client->all_next) {
            client = client->all_next;
        } else {
            client = all_clients;
        }
    } else {
        Client *last = client;
        if (last == all_clients) {
            last = NULL;
        }
        for (client = all_clients; client->all_next != last;
             client = client->all_next)
            ;
    }
    client_focus(client);
    return;
}

void
draw_bars(void) {
    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        monitor_draw_bars(monitor);
    }
    return;
}

void
scan_windows_once(void) {
    Window root_return;
    Window parent_return;
    Window *children_return = NULL;
    uint32 nchildren_return;
    XWindowAttributes window_attributes;
    int32 success;

    success = XQueryTree(display, root, &root_return, &parent_return,
                         &children_return, &nchildren_return);
    if (!success) {
        return;
    }

    for (uint32 i = 0; i < nchildren_return; i += 1) {
        Window child = children_return[i];

        if (!XGetWindowAttributes(display, child, &window_attributes)) {
            continue;
        }
        if (window_attributes.override_redirect) {
            continue;
        }
        if (XGetTransientForHint(display, child, &root_return)) {
            continue;
        }

        if (window_attributes.map_state == IsViewable
            || window_state(child) == IconicState) {
            client_new(child, &window_attributes);
        }
    }

    /* now the transients */
    for (uint32 i = 0; i < nchildren_return; i += 1) {
        Window child = children_return[i];

        if (!XGetWindowAttributes(display, child, &window_attributes)) {
            continue;
        }
        if (!XGetTransientForHint(display, child, &root_return)) {
            continue;
        }

        if (window_attributes.map_state == IsViewable
            || window_state(child) == IconicState) {
            client_new(child, &window_attributes);
        }
    }

    if (children_return) {
        XFree(children_return);
    }
    return;
}

void
setup_once(void) {
    XVisualInfo *visual_infos;
    XVisualInfo vinfo_template;
    int32 nitems_return;
    int64 vinfo_mask = VisualScreenMask | VisualDepthMask | VisualClassMask;
    XSetWindowAttributes window_attributes;
    Atom UTF8STRING;
    struct sigaction signal_action;

    /* do not transform children into zombies when they terminate */
    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
    signal_action.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &signal_action, NULL);

    /* clean up any zombies (inherited from .xinitrc etc) immediately */
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    /* init screen */
    screen = DefaultScreen(display);
    screen_width = DisplayWidth(display, screen);
    screen_height = DisplayHeight(display, screen);
    root = RootWindow(display, screen);

    vinfo_template.screen = screen;
    vinfo_template.depth = 32;
    vinfo_template.class = TrueColor;
    visual_infos
        = XGetVisualInfo(display, vinfo_mask, &vinfo_template, &nitems_return);

    visual = NULL;
    for (int32 i = 0; i < nitems_return; i += 1) {
        XRenderPictFormat *render_format;
        XVisualInfo visual_info = visual_infos[i];

        render_format = XRenderFindVisualFormat(display, visual_info.visual);
        if (render_format->type == PictTypeDirect
            && render_format->direct.alphaMask) {
            visual = visual_info.visual;
            depth = visual_info.depth;
            color_map = XCreateColormap(display, root, visual, AllocNone);
            break;
        }
    }

    XFree(visual_infos);

    if (!visual) {
        visual = DefaultVisual(display, screen);
        depth = DefaultDepth(display, screen);
        color_map = DefaultColormap(display, screen);
    }

    draw = draw_create(display, screen, root, (uint32)screen_width,
                       (uint32)screen_height, visual, (uint32)depth, color_map);
    if (!draw_fontset_create(draw, fonts, LENGTH(fonts))) {
        error("Error loading fonts for dwm.\n");
        exit(EXIT_FAILURE);
    }
    text_padding = (int32)((double)draw->fonts->h / 2.2);
    bar_height = draw->fonts->h;
    update_geometry();

    /* init atoms */
    UTF8STRING = XInternAtom(display, "UTF8STRING", False);
    WM_INTERN_ATOM(WM_PROTOCOLS);
    WM_INTERN_ATOM(WM_DELETE_WINDOW);
    WM_INTERN_ATOM(WM_STATE);
    WM_INTERN_ATOM(WM_TAKE_FOCUS);
    NET_INTERN_ATOM(NET_ACTIVE_WINDOW);
    NET_INTERN_ATOM(NET_SUPPORTED);
    NET_INTERN_ATOM(NET_WM_NAME);
    NET_INTERN_ATOM(NET_WM_ICON);
    NET_INTERN_ATOM(NET_WM_STATE);
    NET_INTERN_ATOM(NET_SUPPORTING_WM_CHECK);
    NET_INTERN_ATOM(NET_WM_STATE_FULLSCREEN);
    NET_INTERN_ATOM(NET_WM_WINDOW_TYPE);
    NET_INTERN_ATOM(NET_WM_WINDOW_TYPE_DIALOG);
    NET_INTERN_ATOM(NET_CLIENT_LIST);
    NET_INTERN_ATOM(NET_CLIENT_INFO);

    /* init cursors */
    cursor[CURSOR_NORMAL] = draw_cur_create(draw, XC_left_ptr);
    cursor[CURSOR_RESIZE] = draw_cur_create(draw, XC_sizing);
    cursor[CURSOR_MOVE] = draw_cur_create(draw, XC_fleur);

    /* init appearance */
    scheme = malloc2_zero(LENGTH(colors)*SIZEOF(XftColor *));
    for (int32 i = 0; i < LENGTH(colors); i += 1) {
        scheme[i] = draw_scm_create(draw, colors[i], alphas[i], 3);
    }

    /* init bars */
    configure_bars_windows();
    status_update();
    monitor_draw_bars(live_monitor);

    /* supporting window for NET_SUPPORTING_WM_CHECK */
    wm_check_window = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(display, wm_check_window,
                    net_atoms[NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32,
                    PropModeReplace, (uchar *)&wm_check_window, 1);
    XChangeProperty(display, wm_check_window, net_atoms[NET_WM_NAME],
                    UTF8STRING, 8, PropModeReplace, (uchar *)"dwm", 3);
    XChangeProperty(display, root, net_atoms[NET_SUPPORTING_WM_CHECK],
                    XA_WINDOW, 32, PropModeReplace, (uchar *)&wm_check_window,
                    1);

    /* EWMH support per view */
    XChangeProperty(display, root, net_atoms[NET_SUPPORTED], XA_ATOM, 32,
                    PropModeReplace, (uchar *)net_atoms, NET_LAST);
    XDeleteProperty(display, root, net_atoms[NET_CLIENT_LIST]);
    XDeleteProperty(display, root, net_atoms[NET_CLIENT_INFO]);

    /* select events */
    window_attributes.cursor = cursor[CURSOR_NORMAL];
    window_attributes.event_mask
        = SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask
          | PointerMotionMask | EnterWindowMask | LeaveWindowMask
          | StructureNotifyMask | PropertyChangeMask;

    XChangeWindowAttributes(display, root, CWEventMask | CWCursor,
                            &window_attributes);
    XSelectInput(display, root, window_attributes.event_mask);
    grab_keys();
    client_focus(NULL);
    return;
}

void
configure_bars_windows(void) {
    XSetWindowAttributes window_attributes
        = {.override_redirect = True,
           .background_pixel = 0,
           .border_pixel = 0,
           .colormap = color_map,
           .event_mask = ButtonPressMask | ExposureMask};
    XClassHint class_hint = {"dwm", "dwm"};
    ulong value_mask = CWOverrideRedirect | CWBackPixel | CWBorderPixel
                       | CWColormap | CWEventMask;

    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        Window window;

        if (!monitor->top_bar_window) {
            window = XCreateWindow(display, root, monitor->win_x,
                                   monitor->top_bar_y, (uint32)monitor->win_w,
                                   bar_height, 0, depth, InputOutput, visual,
                                   value_mask, &window_attributes);
            monitor->top_bar_window = window;

            XDefineCursor(display, monitor->top_bar_window,
                          cursor[CURSOR_NORMAL]);
            XMapRaised(display, monitor->top_bar_window);
            XSetClassHint(display, monitor->top_bar_window, &class_hint);
        }
        if (!monitor->bottom_bar_window) {
            window = XCreateWindow(display, root, monitor->win_x,
                                   monitor->bottom_bar_y, (uint32)monitor->win_w,
                                   bar_height, 0, depth, InputOutput, visual,
                                   value_mask, &window_attributes);
            monitor->bottom_bar_window = window;

            XDefineCursor(display, monitor->bottom_bar_window,
                          cursor[CURSOR_NORMAL]);
            XMapRaised(display, monitor->bottom_bar_window);
            XSetClassHint(display, monitor->bottom_bar_window, &class_hint);
        }
    }
    return;
}

int32
update_geometry(void) {
    bool dirty = false;

#ifdef XINERAMA
    if (XineramaIsActive(display)) {
        XineramaScreenInfo *screen_info;
        XineramaScreenInfo *unique = NULL;
        Client *client;
        Monitor *monitor;
        int32 i = 0;
        int32 j = 0;
        int32 number_monitors = 0;
        int32 number_unique;
        int32 unique_alloc_len;

        screen_info = XineramaQueryScreens(display, &number_unique);
        for (monitor = monitors; monitor; monitor = monitor->next) {
            number_monitors += 1;
        }

        /* only consider unique geometries as separate screens */
        unique_alloc_len = number_unique;
        unique = malloc2_zero(unique_alloc_len*SIZEOF(*unique));
        while (i < number_unique) {
            if (is_unique_geometry(unique, j, &screen_info[i])) {
                memcpy64(&unique[j], &screen_info[i], SIZEOF(*unique));
                j += 1;
            }

            i += 1;
        }
        XFree(screen_info);
        number_unique = j;

        /* new monitors if number_unique > number_monitors */
        for (int32 k = number_monitors; k < number_unique; k += 1) {
            for (monitor = monitors; monitor && monitor->next;
                 monitor = monitor->next)
                ;
            if (monitor) {
                monitor->next = monitor_create();
            } else {
                monitors = monitor_create();
            }
        }

        monitor = monitors;
        for (int32 k = 0; k < number_unique; k += 1) {
            bool unique_x = unique[k].x_org != monitor->mon_x;
            bool unique_y = unique[k].y_org != monitor->mon_y;
            bool unique_w = unique[k].width != monitor->mon_w;
            bool unique_h = unique[k].height != monitor->mon_h;

            if (k >= number_monitors || unique_x || unique_y || unique_w
                || unique_h) {
                dirty = true;
                monitor->num = k;
                monitor->mon_x = monitor->win_x = unique[k].x_org;
                monitor->mon_y = monitor->win_y = unique[k].y_org;
                monitor->mon_w = monitor->win_w = unique[k].width;
                monitor->mon_h = monitor->win_h = unique[k].height;
                monitor_update_bar_position(monitor);
            }

            if (!(monitor = monitor->next)) {
                break;
            }
        }

        /* removed monitors if number_monitors > number_unique */
        for (int32 k = number_unique; k < number_monitors; k += 1) {
            for (monitor = monitors; monitor && monitor->next;
                 monitor = monitor->next)
                ;

            while ((client = monitor->clients)) {
                dirty = true;
                monitor->clients = client->next;
                // TODO: This corrupts all_clients unless client is its head.
                all_clients = client->all_next;
                client_detach_stack(client);
                client->monitor = monitors;
                client_attach(client);
                client_attach_stack(client);
            }

            if (monitor == live_monitor) {
                live_monitor = monitors;
            }
            monitor_cleanup_monitor(monitor);
        }
        free2(unique, unique_alloc_len*SIZEOF(*unique));
    } else
#endif /* XINERAMA */
    {  /* default monitor setup */
        if (!monitors) {
            monitors = monitor_create();
        }
        if (monitors->mon_w != screen_width
            || monitors->mon_h != screen_height) {
            dirty = true;
            monitors->mon_w = monitors->win_w = screen_width;
            monitors->mon_h = monitors->win_h = screen_height;
            monitor_update_bar_position(monitors);
        }
    }
    if (dirty) {
        live_monitor = monitors;
        live_monitor = window_to_monitor(root);
    }
    return dirty;
}

void
update_numlock_mask(void) {
    XModifierKeymap *modmap;

    numlock_mask = 0;
    modmap = XGetModifierMapping(display);
    for (int32 i = 0; i < 8; i += 1) {
        for (int32 j = 0; j < modmap->max_keypermod; j += 1) {
            KeyCode key_code
                = modmap->modifiermap[i*modmap->max_keypermod + j];
            if (key_code == XKeysymToKeycode(display, XK_Num_Lock)) {
                numlock_mask = (1 << i);
            }
        }
    }
    XFreeModifiermap(modmap);
}

void
status_update(void) {
    char *separator;

    if (!window_text_property(root, XA_WM_NAME,
                              status_top.text, SIZEOF(status_top.text))) {
        error("Error getting XA_WM_NAME property.\n");

        strcpy(status_top.text, "dwm-" QUOTE(VERSION));
        strcpy(status_top.text, "dwm-" QUOTE(VERSION));

        status_top.pixels = get_text_pixels(status_top.text) - text_padding + 2;
        status_bottom.pixels = status_top.pixels;
        return;
    }

    separator = strchr(status_top.text, DWM_BAR_SEPARATOR);
    if (separator) {
        int64 top_length = separator - status_top.text;
        // TODO: This copies one byte past status_top.text after the separator.
        int64 bottom_length = SIZEOF(status_bottom.text) - top_length;
        *separator = '\0';
        separator += 1;
        memcpy64(status_bottom.text, separator, bottom_length);
    } else {
        memset64(status_bottom.text, 0, SIZEOF(status_bottom.text));
    }

    status_parse_text(&status_top);
    status_parse_text(&status_bottom);
    return;
}

int32
main(int32 argc, char *argv[]) {
    if (argc == 2 && !strcmp("-v", argv[1])) {
        printf("dwm-" QUOTE(VERSION) "\n");
        exit(EXIT_SUCCESS);
    } else if (argc != 1) {
        error("usage: dwm [-v]");
        exit(EXIT_FAILURE);
    }

    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale()) {
        error("Warning: no locale support.\n");
    }

    if (!(display = XOpenDisplay(NULL))) {
        error("Error opening display.\n");
        exit(EXIT_FAILURE);
    }
    {
        xerrorxlib = XSetErrorHandler(handler_xerror_start);

        /* this causes an error if some other window manager is running */
        XSelectInput(display, DefaultRootWindow(display),
                     SubstructureRedirectMask);

        XSync(display, False);
        XSetErrorHandler(handler_xerror);
        XSync(display, False);
    }

    setup_once();

#ifdef __OpenBSD__
    char *pledge_args = "stdio rpath proc exec";
    if (pledge(pledge_args, NULL) == -1) {
        error("Error in pledge(%s)\n", pledge_args);
        exit(EXIT_FAILURE);
    }
#endif /* __OpenBSD__ */

    scan_windows_once();

    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        monitor_focus(monitor, false);

        view_tag(1 << 5);
        set_layout(&layouts[2]);

        toggle_bar(BAR_BOTTOM);

        view_tag(1 << 1);
    }

    {
        XEvent event;
        XSync(display, False);

        while (dwm_running) {
            XNextEvent(display, &event);
            if (handlers[event.type]) {
                handlers[event.type](&event);
            }
        }
    }

    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        while (monitor->stack) {
            client_unmanage(monitor->stack, 0);
        }
    }

    XUngrabKey(display, AnyKey, AnyModifier, root);

    while (monitors) {
        monitor_cleanup_monitor(monitors);
    }

    if (dwm_restart) {
        error("restarting...");
        execvp(argv[0], argv);
    }

    for (int32 i = 0; i < LENGTH(colors); i += 1) {
        free2(scheme[i], 3*SIZEOF(*scheme[i]));
    }
    free2(scheme, LENGTH(colors)*SIZEOF(*scheme));

    XDestroyWindow(display, wm_check_window);
    draw_free(draw);

    XSync(display, False);
    XSetInputFocus(display, PointerRoot, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(display, root, net_atoms[NET_ACTIVE_WINDOW]);
    XCloseDisplay(display);

    exit(EXIT_SUCCESS);
}
