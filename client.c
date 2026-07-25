#if !defined(CLIENT_C)
#define CLIENT_C

#include "dwm.h"
#include "config.h"

void
client_apply_rules(Client *client) {
    const char *class;
    const char *instance;
    XClassHint class_hint = {NULL, NULL};

    client->is_floating = false;
    client->tags = 0;
    XGetClassHint(display, client->window, &class_hint);
    class = class_hint.res_class ? class_hint.res_class : broken;
    instance = class_hint.res_name ? class_hint.res_name : broken;

    for (int32 i = 0; i < LENGTH(rules); i += 1) {
        const Rule *rule = &rules[i];
        Monitor *monitor_aux;

        if ((!rule->title || strstr(client->name, rule->title))
            && (!rule->class || strstr(class, rule->class))
            && (!rule->instance || strstr(instance, rule->instance))) {
            client->is_floating = rule->is_floating;
            client->is_fake_fullscreen = rule->is_fake_fullscreen;
            client->tags |= rule->tags;

            if (rule->is_floating) {
                client_center(client);
            }

            for (monitor_aux = monitors;
                 monitor_aux && monitor_aux->num != rule->monitor;
                 monitor_aux = monitor_aux->next)
                ;
            if (monitor_aux) {
                client->monitor = monitor_aux;
            }

            if (rule->switchtotag) {
                view_tag(rule->tags);
            }
        }
    }
    if (class_hint.res_class) {
        XFree(class_hint.res_class);
    }
    if (class_hint.res_name) {
        XFree(class_hint.res_name);
    }

    if (client->tags & TAGMASK) {
        client->tags = client->tags & TAGMASK;
    } else {
        uint32 which_tags = client->monitor->selected_tags;
        client->tags = client->monitor->tagset[which_tags];
    }
    return;
}

int32
client_apply_size_hints(Client *client, int32 *x, int32 *y, int32 *w, int32 *h,
                        bool interact) {
    Monitor *monitor = client->monitor;
    int32 success;

    *w = (int32)MAX(1, *w);
    *h = (int32)MAX(1, *h);

    if (interact) {
        if (*x > screen_width) {
            *x = screen_width - client_pixels_width(client);
        }
        if (*y > screen_height) {
            *y = screen_height - client_pixels_height(client);
        }
        if (*x + *w + 2*client->border_pixels < 0) {
            *x = 0;
        }
        if (*y + *h + 2*client->border_pixels < 0) {
            *y = 0;
        }
    } else {
        if (*x >= monitor->win_x + monitor->win_w) {
            *x = monitor->win_x + monitor->win_w - client_pixels_width(client);
        }
        if (*y >= monitor->win_y + monitor->win_h) {
            *y = monitor->win_y + monitor->win_h - client_pixels_height(client);
        }
        if (*x + *w + 2*client->border_pixels <= monitor->win_x) {
            *x = monitor->win_x;
        }
        if (*y + *h + 2*client->border_pixels <= monitor->win_y) {
            *y = monitor->win_y;
        }
    }

    if (*h < (int32)bar_height) {
        *h = (int32)bar_height;
    }
    if (*w < (int32)bar_height) {
        *w = (int32)bar_height;
    }

    if (resizehints || client->is_floating
        || !client->monitor->layout[client->monitor->lay_i]->function) {
        int32 base_is_min;

        if (!client->hintsvalid) {
            client_update_size_hints(client);
        }

        /* see last two sentences in ICCCM 4.1.2.3 */
        base_is_min = client->base_w == client->min_w
                      && client->base_h == client->min_h;
        if (!base_is_min) { /* temporarily remove base dimensions */
            *w -= client->base_w;
            *h -= client->base_h;
        }

        /* adjust for aspect limits */
        if (client->min_aspect > 0 && client->max_aspect > 0) {
            // TODO: Do not round aspect limits before scaling.
            // Fractional ratios otherwise produce wrong sizes.
            if (client->max_aspect < (float)*w / (float)*h) {
                *w = *h*((int32)(client->max_aspect + 0.5f));
            } else if (client->min_aspect < (float)*h / (float)*w) {
                *h = *w*((int32)(client->min_aspect + 0.5f));
            }
        }

        if (base_is_min) { /* increment calculation requires this */
            *w -= client->base_w;
            *h -= client->base_h;
        }

        /* adjust for increment value */
        if (client->increment_w) {
            *w -= *w % client->increment_w;
        }
        if (client->increment_h) {
            *h -= *h % client->increment_h;
        }

        /* restore base dimensions */
        *w = (int32)MAX(*w + client->base_w, client->min_w);
        *h = (int32)MAX(*h + client->base_h, client->min_h);
        if (client->max_w) {
            *w = (int32)MIN(*w, client->max_w);
        }
        if (client->max_h) {
            *h = (int32)MIN(*h, client->max_h);
        }
    }
    success = *x != client->x || *y != client->y || *w != client->w
              || *h != client->h;
    return success;
}

void
client_attach(Client *client) {
    client->next = client->monitor->clients;
    client->all_next = all_clients;
    client->monitor->clients = client;
    all_clients = client;
    return;
}

void
client_attach_stack(Client *client) {
    client->stack_next = client->monitor->stack;
    client->monitor->stack = client;
    return;
}

void
client_configure(Client *client) {
    XConfigureEvent configure_event;

    configure_event.type = ConfigureNotify;
    configure_event.display = display;
    configure_event.event = client->window;
    configure_event.window = client->window;
    configure_event.x = client->x;
    configure_event.y = client->y;
    configure_event.width = client->w;
    configure_event.height = client->h;
    configure_event.border_width = client->border_pixels;
    configure_event.above = None;
    configure_event.override_redirect = False;

    XSendEvent(display, client->window, False, StructureNotifyMask,
               (XEvent *)&configure_event);
    return;
}

void
client_detach(Client *client) {
    Client **clients;

    for (clients = &client->monitor->clients; *clients && *clients != client;
         clients = &(*clients)->next)
        ;
    *clients = client->next;

    for (clients = &all_clients; *clients && *clients != client;
         clients = &(*clients)->all_next)
        ;
    *clients = client->all_next;

    return;
}

void
client_detach_stack(Client *client) {
    Client **client_aux;

    for (client_aux = &client->monitor->stack;
         *client_aux && *client_aux != client;
         client_aux = &(*client_aux)->stack_next)
        ;
    *client_aux = client->stack_next;

    if (client == client->monitor->selected_client) {
        Client *t;
        for (t = client->monitor->stack; t && !client_is_visible(t);
             t = t->stack_next)
            ;
        client->monitor->selected_client = t;
    }
    return;
}

void
client_focus(Client *client) {
    Client *selected = live_monitor->selected_client;
    if (!client || !client_is_visible(client)) {
        for (client = live_monitor->stack; client && !client_is_visible(client);
             client = client->stack_next)
            ;
    }

    if (selected && selected != client) {
        client_unfocus(selected, false);
    }

    if (client) {
        if (client->monitor != live_monitor) {
            live_monitor = client->monitor;
        }
        if (client->is_urgent) {
            client_set_urgent(client, false);
        }
        client_detach_stack(client);
        client_attach_stack(client);
        client_grab_buttons(client, true);
        XSetWindowBorder(display, client->window,
                         scheme[SCHEME_SELECTED][ColBorder].pixel);
        client_set_focus(client);
    } else {
        XSetInputFocus(display, live_monitor->top_bar_window,
                       RevertToPointerRoot, CurrentTime);
        XDeleteProperty(display, root, net_atoms[NET_ACTIVE_WINDOW]);
    }

    live_monitor->selected_client = client;
    draw_bars();
    return;
}

Atom
client_get_atom_property(Client *client, Atom property) {
    int32 actual_format_return;
    ulong nitems_return;
    Atom actual_type_return;
    Atom atom = None;
    Atom *prop_return = NULL;
    int32 success;

    success = XGetWindowProperty(
        display, client->window, property, 0L, SIZEOF(atom), False, XA_ATOM,
        &actual_type_return, &actual_format_return, &nitems_return,
        &nitems_return, (uchar **)&prop_return);
    if (success == Success && prop_return) {
        atom = *prop_return;
        XFree(prop_return);
    }
    return atom;
}

void
client_grab_buttons(Client *client, bool focused) {
    uint32 modifiers[] = {0, LockMask, numlock_mask, numlock_mask | LockMask};

    update_numlock_mask();
    XUngrabButton(display, AnyButton, AnyModifier, client->window);
    if (!focused) {
        XGrabButton(display, AnyButton, AnyModifier, client->window, False,
                    BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
    }
    for (int32 i = 0; i < LENGTH(buttons); i += 1) {
        if (buttons[i].click != CLICK_CLIENT_WIN) {
            continue;
        }

        for (int32 j = 0; j < LENGTH(modifiers); j += 1) {
            XGrabButton(display, (uint32)buttons[i].button,
                        buttons[i].mask | modifiers[j], client->window, False,
                        BUTTONMASK, GrabModeAsync, GrabModeSync, None, None);
        }
    }
    return;
}

void
client_center(Client *client) {
    Monitor *monitor = client->monitor;
    int32 client_width = client_pixels_width(client);
    int32 client_height = client_pixels_height(client);

    client->x = monitor->mon_x + (monitor->mon_w - client_width) / 2;
    client->y = monitor->mon_y + (monitor->mon_h - client_height) / 2;
    return;
}

void
client_new(Window window, XWindowAttributes *window_attributes) {
    Window trans_window = None;
    XWindowChanges window_changes;
    Client *client = malloc2_zero(SIZEOF(*client));
    Client *trans_client = NULL;
    int32 success;

    client->window = window;

    client->x = client->old_x = window_attributes->x;
    client->y = client->old_y = window_attributes->y;
    client->w = client->old_w = window_attributes->width;
    client->h = client->old_h = window_attributes->height;
    client->old_border_pixels = window_attributes->border_width;

    client_update_icon(client);
    client_update_title(client);

    success = XGetTransientForHint(display, window, &trans_window);
    if (success && (trans_client = window_to_client(trans_window))) {
        client->monitor = trans_client->monitor;
        client->tags = trans_client->tags;
    } else {
        client->monitor = live_monitor;
        client_apply_rules(client);
    }

    {
        Monitor *monitor = client->monitor;
        int32 client_width = client_pixels_width(client);
        int32 client_height = client_pixels_height(client);

        if (client->x + client_width > monitor->win_x + monitor->win_w) {
            client->x = monitor->win_x + monitor->win_w - client_width;
        }
        if (client->y + client_height > monitor->win_y + monitor->win_h) {
            client->y = monitor->win_y + monitor->win_h - client_height;
        }
    }
    client->x = (int32)MAX(client->x, client->monitor->win_x);
    client->y = (int32)MAX(client->y, client->monitor->win_y);
    client->border_pixels = border_pixels;

    window_changes.border_width = client->border_pixels;
    XConfigureWindow(display, window, CWBorderWidth, &window_changes);
    XSetWindowBorder(display, window, scheme[SCHEME_NORMAL][ColBorder].pixel);

    /* propagates border_pixels, if size doesn'trans_client change */
    client_configure(client);
    client_update_window_type(client);
    client_update_size_hints(client);
    client_update_wm_hints(client);
    {
        int32 actual_format_return;
        ulong *prop_return = NULL;
        ulong nitems_return = 0;
        ulong bytes_after_return;
        Atom actual_type_return;

        success = XGetWindowProperty(
            display, client->window, net_atoms[NET_CLIENT_INFO], 0L, 2L, False,
            XA_CARDINAL, &actual_type_return, &actual_format_return,
            &nitems_return, &bytes_after_return, (uchar **)&prop_return);
        if (success == Success && nitems_return == 2) {
            client->tags = (uint32)*prop_return;
            for (Monitor *mon = monitors; mon; mon = mon->next) {
                if (mon->num == (int32)*(prop_return + 1)) {
                    client->monitor = mon;
                    break;
                }
            }
        }
        if (nitems_return > 0) {
            XFree(prop_return);
        }
    }
    client_set_client_tag_prop(client);

    client->w = (int32)MIN(client->w, (screen_width*2) / 3);
    client->h = (int32)MIN(client->h, (screen_height*2) / 3);

    client->stored_fx = client->x;
    client->stored_fy = client->y;
    client->stored_fw = client->w;
    client->stored_fh = client->h;

    client_center(client);

    XSelectInput(display, window,
                 EnterWindowMask | FocusChangeMask | PropertyChangeMask
                     | StructureNotifyMask);

    client_grab_buttons(client, false);

    if (!client->is_floating) {
        client->is_floating = trans_window != None || client->is_fixed;
        client->was_floating = client->is_floating;
    }
    if (client->is_floating) {
        XRaiseWindow(display, client->window);
    }

    client_attach(client);
    client_attach_stack(client);

    XChangeProperty(display, root, net_atoms[NET_CLIENT_LIST], XA_WINDOW, 32,
                    PropModeAppend, (uchar *)&(client->window), 1);

    /* some windows require this */
    XMoveResizeWindow(display, client->window, client->x + 2*screen_width,
                      client->y, (uint32)client->w, (uint32)client->h);
    client_set_client_state(client, NormalState);

    if (client->monitor == live_monitor) {
        client_unfocus(live_monitor->selected_client, false);
    }

    client->monitor->selected_client = client;
    monitor_arrange(client->monitor);
    XMapWindow(display, client->window);
    client_focus(NULL);
    return;
}

void
client_unmanage(Client *client, int32 destroyed) {
    Monitor *monitor = client->monitor;
    XWindowChanges window_changes;

    client_detach(client);
    client_detach_stack(client);
    client_free_icon(client);

    if (!destroyed) {
        window_changes.border_width = client->old_border_pixels;
        XGrabServer(display); /* avoid race conditions */
        XSetErrorHandler(handler_xerror_dummy);

        XSelectInput(display, client->window, NoEventMask);

        XConfigureWindow(display, client->window, CWBorderWidth,
                         &window_changes);
        XUngrabButton(display, AnyButton, AnyModifier, client->window);
        client_set_client_state(client, WithdrawnState);

        XSync(display, False);
        XSetErrorHandler(handler_xerror);
        XUngrabServer(display);
    }

    free2(client, SIZEOF(*client));
    client_focus(NULL);

    XDeleteProperty(display, root, net_atoms[NET_CLIENT_LIST]);
    for (Monitor *mon = monitors; mon; mon = mon->next) {
        for (Client *c = mon->clients; c; c = c->next) {
            XChangeProperty(display, root, net_atoms[NET_CLIENT_LIST],
                            XA_WINDOW, 32, PropModeAppend,
                            (uchar *)&(c->window), 1);
        }
    }

    monitor_arrange(monitor);
    return;
}

Client *
client_next_tiled(Client *client) {
    while (true) {
        if (client == NULL) {
            break;
        }
        if (!client->is_floating && client_is_visible(client)) {
            break;
        }

        client = client->next;
    }
    return client;
}

void
client_pop(Client *client) {
    client_detach(client);
    client_attach(client);
    client_focus(client);
    monitor_arrange(client->monitor);
    return;
}

void
client_resize(Client *client, int32 x, int32 y, int32 w, int32 h, bool interact) {
    if (client_apply_size_hints(client, &x, &y, &w, &h, interact)) {
        client_resize_apply(client, x, y, w, h);
    }
    return;
}

void
client_resize_apply(Client *client, int32 x, int32 y, int32 w, int32 h) {
    XWindowChanges window_changes;
    uint32 n = 0;

    client->old_x = client->x;
    client->old_y = client->y;
    client->old_w = client->w;
    client->old_h = client->h;

    client->x = window_changes.x = x;
    client->y = window_changes.y = y;
    client->w = window_changes.width = w;
    client->h = window_changes.height = h;

    window_changes.border_width = client->border_pixels;

    // TODO: Use client->monitor here; live_monitor may be different.
    for (Client *client_aux = client_next_tiled(live_monitor->clients);
         client_aux; client_aux = client_next_tiled(client_aux->next)) {
        n += 1;
    }

    if (!(client->is_floating)) {
        const Layout *layout = live_monitor->layout[live_monitor->lay_i];
        if (layout->function == monitor_layout_monocle || n == 1) {
            window_changes.border_width = 0;
            client->w = window_changes.width += client->border_pixels*2;
            client->h = window_changes.height += client->border_pixels*2;
        }
    }

    XConfigureWindow(display, client->window,
                     CWX | CWY | CWWidth | CWHeight | CWBorderWidth,
                     &window_changes);
    client_configure(client);
    XSync(display, False);
    return;
}

void
client_send_monitor(Client *client, Monitor *monitor) {
    if (client->monitor == monitor) {
        return;
    }

    client_unfocus(client, true);
    client_detach(client);
    client_detach_stack(client);

    client->monitor = monitor;
    client->tags = monitor->tagset[monitor->selected_tags];

    client_attach(client);
    client_attach_stack(client);
    client_set_client_tag_prop(client);
    client_focus(NULL);

    monitor_arrange(NULL);
    return;
}

void
client_set_client_state(Client *client, int64 state) {
    int64 data[] = {state, None};

    XChangeProperty(display, client->window, wm_atoms[WM_STATE],
                    wm_atoms[WM_STATE], 32, PropModeReplace, (uchar *)data, 2);
    return;
}

bool
client_send_event(Client *client, Atom proto) {
    int32 n;
    Atom *protocols;
    bool exists = false;
    XEvent event;

    if (XGetWMProtocols(display, client->window, &protocols, &n)) {
        while (n--) {
            if (protocols[n] == proto) {
                exists = true;
                break;
            }
        }
        XFree(protocols);
    }
    if (exists) {
        event.type = ClientMessage;
        event.xclient.window = client->window;
        event.xclient.message_type = wm_atoms[WM_PROTOCOLS];
        event.xclient.format = 32;
        event.xclient.data.l[0] = (int64)proto;
        event.xclient.data.l[1] = CurrentTime;
        XSendEvent(display, client->window, False, NoEventMask, &event);
    }
    return exists;
}

void
client_set_focus(Client *client) {
    if (!client->never_focus) {
        XSetInputFocus(display, client->window, RevertToPointerRoot,
                       CurrentTime);
        XChangeProperty(display, root, net_atoms[NET_ACTIVE_WINDOW], XA_WINDOW,
                        32, PropModeReplace, (uchar *)&(client->window), 1);
    }
    client_send_event(client, wm_atoms[WM_TAKE_FOCUS]);
    return;
}

void
client_set_fullscreen(Client *client, bool fullscreen) {
    if (fullscreen && !client->is_fullscreen) {
        XChangeProperty(display, client->window, net_atoms[NET_WM_STATE],
                        XA_ATOM, 32, PropModeReplace,
                        (uchar *)&net_atoms[NET_WM_STATE_FULLSCREEN], 1);
        client->is_fullscreen = true;
        if (client->is_fake_fullscreen) {
            client_resize_apply(client, client->x, client->y, client->w,
                                client->h);
            monitor_arrange(client->monitor);
            return;
        }
        client->was_floating = client->is_floating;
        // TODO: This overwrites the original X border saved for unmanage.
        client->old_border_pixels = client->border_pixels;
        client->border_pixels = 0;
        client->is_floating = true;

        client_resize_apply(client, client->monitor->mon_x,
                            client->monitor->mon_y, client->monitor->mon_w,
                            client->monitor->mon_h);
        XRaiseWindow(display, client->window);
    } else if (!fullscreen && client->is_fullscreen) {
        XChangeProperty(display, client->window, net_atoms[NET_WM_STATE],
                        XA_ATOM, 32, PropModeReplace, (uchar *)0, 0);
        client->is_fullscreen = false;
        if (client->is_fake_fullscreen) {
            client_resize_apply(client, client->x, client->y, client->w,
                                client->h);
            monitor_arrange(client->monitor);
            return;
        }
        client->is_floating = client->was_floating;
        client->border_pixels = client->old_border_pixels;

        client->x = client->old_x;
        client->y = client->old_y;
        client->w = client->old_w;
        client->h = client->old_h;

        client_resize_apply(client, client->x, client->y, client->w, client->h);
        monitor_arrange(client->monitor);
    }
    return;
}

void
client_update_window_type(Client *client) {
    Atom state;
    Atom window_type;

    state = client_get_atom_property(client, net_atoms[NET_WM_STATE]);
    window_type
        = client_get_atom_property(client, net_atoms[NET_WM_WINDOW_TYPE]);

    if (state == net_atoms[NET_WM_STATE_FULLSCREEN]) {
        client_set_fullscreen(client, true);
    }
    if (window_type == net_atoms[NET_WM_WINDOW_TYPE_DIALOG]) {
        client->is_floating = true;
    }
    return;
}

void
client_update_wm_hints(Client *client) {
    XWMHints *wm_hints;
    bool urgent;

    if (!(wm_hints = XGetWMHints(display, client->window))) {
        return;
    }

    urgent = wm_hints->flags & XUrgencyHint;
    if (urgent && client == live_monitor->selected_client) {
        wm_hints->flags &= ~XUrgencyHint;
        XSetWMHints(display, client->window, wm_hints);
    } else {
        client->is_urgent = urgent;
        if (client->is_urgent) {
            XSetWindowBorder(display, client->window,
                             scheme[SCHEME_URGENT][ColBorder].pixel);
        }
    }

    if (wm_hints->flags & InputHint) {
        client->never_focus = !wm_hints->input;
    } else {
        client->never_focus = false;
    }

    XFree(wm_hints);
    return;
}

int32
client_pixels_width(Client *client) {
    int32 width = client->w + 2*client->border_pixels;
    return width;
}

int32
client_pixels_height(Client *client) {
    int32 height = client->h + 2*client->border_pixels;
    return height;
}

bool
client_is_visible(Client *client) {
    Monitor *monitor = client->monitor;
    uint32 monitor_tags = monitor->tagset[monitor->selected_tags];
    return client->tags & monitor_tags;
}

void
client_set_urgent(Client *client, bool urgent) {
    XWMHints *wm_hints;

    client->is_urgent = urgent;
    if (!(wm_hints = XGetWMHints(display, client->window))) {
        return;
    }

    if (urgent) {
        wm_hints->flags = wm_hints->flags | XUrgencyHint;
    } else {
        wm_hints->flags = wm_hints->flags & ~XUrgencyHint;
    }

    XSetWMHints(display, client->window, wm_hints);
    XFree(wm_hints);
    return;
}

void
client_show_hide(Client *client) {
    Monitor *mon;
    if (client == NULL) {
        return;
    }

    mon = client->monitor;

    if (client_is_visible(client)) {
        bool monitor_floating;

        if ((client->tags) && client->is_floating) {
            client_center(client);
        }

        /* show clients top down */
        XMoveWindow(display, client->window, client->x, client->y);

        monitor_floating = !mon->layout[mon->lay_i]->function;
        if ((monitor_floating || client->is_floating)
            && (!client->is_fullscreen || client->is_fake_fullscreen)) {
            client_resize(client, client->x, client->y, client->w, client->h,
                          false);
        }
        client_show_hide(client->stack_next);
    } else {
        /* hide clients bottom up */
        client_show_hide(client->stack_next);
        XMoveWindow(display, client->window, -2*client_pixels_width(client),
                    client->y);
    }
    return;
}

void
client_set_client_tag_prop(Client *client) {
    int64 data[] = {(int64)client->tags, (int64)client->monitor->num};
    XChangeProperty(display, client->window, net_atoms[NET_CLIENT_INFO],
                    XA_CARDINAL, 32, PropModeReplace, (uchar *)data,
                    LENGTH(data));
    return;
}

void
client_free_icon(Client *client) {
    if (client->icon) {
        XRenderFreePicture(display, client->icon);
        client->icon = None;
    }
    return;
}

void
client_unfocus(Client *client, bool set_focus) {
    if (client == NULL) {
        return;
    }

    client_grab_buttons(client, false);
    XSetWindowBorder(display, client->window,
                     scheme[SCHEME_NORMAL][ColBorder].pixel);

    if (set_focus) {
        XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(display, root, net_atoms[NET_ACTIVE_WINDOW]);
    }
    return;
}

void
client_update_size_hints(Client *client) {
    long supplied_return;
    bool has_maxes;
    bool mins_match_maxes;
    XSizeHints size_hints;
    int32 success;

    success = XGetWMNormalHints(display, client->window, &size_hints,
                                &supplied_return);
    if (!success) {
        /* size_hints is uninitialized,
         * ensure that size_hints.flags aren't used */
        size_hints.flags = PSize;
    }

    if (size_hints.flags & PBaseSize) {
        client->base_w = size_hints.base_width;
        client->base_h = size_hints.base_height;
    } else if (size_hints.flags & PMinSize) {
        client->base_w = size_hints.min_width;
        client->base_h = size_hints.min_height;
    } else {
        client->base_w = client->base_h = 0;
    }

    if (size_hints.flags & PResizeInc) {
        client->increment_w = size_hints.width_inc;
        client->increment_h = size_hints.height_inc;
    } else {
        client->increment_w = client->increment_h = 0;
    }

    if (size_hints.flags & PMaxSize) {
        client->max_w = size_hints.max_width;
        client->max_h = size_hints.max_height;
    } else {
        client->max_w = client->max_h = 0;
    }

    if (size_hints.flags & PMinSize) {
        client->min_w = size_hints.min_width;
        client->min_h = size_hints.min_height;
    } else if (size_hints.flags & PBaseSize) {
        client->min_w = size_hints.base_width;
        client->min_h = size_hints.base_height;
    } else {
        client->min_w = client->min_h = 0;
    }

    if (size_hints.flags & PAspect) {
        float aspect_x = (float)size_hints.min_aspect.x;
        float aspect_y = (float)size_hints.min_aspect.y;
        client->min_aspect = aspect_y / aspect_x;
        client->max_aspect = aspect_x / aspect_y;
    } else {
        client->max_aspect = client->min_aspect = 0.0;
    }

    has_maxes = client->max_w && client->max_h;
    mins_match_maxes
        = client->max_w == client->min_w && client->max_h == client->min_h;
    client->is_fixed = has_maxes && mins_match_maxes;

    client->hintsvalid = true;
    return;
}

void
client_update_title(Client *client) {
    if (!window_text_property(client->window, net_atoms[NET_WM_NAME],
                              client->name, SIZEOF(client->name))) {
        window_text_property(client->window, XA_WM_NAME, client->name,
                             SIZEOF(client->name));
    }
    if (client->name[0] == '\0') {
        strcpy(client->name, broken);
    }
    return;
}

void
client_update_icon(Client *client) {
    Window window = client->window;
    Atom actual_type_return;
    int32 actual_format_return;
    ulong nitems_return;
    ulong bytes_after_return;
    ulong *prop_return = NULL;

    ulong *pixel_find = NULL;
    uint32 *pixel_find32;
    uint32 width_find, height_find;
    uint32 icon_width, icon_height;
    uint32 area_find = 0;
    uint32 *picture_width = &client->icon_width;
    uint32 *picture_height = &client->icon_height;
    int32 success;

    client_free_icon(client);
    success = XGetWindowProperty(
        display, window, net_atoms[NET_WM_ICON], 0L, LONG_MAX, False,
        AnyPropertyType, &actual_type_return, &actual_format_return,
        &nitems_return, &bytes_after_return, (uchar **)&prop_return);
    if (success != Success) {
        return;
    }

    if (nitems_return == 0 || actual_format_return != 32) {
        XFree(prop_return);
        return;
    }

    do {
        ulong *pointer = prop_return;
        const ulong *end = prop_return + nitems_return;
        uint32 bstd = UINT32_MAX;
        uint32 d;

        while (pointer < (end - 1)) {
            uint32 max_dim;
            uint32 w = (uint32)*pointer++;
            uint32 h = (uint32)*pointer++;

            if (w >= 16384 || h >= 16384) {
                XFree(prop_return);
                return;
            }
            if ((area_find = w*h) > (end - pointer)) {
                break;
            }

            max_dim = w > h ? w : h;
            if (max_dim >= ICONSIZE && (d = max_dim - ICONSIZE) < bstd) {
                bstd = d;
                pixel_find = pointer;
            }
            pointer += area_find;
        }

        if (pixel_find) {
            break;
        }

        pointer = prop_return;
        while (pointer < (end - 1)) {
            uint32 max_dim;
            uint32 w = (uint32)*pointer++;
            uint32 h = (uint32)*pointer++;

            if (w >= 16384 || h >= 16384) {
                XFree(prop_return);
                return;
            }
            if ((area_find = w*h) > (end - pointer)) {
                break;
            }

            max_dim = w > h ? w : h;
            if ((d = ICONSIZE - max_dim) < bstd) {
                bstd = d;
                pixel_find = pointer;
            }
            pointer += area_find;
        }
    } while (false);

    if (!pixel_find) {
        XFree(prop_return);
        return;
    }

    width_find = (uint32)pixel_find[-2];
    height_find = (uint32)pixel_find[-1];
    if ((width_find == 0) || (height_find == 0)) {
        XFree(prop_return);
        return;
    }

    if (width_find <= height_find) {
        icon_height = ICONSIZE;
        icon_width = width_find*ICONSIZE / height_find;
        if (icon_width == 0) {
            icon_width = 1;
        }
    } else {
        icon_width = ICONSIZE;
        icon_height = height_find*ICONSIZE / width_find;
        if (icon_height == 0) {
            icon_height = 1;
        }
    }
    *picture_width = icon_width;
    *picture_height = icon_height;

    pixel_find32 = (uint32 *)pixel_find;
    for (uint32 i = 0; i < width_find*height_find; i += 1) {
        uint32 pixel = (uint32)pixel_find[i];
        uint8 a = pixel >> 24u;
        uint32 rb = (a*(pixel & 0xFF00FFu)) >> 8u;
        uint32 g = (a*(pixel & 0x00FF00u)) >> 8u;
        pixel_find32[i] = (rb & 0xFF00FFu) | (g & 0x00FF00u) | ((uint32)a << 24u);
    }

    client->icon
        = draw_picture_create_resized(draw, (char *)pixel_find, width_find,
                                      height_find, icon_width, icon_height);
    XFree(prop_return);
    return;
}

#endif /* CLIENT_C */
