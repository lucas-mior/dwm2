#if !defined(HANDLERS_C)
#define HANDLERS_C

#include "dwm.h"

void
handler_client_message(XEvent *event) {
    XClientMessageEvent *client_message_event = &event->xclient;
    Atom message_type = client_message_event->message_type;
    Client *client = window_to_client(client_message_event->window);

    if (client == NULL) {
        return;
    }

    if (message_type == net_atoms[NET_WM_STATE]) {
        ulong *data = (ulong *)client_message_event->data.l;

        if (data[1] == net_atoms[NET_WM_STATE_FULLSCREEN]
            || data[2] == net_atoms[NET_WM_STATE_FULLSCREEN]) {
            bool net_wm_state_add = data[0] == 1;
            bool net_wm_state_toggle = data[0] == 2;
            bool fullscreen = net_wm_state_add
                              || (net_wm_state_toggle
                                  && (!client->is_fullscreen
                                      || client->is_fake_fullscreen));
            client_set_fullscreen(client, fullscreen);
        }
    } else if (message_type == net_atoms[NET_ACTIVE_WINDOW]) {
        uint32 i;

        for (i = 0; i < LENGTH(tags) && !((1 << i) & client->tags); i += 1) {
            /* find first client tag */
        }

        if (i < LENGTH(tags)) {
            union Arg a = {.ui = 1 << i};
            live_monitor = client->monitor;
            user_view_tag(&a);
            client_focus(client);
            monitor_restack(live_monitor);
        }
    }
    return;
}

void
handler_configure_request(XEvent *event) {
    Client *client;
    XConfigureRequestEvent *conf_request_event = &event->xconfigurerequest;
    XWindowChanges window_changes;
    bool mon_floating;

    // TODO: This uses live_monitor before knowing the requested client.
    // Clients on other monitors get the wrong layout floating state.
    mon_floating = !live_monitor->layout[live_monitor->lay_i]->function;

    if ((client = window_to_client(conf_request_event->window))) {
        if (conf_request_event->value_mask & CWBorderWidth) {
            client->border_pixels = conf_request_event->border_width;
            XSync(display, False);
            return;
        }

        if (client->is_floating || mon_floating) {
            Monitor *monitor;
            bool mask_xy;
            bool mask_hw;

            monitor = client->monitor;
            if (conf_request_event->value_mask & CWX) {
                client->old_x = client->x;
                client->x = monitor->mon_x + conf_request_event->x;
            }
            if (conf_request_event->value_mask & CWY) {
                client->old_y = client->y;
                client->y = monitor->mon_y + conf_request_event->y;
            }
            if (conf_request_event->value_mask & CWWidth) {
                client->old_w = client->w;
                client->w = conf_request_event->width;
            }
            if (conf_request_event->value_mask & CWHeight) {
                client->old_h = client->h;
                client->h = conf_request_event->height;
            }

            if (client->is_floating) {
                Monitor *m = monitor;
                int32 client_width = client_pixels_width(client);
                int32 client_height = client_pixels_height(client);

                if ((client->x + client->w) > (m->mon_x + m->mon_w)) {
                    client->x = m->mon_x + (m->mon_w / 2 - client_width / 2);
                }
                if ((client->y + client->h)
                    > (monitor->mon_y + monitor->mon_h)) {
                    client->y = m->mon_y + (m->mon_h / 2 - client_height / 2);
                }
            }

            mask_xy = conf_request_event->value_mask & (CWX | CWY);
            mask_hw = conf_request_event->value_mask & (CWWidth | CWHeight);
            if (mask_xy && !mask_hw) {
                client_configure(client);
            }

            if (client_is_visible(client)) {
                XMoveResizeWindow(display, client->window, client->x, client->y,
                                  (uint32)client->w, (uint32)client->h);
            }
        } else {
            client_configure(client);
        }
    } else {
        window_changes.x = conf_request_event->x;
        window_changes.y = conf_request_event->y;
        window_changes.width = conf_request_event->width;
        window_changes.height = conf_request_event->height;
        window_changes.border_width = conf_request_event->border_width;
        window_changes.sibling = conf_request_event->above;
        window_changes.stack_mode = conf_request_event->detail;

        XConfigureWindow(display, conf_request_event->window,
                         (uint32)conf_request_event->value_mask,
                         &window_changes);
    }
    XSync(display, False);
    return;
}

void
handler_configure_notify(XEvent *event) {
    XConfigureEvent *configure_event = &event->xconfigure;
    int32 dirty;

    if (configure_event->window != root) {
        return;
    }

    /* TODO: update_geometry handling sucks, needs to be simplified */
    dirty = screen_width != configure_event->width
            || screen_height != configure_event->height;
    screen_width = configure_event->width;
    screen_height = configure_event->height;

    if (update_geometry() || dirty) {
        draw_resize(draw, (uint32)screen_width, bar_height);
        configure_bars_windows();
        for (Monitor *mon = monitors; mon; mon = mon->next) {
            for (Client *client = mon->clients; client; client = client->next) {
                if (client->is_fullscreen && !client->is_fake_fullscreen) {
                    client_resize_apply(client, mon->mon_x, mon->mon_y,
                                        mon->mon_w, mon->mon_h);
                }
            }
            XMoveResizeWindow(display, mon->top_bar_window, mon->win_x,
                              mon->top_bar_y, (uint32)mon->win_w, bar_height);
            XMoveResizeWindow(display, mon->bottom_bar_window, mon->win_x,
                              mon->bottom_bar_y, (uint32)mon->win_w,
                              bar_height);
        }
        client_focus(NULL);
        monitor_arrange(NULL);
    }
    return;
}

void
handler_destroy_notify(XEvent *event) {
    Client *client;
    XDestroyWindowEvent *destroy_window_event = &event->xdestroywindow;

    if ((client = window_to_client(destroy_window_event->window))) {
        client_unmanage(client, 1);
    }
    return;
}

void
handler_enter_notify(XEvent *event) {
    Client *client;
    Monitor *monitor;
    XCrossingEvent *crossing_event = &event->xcrossing;
    bool is_root = crossing_event->window == root;
    bool notify_normal = crossing_event->mode == NotifyNormal;
    bool notify_inferior = crossing_event->detail == NotifyInferior;

    if (!is_root && (!notify_normal || notify_inferior)) {
        return;
    }

    if ((client = window_to_client(crossing_event->window))) {
        monitor = client->monitor;
    } else {
        monitor = window_to_monitor(crossing_event->window);
    }

    if (monitor != live_monitor) {
        client_unfocus(live_monitor->selected_client, true);
        live_monitor = monitor;
    } else if (client == live_monitor->selected_client) {
        return;
    } else if (client == NULL) {
        return;
    }
    client_focus(client);
    return;
}

/* there are some broken focus acquiring clients needing extra handling */
void
handler_focus_in(XEvent *event) {
    XFocusChangeEvent *focus_change_event = &event->xfocus;

    if (live_monitor->selected_client == NULL) {
        return;
    }

    if (focus_change_event->window != live_monitor->selected_client->window) {
        client_set_focus(live_monitor->selected_client);
    }
    return;
}

void
handler_expose(XEvent *event) {
    Monitor *monitor;
    XExposeEvent *expose_event = &event->xexpose;

    if (expose_event->count != 0) {
        return;
    }
    if ((monitor = window_to_monitor(expose_event->window))) {
        monitor_draw_bars(monitor);
    }
    return;
}

void
handler_key_press(XEvent *event) {
    KeySym keysym;
    XKeyEvent *key_event = &event->xkey;
    keysym = XKeycodeToKeysym(display, (KeyCode)key_event->keycode, 0);

    for (uint32 i = 0; i < LENGTH(keys); i += 1) {
        if (keysym == keys[i].keysym
            && CLEANMASK(keys[i].mod) == CLEANMASK(key_event->state)
            && keys[i].function) {
            keys[i].function(&(keys[i].arg));
        }
    }
    return;
}

void
handler_mapping_notify(XEvent *event) {
    XMappingEvent *mapping_event = &event->xmapping;

    XRefreshKeyboardMapping(mapping_event);
    if (mapping_event->request == MappingKeyboard) {
        grab_keys();
    }
    return;
}

void
handler_map_request(XEvent *event) {
    static XWindowAttributes window_attributes;
    XMapRequestEvent *map_request_event = &event->xmaprequest;
    int32 success;

    success = XGetWindowAttributes(display, map_request_event->window,
                                   &window_attributes);
    if (success == 0) {
        return;
    }

    if (window_attributes.override_redirect) {
        return;
    }

    if (window_to_client(map_request_event->window) == NULL) {
        client_new(map_request_event->window, &window_attributes);
    }
    return;
}

void
handler_motion_notify(XEvent *event) {
    static Monitor *monitor_save = NULL;
    Monitor *m;
    XMotionEvent *motion_event = &event->xmotion;

    if (motion_event->window != root) {
        return;
    }

    m = monitor_from_rectangle(motion_event->x_root, motion_event->y_root, 1,
                               1);
    if (m != monitor_save && monitor_save) {
        monitor_focus(m, true);
    }

    monitor_save = m;
    return;
}

void
handler_property_notify(XEvent *event) {
    Client *client;
    Window trans;
    XPropertyEvent *property_event = &event->xproperty;

    if ((property_event->window == root)
        && (property_event->atom == XA_WM_NAME)) {
        status_update();
        monitor_draw_bars(live_monitor);
        return;
    }
    if (property_event->state == PropertyDelete) {
        return;
    }

    if ((client = window_to_client(property_event->window)) == NULL) {
        return;
    }

    switch (property_event->atom) {
    case XA_WM_TRANSIENT_FOR:
        if (!client->is_floating) {
            if (XGetTransientForHint(display, client->window, &trans)) {
                if (window_to_client(trans)) {
                    client->is_floating = true;
                    monitor_arrange(client->monitor);
                }
            }
        }
        break;
    case XA_WM_NORMAL_HINTS:
        client->hintsvalid = false;
        break;
    case XA_WM_HINTS:
        client_update_wm_hints(client);
        draw_bars();
        break;
    default:
        break;
    }

    if (property_event->atom == XA_WM_NAME
        || property_event->atom == net_atoms[NET_WM_NAME]) {
        client_update_title(client);
        if (client == client->monitor->selected_client) {
            monitor_draw_bars(client->monitor);
        }
    } else if (property_event->atom == net_atoms[NET_WM_ICON]) {
        client_update_icon(client);
        if (client == client->monitor->selected_client) {
            monitor_draw_bars(client->monitor);
        }
    }
    if (property_event->atom == net_atoms[NET_WM_WINDOW_TYPE]) {
        client_update_window_type(client);
    }
    return;
}

void
handler_unmap_notify(XEvent *event) {
    Client *client;
    XUnmapEvent *unmap_event = &event->xunmap;

    if ((client = window_to_client(unmap_event->window))) {
        if (unmap_event->send_event) {
            client_set_client_state(client, WithdrawnState);
        } else {
            client_unmanage(client, 0);
        }
    }
    return;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 *ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handlers, which may call exit. */
int32
handler_xerror(Display *error_display, XErrorEvent *error_event) {
    uchar error_code = error_event->error_code;
    uchar request_code = error_event->request_code;
    (void)error_display;

    if (error_code == BadWindow) {
        error("BadWindow");
        return 0;
    }

    switch (request_code) {
    case X_SetInputFocus:
        if (error_code == BadMatch) {
            error("X_SetInputFocus -> BadMatch");
            return 0;
        } else {
            error("X_SetInputFocus");
            goto default_handlers;
        }
    case X_PolyText8:
        if (error_code == BadDrawable) {
            error("X_PolyText8 -> BadDrawable");
            return 0;
        } else {
            error("X_PolyText8");
            goto default_handlers;
        }
    case X_PolyFillRectangle:
        if (error_code == BadDrawable) {
            error("X_PolyFillRectangle -> BadDrawable");
            return 0;
        } else {
            error("X_PolyFillRectangle");
            goto default_handlers;
        }
    case X_PolySegment:
        if (error_code == BadDrawable) {
            error("X_PolySegment -> BadDrawable");
            return 0;
        } else {
            error("X_PolySegment");
            goto default_handlers;
        }
    case X_ConfigureWindow:
        if (error_code == BadMatch) {
            error("X_ConfigureWindow -> BadMatch");
            return 0;
        } else {
            error("X_ConfigureWindow");
            goto default_handlers;
        }
    case X_GrabButton:
        if (error_code == BadAccess) {
            error("X_GrabButton -> BadAccess");
            return 0;
        } else {
            error("X_GrabButton");
            goto default_handlers;
        }
    case X_GrabKey:
        if (error_code == BadAccess) {
            error("X_GrabKey -> BadAccess");
            return 0;
        } else {
            goto default_handlers;
        }
    case X_CopyArea:
        if (error_code == BadDrawable) {
            error("X_CopyArea -> BadDrawable");
            return 0;
        } else {
            error("X_CopyArea");
            goto default_handlers;
        }
    default:
        error("Fatal error: request code=%d, error code=%d\n", request_code,
              error_code);
        goto default_handlers;
    }

default_handlers:
    return xerrorxlib(display, error_event); /* may call exit */
}

int32
handler_xerror_dummy(Display *error_display, XErrorEvent *error_event) {
    (void)error_display;
    (void)error_event;
    return 0;
}

int32
handler_xerror_start(Display *error_display, XErrorEvent *error_event) {
    (void)error_display;
    (void)error_event;
    error("Error starting dwm: another window manager is running.\n");
    exit(EXIT_FAILURE);
}

#endif
