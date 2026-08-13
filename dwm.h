#if !defined(DWM_H)
#define DWM_H

#include "cbase.h"

#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

#if defined(XINERAMA)
#include <X11/extensions/Xinerama.h>
#endif

#include <X11/Xft/Xft.h>
#include <Imlib2.h>

#if CC_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
#pragma clang diagnostic ignored "-Wreserved-identifier"
#endif

#include <hb.h>
#include <hb-ft.h>

#undef SIZEOF
#define SIZEOF(...) (int64)(sizeof(__VA_ARGS__))

#if CC_CLANG
#pragma clang diagnostic pop
#endif

#include "cbase.h"

typedef struct DwmFont {
    Display *dpy;
    uint32 h;
    XftFont *xfont;
    FcPattern *pattern;
    hb_font_t *hbfont;
    struct DwmFont *next;
} DwmFont;

enum { ColFg, ColBg, ColBorder }; /* XftColor scheme index */

typedef struct Draw {
    uint32 w;
    uint32 h;
    Display *dpy;
    int32 screen;
    Window root;
    Visual *visual;
    uint32 depth;
    Colormap cmap;
    Drawable drawable;
    Picture picture;
    GC gc;
    XftColor *scheme;
    DwmFont *fonts;
} Draw;

/* Drawable abstraction */
Draw *draw_create(
    Display *dpy,
    int32 screen,
    Window win,
    uint32 w,
    uint32 h,
    Visual *visual,
    uint32 depth,
    Colormap cmap
);
void draw_resize(Draw *draw, uint32 w, uint32 h);
void draw_free(Draw *draw);

/* DwmFont abstraction */
DwmFont *draw_fontset_create(Draw *draw, char *fonts[], int64 fontcount);
void draw_fontset_free(DwmFont *set);
uint32 draw_fontset_getwidth(Draw *draw, char *text);
uint32 draw_fontset_getwidth_clamp(
    Draw *draw,
    char *text,
    uint32 n
);
void draw_font_getexts(
    DwmFont *font,
    char *text,
    uint32 len,
    uint32 *w,
    uint32 *h
);

/* Colorscheme abstraction */
void draw_clr_create(
    Draw *draw,
    XftColor *dest,
    char *clrname,
    uint32 alpha
);
XftColor *draw_scm_create(
    Draw *draw,
    char *clrnames[],
    uint32 alphas[],
    int64 clrcount
);

/* Cursor abstraction */
Cursor draw_cur_create(Draw *draw, int32 shape);

/* Drawing context manipulation */
void draw_setfontset(Draw *draw, DwmFont *set);
void draw_setscheme(Draw *draw, XftColor *scm);

Picture draw_picture_create_resized(
    Draw *draw,
    char *src,
    uint32 src_w,
    uint32 src_h,
    uint32 dst_w,
    uint32 dst_h
);

/* Drawing functions */
void draw_rect(
    Draw *draw,
    int32 x,
    int32 y,
    uint32 w,
    uint32 h,
    int32 filled,
    int32 invert
);
int32 draw_text(
    Draw *draw,
    int32 x,
    int32 y,
    uint32 w,
    uint32 h,
    uint32 lpad,
    char *text,
    int32 invert
);
void draw_pic(
    Draw *draw,
    int32 x,
    int32 y,
    uint32 w,
    uint32 h,
    Picture pic
);

/* Map functions */
void draw_map(
    Draw *draw,
    Window win,
    int32 x,
    int32 y,
    uint32 w,
    uint32 h
);

#define BUTTONMASK (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         \
    (mask & ~(numlock_mask|LockMask) \
    & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define MOUSEMASK (BUTTONMASK|PointerMotionMask)
#define TAGMASK   ((1u << (LENGTH(tags))) - 1u)
#define PAUSE_MILIS_AS_NANOS(X) ((X)*1000*1000)

#define TAG_DISPLAY_SIZE 32
#define ALT_TAB_GRAB_TRIES 10
#define STATUS_BUFFER_SIZE 200
#define STATUS_MAX_BLOCKS 40

// Note: must be kept in sync with dwmblocks2
#define STATUS_PROGRAM "dwmblocks2"
#define DWM_BAR_SEPARATOR ((char) 0x01)

#define NET_INTERN_ATOM(X) do { \
    net_atoms[X] = XInternAtom(display, "_"#X, False); \
} while (0)

#define WM_INTERN_ATOM(X) do { \
    wm_atoms[X] = XInternAtom(display, #X, False); \
} while (0)

enum {
    NET_SUPPORTED,
    NET_WM_NAME,
    NET_WM_ICON,
    NET_WM_STATE,
    NET_SUPPORTING_WM_CHECK,
    NET_WM_STATE_FULLSCREEN,
    NET_ACTIVE_WINDOW,
    NET_WM_WINDOW_TYPE,
    NET_WM_WINDOW_TYPE_DIALOG,
    NET_CLIENT_LIST,
    NET_CLIENT_INFO,
    NET_LAST
};
enum {
    WM_PROTOCOLS,
    WM_DELETE_WINDOW,
    WM_STATE,
    WM_TAKE_FOCUS,
    WM_LAST
};

enum {
    BAR_BOTTOM,
    BAR_TOP
};

enum Direction {
    DIRECTION_LEFT,
    DIRECTION_RIGHT,
    DIRECTION_UP,
    DIRECTION_DOWN,
};

enum {
    CURSOR_NORMAL,
    CURSOR_RESIZE,
    CURSOR_MOVE,
    CURSOR_LAST
};
enum {
    SCHEME_NORMAL,
    SCHEME_INVERSE,
    SCHEME_SELECTED,
    SCHEME_URGENT
};
enum {
    CLICK_BAR_TAGS,
    CLICK_BAR_LAYOUT_SYMBOL,
    CLICK_BAR_STATUS,
    CLICK_BAR_TITLE,

    CLICK_BOTTOM_BAR,
    CLICK_CLIENT_WIN,
    CLICK_ROOT_WIN,
    CLICK_LAST
};

union Arg {
    int32 i;
    uint32 ui;
    float f;
    void *v;
};

typedef struct Button {
    uint32 click;
    uint32 mask;
    int64 button;
    void (*function)(union Arg *arg);
    union Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
    char name[256];
    Client *next;
    Client *stack_next;
    Client *all_next;
    Monitor *monitor;
    Picture icon;
    float min_aspect;
    float max_aspect;

    int32 x;
    int32 y;
    int32 w;
    int32 h;
    int32 stored_fx;
    int32 stored_fy;
    int32 stored_fw;
    int32 stored_fh;
    int32 old_x;
    int32 old_y;
    int32 old_w;
    int32 old_h;
    int32 base_w;
    int32 base_h;
    int32 increment_w;
    int32 increment_h;
    int32 max_w;
    int32 max_h;
    int32 min_w;
    int32 min_h;
    int32 border_pixels;
    int32 original_border_pixels;
    int32 fullscreen_border_pixels;
    uint32 tags;

    uint32 icon_width;
    uint32 icon_height;

    bool hintsvalid;
    bool is_fixed;
    bool is_floating;
    bool is_urgent;
    Window window;
    bool never_focus;
    bool was_floating;
    bool is_fullscreen;
    bool is_fake_fullscreen;
};

typedef struct Key {
    uint32 mod;
    KeySym keysym;
    void (*function)(union Arg *);
    union Arg arg;
} Key;

typedef struct Layout {
    char *symbol;
    void (*function)(Monitor *);
} Layout;

typedef struct Pertag Pertag;
struct Monitor {
    char layout_symbol[16];
    Layout *layout[2];

    Client *clients;
    Client *selected_client;
    Client *stack;
    Monitor *next;
    Pertag *pertag;

    uint32 tagset[2];

    float master_fact;
    int32 number_masters;
    int32 num;
    int32 top_bar_y;
    int32 bottom_bar_y;
    int32 mon_x;
    int32 mon_y;
    int32 mon_w;
    int32 mon_h;
    int32 win_x;
    int32 win_y;
    int32 win_w;
    int32 win_h;

    uint32 selected_tags;
    uint32 lay_i;

    bool show_top_bar;
    bool show_bottom_bar;
    Window top_bar_window;
    Window bottom_bar_window;
};

typedef struct Rule {
    char *class;
    char *instance;
    char *title;

    uint32 tags;
    uint32 switchtotag;
    int32 monitor;
    bool is_floating;
    bool is_fake_fullscreen;
} Rule;

typedef struct BlockSignal {
    int32 min_x;
    int32 max_x;
    int32 signal;
    int32 text_i;
} BlockSignal;

typedef struct StatusBar {
    char text[STATUS_BUFFER_SIZE*2];
    int32 pixels;
    int32 number_blocks;
    BlockSignal blocks_signal[STATUS_MAX_BLOCKS];
} StatusBar;

static StatusBar status_top = {0};
static StatusBar status_bottom = {0};
static int32 status_signal;

static void user_alt_tab(union Arg *);
static void user_aspect_resize(union Arg *);
static void user_focus_monitor(union Arg *);
static void user_focus_stack(union Arg *);
static void user_focus_urgent(union Arg *);
static void user_kill_client(union Arg *);
static void user_more_masters(union Arg *);
static void user_mouse_move(union Arg *);
static void user_mouse_resize(union Arg *);
static void user_promote_to_master(union Arg *);
static void user_quit_dwm(union Arg *);
static void user_set_layout(union Arg *);
static void user_set_master_fact(union Arg *);
static void user_signal_status_bar(union Arg *);
static void user_tag(union Arg *);
static void user_tag_monitor(union Arg *);
static void user_toggle_bar(union Arg *);
static void user_toggle_floating(union Arg *);
static void user_toggle_fullscreen(union Arg *);
static void user_toggle_tag(union Arg *);
static void user_toggle_view(union Arg *);
static void user_view_tag(union Arg *);
static void user_window_view(union Arg *);

static int32 handler_xerror(Display *, XErrorEvent *);
static int32 handler_xerror_dummy(Display *, XErrorEvent *);
static int32 handler_xerror_start(Display *, XErrorEvent *);
static void handler_button_press(XEvent *);
static void handler_client_message(XEvent *);
static void handler_configure_notify(XEvent *);
static void handler_configure_request(XEvent *);
static void handler_destroy_notify(XEvent *);
static void handler_enter_notify(XEvent *);
static void handler_expose(XEvent *);
static void handler_focus_in(XEvent *);
static void handler_key_press(XEvent *);
static void handler_map_request(XEvent *);
static void handler_mapping_notify(XEvent *);
static void handler_motion_notify(XEvent *);
static void handler_others(XEvent *);
static void handler_property_notify(XEvent *);
static void handler_unmap_notify(XEvent *);

static Atom client_get_atom_property(Client *, Atom);
static Client *client_next_tiled(Client *);
static bool client_is_visible(Client *);
static bool client_send_event(Client *, Atom);
static int32 client_apply_size_hints(
    Client *,
    int32 *,
    int32 *,
    int32 *,
    int32 *,
    bool
);
static int32 client_pixels_height(Client *);
static int32 client_pixels_width(Client *);
static void client_apply_rules(Client *);
static void client_attach(Client *);
static void client_attach_stack(Client *);
static void client_center(Client *);
static void client_configure(Client *);
static void client_detach(Client *);
static void client_detach_stack(Client *);
static void client_focus(Client *);
static void client_free_icon(Client *);
static void client_grab_buttons(Client *, bool);
static void client_new(Window, XWindowAttributes *);
static void client_pop(Client *);
static void client_resize(Client *, int32, int32, int32, int32, bool);
static void client_resize_apply(Client *, int32, int32, int32, int32);
static void client_send_monitor(Client *, Monitor *);
static void client_set_client_state(Client *, int64);
static void client_set_client_tag_prop(Client *);
static void client_set_focus(Client *);
static void client_set_fullscreen(Client *, bool);
static void client_set_urgent(Client *, bool);
static void client_show_hide(Client *);
static void client_unfocus(Client *, bool);
static void client_unmanage(Client *, int32);
static void client_update_icon(Client *);
static void client_update_size_hints(Client *);
static void client_update_title(Client *);
static void client_update_window_type(Client *);
static void client_update_wm_hints(Client *);

static void monitor_arrange(Monitor *);
static void monitor_arrange_monitor(Monitor *);
static void monitor_cleanup_monitor(Monitor *);
static void monitor_draw_bars(Monitor *);
static void monitor_layout_columns(Monitor *);
static void monitor_layout_grid(Monitor *);
static void monitor_layout_monocle(Monitor *);
static void monitor_layout_tile(Monitor *);
static void monitor_restack(Monitor *);
static void monitor_update_bar_position(Monitor *);
static void monitor_focus(Monitor *, bool);
static void monitor_restore_pertag(Monitor *, Pertag *);

static Monitor *monitor_create(void);
static Monitor *monitor_from_direction(int32);
static Monitor *monitor_from_rectangle(int32, int32, int32, int32);

static Monitor *window_to_monitor(Window);
static Client *window_to_client(Window);
static int32 window_text_property(Window, Atom, char *, uint32);
static int64 window_state(Window);

static void set_layout(Layout *);
static int32 get_root_pointer(int32 *, int32 *);
static int32 get_text_pixels(char *);
static int32 update_geometry(void);
static void configure_bars_windows(void);
static void draw_bars(void);
static void draw_status_text(StatusBar *, int32);
static void focus_direction(enum Direction);
static void focus_next(bool);
static void grab_keys(void);
static void scan_windows_once(void);
static void setup_once(void);
static void status_get_signal_number(StatusBar *, int32);
static void status_parse_text(StatusBar *);
static void toggle_bar(int32);
static void update_numlock_mask(void);
static void status_update(void);
static void view_tag(uint32);

static char broken[] = "broken";

static int32 screen;
static int32 screen_width;
static int32 screen_height;

static uint32 bar_height;
static int32 text_padding;
static int32 (*xerrorxlib)(Display *, XErrorEvent *);
static uint32 numlock_mask = 0;

static Atom wm_atoms[WM_LAST];
static Atom net_atoms[NET_LAST];
static Display *display;
static Visual *visual;
static Colormap color_map;
static Window root;
static Window wm_check_window;
static int32 depth;

static bool dwm_restart = false;
static bool dwm_running = true;

static Cursor cursor[CURSOR_LAST];
static XftColor **scheme;
static Draw *draw;

static Monitor *monitors;
static Monitor *live_monitor;
static Client *all_clients = NULL;

#endif
