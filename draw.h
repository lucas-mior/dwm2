/* See LICENSE file for copyright and license details. */

typedef struct {
	Cursor cursor;
} Cur;

typedef struct Fnt {
	Display *dpy;
	unsigned int h;
	XftFont *xfont;
	FcPattern *pattern;
	struct Fnt *next;
} Fnt;

enum { ColFg, ColBg, ColBorder }; /* Clr scheme index */
typedef XftColor Clr;

typedef struct {
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
	Clr *scheme;
	Fnt *fonts;
} Drw;

/* Drawable abstraction */
Drw *draw_create(Display *dpy, int screen, Window win, unsigned int w, unsigned int h, Visual *visual, unsigned int depth, Colormap cmap);
void draw_resize(Drw *draw, unsigned int w, unsigned int h);
void draw_free(Drw *draw);

/* Fnt abstraction */
Fnt *draw_fontset_create(Drw* draw, const char *fonts[], size_t fontcount);
void draw_fontset_free(Fnt* set);
unsigned int draw_fontset_getwidth(Drw *draw, const char *text);
unsigned int draw_fontset_getwidth_clamp(Drw *draw, const char *text, unsigned int n);
void draw_font_getexts(Fnt *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h);

/* Colorscheme abstraction */
void draw_clr_create(Drw *draw, Clr *dest, const char *clrname, unsigned int alpha);
Clr *draw_scm_create(Drw *draw, const char *clrnames[], const unsigned int alphas[], size_t clrcount);

/* Cursor abstraction */
Cur *draw_cur_create(Drw *draw, int shape);
void draw_cur_free(Drw *draw, Cur *cursor);

/* Drawing context manipulation */
void draw_setfontset(Drw *draw, Fnt *set);
void draw_setscheme(Drw *draw, Clr *scm);

Picture draw_picture_create_resized(Drw *draw, char *src, unsigned int src_w, unsigned int src_h, unsigned int dst_w, unsigned int dst_h);

/* Drawing functions */
void draw_rect(Drw *draw, int x, int y, unsigned int w, unsigned int h, int filled, int invert);
int draw_text(Drw *draw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, const char *text, int invert);
void draw_pic(Drw *draw, int x, int y, unsigned int w, unsigned int h, Picture pic);

/* Map functions */
void draw_map(Drw *draw, Window win, int x, int y, unsigned int w, unsigned int h);
