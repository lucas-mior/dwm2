#if !defined(USER_C)
#define USER_C

#include "cbase.h"
#include "dwm.h"
#include "config.h"

static void
user_alt_tab(union Arg *arg) {
    static bool alt_tab_direction = false;
    Monitor *old_monitor = live_monitor;
    Client *client;
    bool grabbed = false;
    int32 grab_status = 1000;
    (void)arg;

    if (all_clients == NULL) {
        return;
    }

    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        monitor_focus(monitor, false);
        view_tag((uint32)~0);
        set_layout(&layouts[3]);
    }
    monitor_focus(old_monitor, false);
    client_focus(live_monitor->selected_client);
    focus_next(alt_tab_direction);

    for (int32 i = 0; i < ALT_TAB_GRAB_TRIES; i += 1) {
        if (grab_status != GrabSuccess) {
            grab_status = XGrabKeyboard(display, root, True, GrabModeAsync,
                                        GrabModeAsync, CurrentTime);
        }
        if (grab_status == GrabSuccess) {
            grabbed = XGrabButton(display, AnyButton, AnyModifier, root, False,
                                  BUTTONMASK, GrabModeAsync, GrabModeAsync,
                                  None, None);
            break;
        }
        sleep_ns(PAUSE_MILIS_AS_NANOS(5));
    }

    client = live_monitor->selected_client;
    while (grabbed) {
        XEvent event;
        Monitor *monitor;

        XNextEvent(display, &event);
        switch (event.type) {
        case ConfigureRequest:
        case DestroyNotify:
        case Expose:
        case MapRequest:
            handlers[event.type](&event);
            break;
        case KeyPress:
            if (event.xkey.keycode == tab_cycle_key) {
                focus_next(alt_tab_direction);
            } else if (event.xkey.keycode == key_j) {
                focus_direction(DIRECTION_LEFT);
            } else if (event.xkey.keycode == key_semicolon) {
                focus_direction(DIRECTION_RIGHT);
            } else if (event.xkey.keycode == key_l) {
                focus_direction(DIRECTION_UP);
            } else if (event.xkey.keycode == key_k) {
                focus_direction(DIRECTION_DOWN);
            }
            client = live_monitor->selected_client;
            break;
        case KeyRelease:
            if (event.xkey.keycode == tab_mod_key) {
                XUngrabKeyboard(display, CurrentTime);
                XUngrabButton(display, AnyButton, AnyModifier, root);
                grabbed = false;
                alt_tab_direction = !alt_tab_direction;
                if (client) {
                    view_tag(client->tags);
                }
            }
            break;
        case ButtonPress: {
            XButtonPressedEvent *button_event = &(event.xbutton);
            Client *clicked;
            monitor = window_to_monitor(button_event->window);
            if (monitor && (monitor != live_monitor)) {
                monitor_focus(monitor, true);
            }

            clicked = window_to_client(button_event->window);
            if (clicked) {
                client = clicked;
                client_focus(client);
            }
            XAllowEvents(display, AsyncBoth, CurrentTime);
            break;
        }
        case ButtonRelease:
            XUngrabKeyboard(display, CurrentTime);
            XUngrabButton(display, AnyButton, AnyModifier, root);
            grabbed = false;
            alt_tab_direction = !alt_tab_direction;
            if (client) {
                view_tag(client->tags);
            }
            break;
        default:
            break;
        }
    }
    return;
}

void
user_aspect_resize(union Arg *arg) {
    Monitor *monitor = live_monitor;
    Client *client = live_monitor->selected_client;
    float ratio;
    int32 w;
    int32 h;
    bool monitor_floating = !monitor->layout[monitor->lay_i]->function;

    if (arg == NULL) {
        return;
    }
    if (client == NULL) {
        return;
    }

    if (!client->is_floating && !monitor_floating) {
        return;
    }

    ratio = (float)client->w / (float)client->h;
    h = arg->i;
    w = (int32)(ratio*(float)h);

    XRaiseWindow(display, client->window);
    client_resize(client, client->x, client->y, client->w + w, client->h + h,
                  true);
    return;
}

void
user_focus_monitor(union Arg *arg) {
    Monitor *monitor;

    if (monitors->next == NULL) {
        return;
    }
    if ((monitor = monitor_from_direction(arg->i)) == live_monitor) {
        return;
    }

    monitor_focus(monitor, false);
    return;
}

void
user_focus_stack(union Arg *arg) {
    Client *client = NULL;

    if (live_monitor->selected_client == NULL) {
        return;
    }
    if (live_monitor->selected_client->is_fullscreen && lockfullscreen) {
        return;
    }

    if (arg->i > 0) {
        for (client = live_monitor->selected_client->next;
             client && !client_is_visible(client); client = client->next) {
            /* find next visible client */
        }
        if (client == NULL) {
            for (client = live_monitor->clients;
                 client && !client_is_visible(client); client = client->next) {
                /* find first visible client */
            }
        }
    } else {
        Client *client_aux;
        for (client_aux = live_monitor->clients;
             client_aux != live_monitor->selected_client;
             client_aux = client_aux->next) {
            if (client_is_visible(client_aux)) {
                client = client_aux;
            }
        }
        if (client == NULL) {
            for (; client_aux; client_aux = client_aux->next) {
                if (client_is_visible(client_aux)) {
                    client = client_aux;
                }
            }
        }
    }
    if (client) {
        client_focus(client);
        monitor_restack(live_monitor);
    }
    return;
}

void
user_focus_urgent(union Arg *arg) {
    (void)arg;
    for (Monitor *monitor = monitors; monitor; monitor = monitor->next) {
        Client *client;

        for (client = monitor->clients; client && !client->is_urgent;
             client = client->next) {
            /* find urgent client */
        }

        if (client) {
            int32 i = 0;
            client_unfocus(live_monitor->selected_client, false);
            live_monitor = monitor;

            while (i < LENGTH(tags) && !((1 << i) & client->tags)) {
                i += 1;
            }
            if (i < LENGTH(tags)) {
                view_tag(1 << i);
                client_focus(client);
            }
        }
    }
    return;
}

void
user_more_masters(union Arg *arg) {
    Monitor *monitor = live_monitor;
    Pertag *pertag = monitor->pertag;
    int32 number_slaves = -1;
    int32 number_masters;
    uint32 tag;

    for (Client *client = monitor->clients; client;
         client = client_next_tiled(client->next)) {
        number_slaves += 1;
    }

    number_masters = MIN(monitor->number_masters + arg->i,
                         number_slaves + 1);
    number_masters = MAX(number_masters, 0);

    tag = monitor->pertag->tag;
    monitor->number_masters = pertag->number_masters[tag] = number_masters;

    monitor_arrange(monitor);
    return;
}

void
user_kill_client(union Arg *arg) {
    Client *selected = live_monitor->selected_client;

    (void)arg;
    if (selected == NULL) {
        return;
    }

    if (!client_send_event(selected, wm_atoms[WM_DELETE_WINDOW])) {
        XGrabServer(display);
        XSetErrorHandler(handler_xerror_dummy);
        XSetCloseDownMode(display, DestroyAll);

        XKillClient(display, selected->window);
        XSync(display, False);

        XSetErrorHandler(handler_xerror);
        XUngrabServer(display);
    }
    return;
}

void
user_mouse_move(union Arg *arg) {
    Client *client;
    Monitor *monitor_aux;
    XEvent event;
    Time last_time = 0;
    int32 success;
    int32 x;
    int32 y;
    int32 ocx;
    int32 ocy;

    (void)arg;

    if ((client = live_monitor->selected_client) == NULL) {
        return;
    }

    if (client->is_fullscreen && !client->is_fake_fullscreen) {
        return;
    }

    monitor_restack(live_monitor);
    ocx = client->x;
    ocy = client->y;

    success = XGrabPointer(display, root, False, MOUSEMASK, GrabModeAsync,
                           GrabModeAsync, None, cursor[CURSOR_MOVE],
                           CurrentTime);
    if (success != GrabSuccess) {
        return;
    }

    if (!get_root_pointer(&x, &y)) {
        XUngrabPointer(display, CurrentTime);
        return;
    }

    do {
        XMaskEvent(display, MOUSEMASK | ExposureMask | SubstructureRedirectMask,
                   &event);
        switch (event.type) {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handlers[event.type](&event);
            break;
        case MotionNotify: {
            Monitor *monitor = live_monitor;
            bool is_floating = client->is_floating;
            int32 new_x = ocx + (event.xmotion.x - x);
            int32 new_y = ocy + (event.xmotion.y - y);
            int32 client_width = client_pixels_width(client);
            int32 client_height = client_pixels_height(client);
            int32 over_x[2] = {
                abs(monitor->win_x - new_x),
                abs(monitor->win_x + monitor->win_w - (new_x + client_width)),
            };
            int32 over_y[2] = {
                abs(monitor->win_y - new_y),
                abs(monitor->win_y + monitor->win_h - (new_y + client_height)),
            };

            if ((event.xmotion.time - last_time) <= (1000 / 60)) {
                continue;
            }
            last_time = event.xmotion.time;

            if (over_x[0] < snap_pixels) {
                new_x = monitor->win_x;
            } else if (over_x[1] < snap_pixels) {
                new_x = monitor->win_x + monitor->win_w - client_width;
            }

            if (over_y[0] < snap_pixels) {
                new_y = monitor->win_y;
            } else if (over_y[1] < snap_pixels) {
                new_y = monitor->win_y + monitor->win_h - client_height;
            }

            if (!is_floating && monitor->layout[monitor->lay_i]->function) {
                bool moving_x = abs(new_x - client->x) > snap_pixels;
                bool moving_y = abs(new_y - client->y) > snap_pixels;
                if (moving_x || moving_y) {
                    user_toggle_floating(NULL);
                }
            }

            if (!monitor->layout[monitor->lay_i]->function || is_floating) {
                client_resize(client, new_x, new_y, client->w, client->h, true);
            }
            break;
        }
        default:
            break;
        }
    } while (event.type != ButtonRelease);

    XUngrabPointer(display, CurrentTime);

    monitor_aux
        = monitor_from_rectangle(client->x, client->y, client->w, client->h);
    if (monitor_aux != live_monitor) {
        client_send_monitor(client, monitor_aux);
        live_monitor = monitor_aux;
        client_focus(NULL);
    }
    return;
}

void
user_mouse_resize(union Arg *arg) {
    Client *client;
    Monitor *monitor;
    XEvent event;
    Time last_time = 0;
    int32 success;

    (void)arg;

    if ((client = live_monitor->selected_client) == NULL) {
        return;
    }

    if (client->is_fullscreen && !client->is_fake_fullscreen) {
        return;
    }

    monitor_restack(live_monitor);

    success = XGrabPointer(display, root, False, MOUSEMASK, GrabModeAsync,
                           GrabModeAsync, None, cursor[CURSOR_RESIZE],
                           CurrentTime);
    if (success != GrabSuccess) {
        return;
    }

    XWarpPointer(display, None, client->window, 0, 0, 0, 0,
                 client->w + client->border_pixels,
                 client->h + client->border_pixels);
    do {
        XMaskEvent(display, MOUSEMASK | ExposureMask | SubstructureRedirectMask,
                   &event);
        switch (event.type) {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handlers[event.type](&event);
            break;
        case MotionNotify: {
            bool monitor_floating;
            int32 new_w;
            int32 new_h;
            int32 new_x;
            int32 new_y;
            bool over_x;
            bool under_x;
            bool over_y;
            bool under_y;

            if ((event.xmotion.time - last_time) <= (1000 / 60)) {
                continue;
            }
            last_time = event.xmotion.time;

            event.xmotion.x += (-client->x - 2*client->border_pixels + 1);
            event.xmotion.y += (-client->y - 2*client->border_pixels + 1);

            new_w = MAX(event.xmotion.x, 1);
            new_h = MAX(event.xmotion.y, 1);

            monitor_floating
                = !(live_monitor->layout[live_monitor->lay_i]->function);
            if (!client->is_floating && !monitor_floating) {
                bool over_snap_x = abs(new_w - client->w) > snap_pixels;
                bool over_snap_y = abs(new_h - client->h) > snap_pixels;

                new_x = client->monitor->win_x + new_w;
                new_y = client->monitor->win_y + new_h;
                over_x = new_x >= live_monitor->win_x;
                under_x = new_x <= live_monitor->win_x + live_monitor->win_w;
                over_y = new_y >= live_monitor->win_y;
                under_y = new_y <= live_monitor->win_y + live_monitor->win_h;

                if (over_x && under_x && over_y && under_y
                    && (over_snap_x || over_snap_y)) {
                    user_toggle_floating(NULL);
                }
            }
            if (client->is_floating || monitor_floating) {
                client_resize(client, client->x, client->y, new_w, new_h, true);
            }
            break;
        }
        default:
            break;
        }
    } while (event.type != ButtonRelease);

    XWarpPointer(display, None, client->window, 0, 0, 0, 0,
                 client->w + client->border_pixels - 1,
                 client->h + client->border_pixels - 1);
    XUngrabPointer(display, CurrentTime);
    while (XCheckMaskEvent(display, EnterWindowMask, &event)) {
        /* discard stale enter events */
    }

    monitor
        = monitor_from_rectangle(client->x, client->y, client->w, client->h);
    if (monitor != live_monitor) {
        client_send_monitor(client, monitor);
        live_monitor = monitor;
        client_focus(NULL);
    }
    return;
}

void
user_quit_dwm(union Arg *arg) {
    if (arg->i) {
        dwm_restart = true;
    }
    dwm_running = false;
    return;
}

void
user_set_layout(union Arg *arg) {
    set_layout(arg->v);
    return;
}

void
user_set_master_fact(union Arg *arg) {
    float factor;
    Pertag *pertag = live_monitor->pertag;

    if (arg == NULL) {
        return;
    }
    if (live_monitor->layout[live_monitor->lay_i]->function == NULL) {
        return;
    }

    if (arg->f < 1.0f) {
        factor = arg->f + live_monitor->master_fact;
    } else {
        /* arg > 1.0 will set master_fact absolutely */
        factor = arg->f - 1.0f;
    }

    if (factor < 0.05f || factor > 0.95f) {
        return;
    }

    live_monitor->master_fact = pertag->master_facts[pertag->tag] = factor;
    monitor_arrange(live_monitor);
    return;
}

void
user_signal_status_bar(union Arg *arg) {
    pid_t status_program_pid;
    union sigval signal_value;
    int32 pipefd[2];
    char buffer[32] = {0};
    int32 bytes_read;

    if (status_signal <= 0) {
        return;
    }
#if defined(SIGRTMIN)
    signal_value.sival_int = arg->i | ((SIGRTMIN + status_signal) << 3);
#else
    signal_value.sival_int = arg->i | ((status_signal) << 3);
#endif

    if (pipe(pipefd) < 0) {
        error("Error creating pipe: %s\n", strerror(errno));
        return;
    }

    switch (fork()) {
    case -1:
        error("Error forking: %s\n", strerror(errno));
        XCLOSE(&pipefd[0]);
        XCLOSE(&pipefd[1]);
        return;
    case 0:
        XCLOSE(&pipefd[0]);
        xdup2(pipefd[1], STDOUT_FILENO);
        XCLOSE(&pipefd[1]);
        execlp("pidof", "pidof", "-s", STATUS_PROGRAM, NULL);
        error("Error executing pidof.\n");
        _exit(EXIT_FAILURE);
    default:
        XCLOSE(&pipefd[1]);
        break;
    }

    if ((bytes_read
            = (int32)read64(pipefd[0], buffer, SIZEOF(buffer) - 1)) <= 0) {
        XCLOSE(&pipefd[0]);
        return;
    }
    buffer[bytes_read] = '\0';
    XCLOSE(&pipefd[0]);

    status_program_pid = (pid_t)atoi2(buffer, bytes_read);
    if (status_program_pid <= 0) {
        return;
    }
#if OS_MAC || OS_OPENBSD
    kill(status_program_pid, SIGUSR1);
#else
    sigqueue(status_program_pid, SIGUSR1, signal_value);
#endif
    return;
}

void
user_tag(union Arg *arg) {
    Client *selected_client = live_monitor->selected_client;
    uint32 which_tag = arg->ui & TAGMASK;

    if (which_tag && selected_client) {
        selected_client->tags = which_tag;
        client_set_client_tag_prop(selected_client);
        client_focus(NULL);
        monitor_arrange(live_monitor);
    }
    return;
}

void
user_tag_monitor(union Arg *arg) {
    Monitor *monitor = monitor_from_direction(arg->i);
    Client *selected = live_monitor->selected_client;

    if ((selected == NULL) || (monitors->next == NULL)) {
        return;
    }

    if (selected->is_floating) {
        selected->x += monitor->mon_x - live_monitor->mon_x;
        selected->y += monitor->mon_y - live_monitor->mon_y;
    }

    client_send_monitor(selected, monitor);
    monitor_focus(monitor, false);
    user_toggle_floating(NULL);
    user_toggle_floating(NULL);
    return;
}

void
user_toggle_bar(union Arg *arg) {
    toggle_bar(arg->i);
    return;
}

void
user_toggle_floating(union Arg *arg) {
    Client *client = live_monitor->selected_client;

    (void)arg;

    if (client == NULL) {
        return;
    }

    if (client->is_fullscreen && !client->is_fake_fullscreen) {
        return;
    }

    client->is_floating = !client->is_floating || client->is_fixed;
    if (client->is_floating) {
        client_resize(client, client->stored_fx, client->stored_fy,
                      client->stored_fw, client->stored_fh, false);
    } else {
        client->stored_fx = client->x;
        client->stored_fy = client->y;
        client->stored_fw = client->w;
        client->stored_fh = client->h;
    }
    client_center(client);
    monitor_arrange(live_monitor);
    return;
}

void
user_toggle_fullscreen(union Arg *arg) {
    Client *client = live_monitor->selected_client;

    (void)arg;
    if (client) {
        client_set_fullscreen(client, !client->is_fullscreen);
    }
    return;
}

void
user_toggle_tag(union Arg *arg) {
    uint32 newtags;

    if (live_monitor->selected_client == NULL) {
        return;
    }

    newtags = live_monitor->selected_client->tags ^ (arg->ui & TAGMASK);
    if (newtags) {
        live_monitor->selected_client->tags = newtags;
        client_set_client_tag_prop(live_monitor->selected_client);
        client_focus(NULL);
        monitor_arrange(live_monitor);
    }
    return;
}

void
user_toggle_view(union Arg *arg) {
    Monitor *monitor = live_monitor;
    Pertag *pertag = live_monitor->pertag;
    uint32 new_tags;

    if (arg->ui & TAGMASK & monitor->tagset[monitor->selected_tags]) {
        view_tag(arg->ui);
        return;
    }

    new_tags = monitor->tagset[monitor->selected_tags] ^ (arg->ui & TAGMASK);
    if (new_tags == 0) {
        return;
    }

    monitor->tagset[monitor->selected_tags] = new_tags;

    if (new_tags == TAGMASK) {
        pertag->old_tag = pertag->tag;
        pertag->tag = 0;
    } else if (pertag->tag == 0 || !(new_tags & (1u << (pertag->tag - 1)))) {
        uint32 i = 0;
        pertag->old_tag = pertag->tag;
        while (!(new_tags & (1u << i))) {
            i += 1;
        }
        pertag->tag = i + 1;
    }

    monitor_restore_pertag(monitor, pertag);

    client_focus(NULL);
    monitor_arrange(monitor);
    return;
}

void
user_view_tag(union Arg *arg) {
    view_tag(arg->ui);
    return;
}

void
user_window_view(union Arg *arg) {
    Client *client = live_monitor->selected_client;

    (void)arg;
    if (client) {
        view_tag(client->tags);
    }
    return;
}

void
user_promote_to_master(union Arg *arg) {
    Client *client = live_monitor->selected_client;
    Monitor *monitor = live_monitor;
    bool monitor_floating = !monitor->layout[monitor->lay_i]->function;
    bool is_next_tiled = client == client_next_tiled(monitor->clients);

    (void)arg;

    if (client == NULL) {
        return;
    }
    if (monitor_floating || client->is_floating) {
        return;
    }
    if (is_next_tiled
        && ((client = client_next_tiled(client->next)) == NULL)) {
        return;
    }

    client_pop(client);
    return;
}

#endif /* USER_C */
