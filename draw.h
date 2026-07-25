/* See LICENSE file for copyright and license details. */

#if !defined(DRAW_H)
#define DRAW_H

#include "dwm.h"
#include "cbase.h"

#include <X11/Xft/Xft.h>
#include <hb.h>
#include <hb-ft.h>

typedef struct DwmFont {
    Display *dpy;
    unsigned int h;
    XftFont *xfont;
    FcPattern *pattern;
    hb_font_t *hbfont;
    struct DwmFont *next;
} DwmFont;

enum { ColFg, ColBg, ColBorder }; /* XftColor scheme index */

typedef struct Draw {
    unsigned int w, h;
    Display *dpy;
    int screen;
    Window root;
    Visual *visual;
    unsigned int depth;
    Colormap cmap;
    Drawable drawable;
    Picture picture;
    GC gc;
    XftColor *scheme;
    DwmFont *fonts;
} Draw;

/* Drawable abstraction */
Draw *draw_create(Display *dpy, int screen, Window win, unsigned int w, unsigned int h, Visual *visual, unsigned int depth, Colormap cmap);
void draw_resize(Draw *draw, unsigned int w, unsigned int h);
void draw_free(Draw *draw);

/* DwmFont abstraction */
DwmFont *draw_fontset_create(Draw* draw, const char *fonts[], int64 fontcount);
void draw_fontset_free(DwmFont* set);
unsigned int draw_fontset_getwidth(Draw *draw, const char *text);
unsigned int draw_fontset_getwidth_clamp(Draw *draw, const char *text, unsigned int n);
void draw_font_getexts(DwmFont *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h);

/* Colorscheme abstraction */
void draw_clr_create(Draw *draw, XftColor *dest, const char *clrname, unsigned int alpha);
XftColor *draw_scm_create(Draw *draw, const char *clrnames[], const unsigned int alphas[], int64 clrcount);

/* Cursor abstraction */
Cursor draw_cur_create(Draw *draw, int shape);

/* Drawing context manipulation */
void draw_setfontset(Draw *draw, DwmFont *set);
void draw_setscheme(Draw *draw, XftColor *scm);

Picture draw_picture_create_resized(Draw *draw, char *src, unsigned int src_w, unsigned int src_h, unsigned int dst_w, unsigned int dst_h);

/* Drawing functions */
void draw_rect(Draw *draw, int x, int y, unsigned int w, unsigned int h, int filled, int invert);
int draw_text(Draw *draw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, const char *text, int invert);
void draw_pic(Draw *draw, int x, int y, unsigned int w, unsigned int h, Picture pic);

/* Map functions */
void draw_map(Draw *draw, Window win, int x, int y, unsigned int w, unsigned int h);

#endif /* DRAW_H */
