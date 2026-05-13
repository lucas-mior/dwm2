/* See LICENSE file for copyright and license details. */

#if !defined(DRAW_C)
#define DRAW_C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <Imlib2.h>

#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */

#include <X11/Xft/Xft.h>

#include "dwm.h"
#include "draw.h"
#include "util.c"

#define BETWEEN(X, A, B)        ((A) <= (X) && (X) <= (B))

#define UTF_INVALID 0xFFFD
#define UTF_SIZ     4

static const unsigned char utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static const long utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};
static const long utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

static long
utf8decodebyte(const char c, int64 *i)
{
	for (*i = 0; *i < (UTF_SIZ + 1); ++(*i)) {
		if (((unsigned char)c & utfmask[*i]) == utfbyte[*i])
			return (unsigned char)c & ~utfmask[*i];
	}
	return 0;
}

static int64
utf8validate(long *u, int64 i)
{
	if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for (i = 1; *u > utfmax[i]; i += 1);
	return i;
}

static int64
utf8decode(const char *c, long *u, int64 clen)
{
	int64 i, j, len, type;
	long udecoded;

	*u = UTF_INVALID;
	if (!clen)
		return 0;
	udecoded = utf8decodebyte(c[0], &len);
	if (!BETWEEN(len, 1, UTF_SIZ))
		return 1;
	for (i = 1, j = 1; i < clen && j < len; i += 1, j += 1) {
		udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
		if (type)
			return j;
	}
	if (j < len)
		return 0;
	*u = udecoded;
	utf8validate(u, len);

	return len;
}

Drw *
draw_create(Display *dpy, int screen, Window root, uint32 w, uint32 h, Visual *visual, uint32 depth, Colormap cmap)
{
	Drw *draw = malloc2_zero(SIZEOF(Drw));

	draw->dpy = dpy;
	draw->screen = screen;
	draw->root = root;
	draw->w = w;
	draw->h = h;
	draw->visual = visual;
	draw->depth = depth;
	draw->cmap = cmap;
	draw->drawable = XCreatePixmap(dpy, root, w, h, depth);
	draw->picture = XRenderCreatePicture(dpy, draw->drawable, XRenderFindVisualFormat(dpy, visual), 0, NULL);
	draw->gc = XCreateGC(dpy, draw->drawable, 0, NULL);
	XSetLineAttributes(dpy, draw->gc, 1, LineSolid, CapButt, JoinMiter);

	return draw;
}

void
draw_resize(Drw *draw, uint32 w, uint32 h)
{
	if (!draw)
		return;

	draw->w = w;
	draw->h = h;
	if (draw->picture)
		XRenderFreePicture(draw->dpy, draw->picture);
	if (draw->drawable)
		XFreePixmap(draw->dpy, draw->drawable);
	draw->drawable = XCreatePixmap(draw->dpy, draw->root, w, h, draw->depth);
	draw->picture = XRenderCreatePicture(draw->dpy, draw->drawable, XRenderFindVisualFormat(draw->dpy, draw->visual), 0, NULL);
}

void
draw_free(Drw *draw)
{
	XRenderFreePicture(draw->dpy, draw->picture);
	XFreePixmap(draw->dpy, draw->drawable);
	XFreeGC(draw->dpy, draw->gc);
	draw_fontset_free(draw->fonts);
	free(draw);
}

/* This function is an implementation detail. Library users should use
 * draw_fontset_create instead.
 */
static Fnt *
xfont_create(Drw *draw, const char *fontname, FcPattern *fontpattern)
{
	Fnt *font;
	XftFont *xfont = NULL;
	FcPattern *pattern = NULL;

	if (fontname) {
		/* Using the pattern found at font->xfont->pattern does not yield the
		 * same substitution results as using the pattern returned by
		 * FcNameParse; using the latter results in the desired fallback
		 * behaviour whereas the former just results in missing-character
		 * rectangles being drawn, at least with some fonts. */
		if (!(xfont = XftFontOpenName(draw->dpy, draw->screen, fontname))) {
			fprintf(stderr, "error, cannot load font from name: '%s'\n", fontname);
			return NULL;
		}
		if (!(pattern = FcNameParse((const FcChar8 *) fontname))) {
			fprintf(stderr, "error, cannot parse font name to pattern: '%s'\n", fontname);
			XftFontClose(draw->dpy, xfont);
			return NULL;
		}
	} else if (fontpattern) {
		if (!(xfont = XftFontOpenPattern(draw->dpy, fontpattern))) {
			fprintf(stderr, "error, cannot load font from pattern.\n");
			return NULL;
		}
	} else {
		error("Error: no font specified.");
        exit(EXIT_FAILURE);
	}

	font = malloc2_zero(sizeof(Fnt));
	font->xfont = xfont;
	font->pattern = pattern;
	font->h = (uint32) (xfont->ascent + xfont->descent);
	font->dpy = draw->dpy;

	return font;
}

static void
xfont_free(Fnt *font)
{
	if (!font)
		return;
	if (font->pattern)
		FcPatternDestroy(font->pattern);
	XftFontClose(font->dpy, font->xfont);
	free(font);
}

Fnt*
draw_fontset_create(Drw* draw, const char *fonts[], int64 fontcount)
{
	Fnt *cur, *ret = NULL;

	if (!draw || !fonts)
		return NULL;

	for (int64 i = 1; i <= fontcount; i += 1) {
		if ((cur = xfont_create(draw, fonts[fontcount - i], NULL))) {
			cur->next = ret;
			ret = cur;
		}
	}
	return (draw->fonts = ret);
}

void
draw_fontset_free(Fnt *font)
{
	if (font) {
		draw_fontset_free(font->next);
		xfont_free(font);
	}
}

void
draw_clr_create(Drw *draw, Clr *dest, const char *clrname, uint32 alpha)
{
	if (!draw || !dest || !clrname)
		return;

	if (!XftColorAllocName(draw->dpy, draw->visual, draw->cmap,
	                       clrname, dest)) {
		error("error, cannot allocate color '%s'", clrname);
        exit(EXIT_FAILURE);
    }

	dest->pixel = (dest->pixel & 0x00ffffffU) | (alpha << 24);
}

Clr *
draw_scm_create(Drw *draw, const char *clrnames[], const uint32 alphas[], int64 clrcount)
{
	Clr *ret;

	/* need at least two colors for a scheme */
	if (!draw
            || !clrnames || clrcount < 2 ||
            !(ret = malloc2_zero(clrcount*SIZEOF(XftColor))))
		return NULL;

	for (int64 i = 0; i < clrcount; i += 1)
		draw_clr_create(draw, &ret[i], clrnames[i], alphas[i]);
	return ret;
}

void
draw_setfontset(Drw *draw, Fnt *set)
{
	if (draw)
		draw->fonts = set;
}

void
draw_setscheme(Drw *draw, Clr *scm)
{
	if (draw)
		draw->scheme = scm;
}

Picture
draw_picture_create_resized(Drw *draw, char *src, uint32 srcw, uint32 srch, uint32 dstw, uint32 dsth) {
	Pixmap pm;
	Picture pic;
	GC gc;

	if (srcw <= (dstw << 1u) && srch <= (dsth << 1u)) {
		XTransform xf;
		XImage img = {
			(int)srcw, (int)srch, 0, ZPixmap, src,
			ImageByteOrder(draw->dpy), BitmapUnit(draw->dpy), BitmapBitOrder(draw->dpy), 32,
			32, 0, 32,
			0, 0, 0,
            .obdata = 0,
		};
		XInitImage(&img);

		pm = XCreatePixmap(draw->dpy, draw->root, srcw, srch, 32);
		gc = XCreateGC(draw->dpy, pm, 0, NULL);
		XPutImage(draw->dpy, pm, gc, &img, 0, 0, 0, 0, srcw, srch);
		XFreeGC(draw->dpy, gc);

		pic = XRenderCreatePicture(draw->dpy, pm, XRenderFindStandardFormat(draw->dpy, PictStandardARGB32), 0, NULL);
		XFreePixmap(draw->dpy, pm);

		XRenderSetPictureFilter(draw->dpy, pic, FilterBilinear, NULL, 0);
		xf.matrix[0][0] = (int)((srcw << 16u) / dstw); xf.matrix[0][1] = 0; xf.matrix[0][2] = 0;
		xf.matrix[1][0] = 0; xf.matrix[1][1] = (int)((srch << 16u) / dsth); xf.matrix[1][2] = 0;
		xf.matrix[2][0] = 0; xf.matrix[2][1] = 0; xf.matrix[2][2] = 65536;
		XRenderSetPictureTransform(draw->dpy, pic, &xf);
	} else {
		Imlib_Image origin;
        Imlib_Image scaled;
        origin = imlib_create_image_using_data((int)srcw, (int)srch, (DATA32 *)src);
		if (!origin)
            return None;

		imlib_context_set_image(origin);
		imlib_image_set_has_alpha(1);
		scaled = imlib_create_cropped_scaled_image(0, 0, (int)srcw, (int)srch, (int)dstw, (int)dsth);
		imlib_free_image_and_decache();
		if (!scaled)
            return None;
		imlib_context_set_image(scaled);
		imlib_image_set_has_alpha(1);

		XImage img = {
		    (int)dstw, (int)dsth, 0, ZPixmap, (char *)imlib_image_get_data_for_reading_only(),
		    ImageByteOrder(draw->dpy), BitmapUnit(draw->dpy), BitmapBitOrder(draw->dpy), 32,
		    32, 0, 32,
		    0, 0, 0,
            .obdata = 0,
		};
		XInitImage(&img);

		pm = XCreatePixmap(draw->dpy, draw->root, dstw, dsth, 32);
		gc = XCreateGC(draw->dpy, pm, 0, NULL);
		XPutImage(draw->dpy, pm, gc, &img, 0, 0, 0, 0, dstw, dsth);
		imlib_free_image_and_decache();
		XFreeGC(draw->dpy, gc);

		pic = XRenderCreatePicture(draw->dpy, pm, XRenderFindStandardFormat(draw->dpy, PictStandardARGB32), 0, NULL);
		XFreePixmap(draw->dpy, pm);
	}

	return pic;
}

void
draw_rect(Drw *draw, int x, int y, uint32 w, uint32 h, int filled, int invert)
{
	if (!draw || !draw->scheme)
		return;
	XSetForeground(draw->dpy, draw->gc, invert ? draw->scheme[ColBg].pixel : draw->scheme[ColFg].pixel);
	if (filled)
		XFillRectangle(draw->dpy, draw->drawable, draw->gc, x, y, w, h);
	else
		XDrawRectangle(draw->dpy, draw->drawable, draw->gc, x, y, w - 1, h - 1);
}

int
draw_text(Drw *draw, int x, int y, uint32 w, uint32 h, uint32 lpad, const char *text, int invert)
{
	int ty, ellipsis_x = 0;
	uint32 tmpw, ew, ellipsis_w = 0, ellipsis_len;
	XftDraw *d = NULL;
	Fnt *usedfont, *curfont, *nextfont;
	int utf8strlen, utf8charlen, render = x || y || w || h;
	long utf8codepoint = 0;
	const char *utf8str;
	FcCharSet *fccharset;
	FcPattern *fcpattern;
	FcPattern *match;
	XftResult result;
	int charexists = 0, overflow = 0;
	/* keep track of a couple codepoints for which we have no match. */
	enum { nomatches_len = 64 };
	static struct {
        long codepoint[nomatches_len];
        uint32 idx;
        int padding;
    } nomatches;
	static uint32 ellipsis_width = 0;

	if (!draw || (render && (!draw->scheme || !w)) || !text || !draw->fonts)
		return 0;

	if (!render) {
		w = invert ? (uint32)invert : (uint32)~invert;
	} else {
		XSetForeground(draw->dpy, draw->gc, draw->scheme[invert ? ColFg : ColBg].pixel);
		XFillRectangle(draw->dpy, draw->drawable, draw->gc, x, y, w, h);
		d = XftDrawCreate(draw->dpy, draw->drawable, draw->visual, draw->cmap);
		x += lpad;
		w -= lpad;
	}

	usedfont = draw->fonts;
	if (!ellipsis_width && render)
		ellipsis_width = draw_fontset_getwidth(draw, "...");
	while (1) {
		ew = ellipsis_len = utf8strlen = 0;
		utf8str = text;
		nextfont = NULL;
		while (*text) {
			utf8charlen = (int)utf8decode(text, &utf8codepoint, UTF_SIZ);
			for (curfont = draw->fonts; curfont; curfont = curfont->next) {
				charexists = charexists || XftCharExists(draw->dpy, curfont->xfont, (uint32)utf8codepoint);
				if (charexists) {
					draw_font_getexts(curfont, text, (uint32)utf8charlen, &tmpw, NULL);
					if (ew + ellipsis_width <= w) {
						/* keep track where the ellipsis still fits */
						ellipsis_x = x + (int)ew;
						ellipsis_w = w - ew;
						ellipsis_len = (uint32)utf8strlen;
					}

					if (ew + tmpw > w) {
						overflow = 1;
						/* called from draw_fontset_getwidth_clamp():
						 * it wants the width AFTER the overflow
						 */
						if (!render)
							x += tmpw;
						else
							utf8strlen = (int)ellipsis_len;
					} else if (curfont == usedfont) {
						utf8strlen += utf8charlen;
						text += utf8charlen;
						ew += tmpw;
					} else {
						nextfont = curfont;
					}
					break;
				}
			}

			if (overflow || !charexists || nextfont)
				break;
			else
				charexists = 0;
		}

		if (utf8strlen) {
			if (render) {
				ty = y + (int)((h - usedfont->h) / 2) + usedfont->xfont->ascent;
				XftDrawStringUtf8(d, &draw->scheme[invert ? ColBg : ColFg],
				                  usedfont->xfont, x, ty, (XftChar8 *)utf8str, utf8strlen);
			}
			x += ew;
			w -= ew;
		}
		if (render && overflow)
			draw_text(draw, ellipsis_x, y, ellipsis_w, h, 0, "...", invert);

		if (!*text || overflow) {
			break;
		} else if (nextfont) {
			charexists = 0;
			usedfont = nextfont;
		} else {
			/* Regardless of whether or not a fallback font is found, the
			 * character must be drawn. */
			charexists = 1;

			for (int i = 0; i < nomatches_len; i += 1) {
				/* avoid calling XftFontMatch if we know we won't find a match */
				if (utf8codepoint == nomatches.codepoint[i])
					goto no_match;
			}

			fccharset = FcCharSetCreate();
			FcCharSetAddChar(fccharset, (uint32)utf8codepoint);

			if (!draw->fonts->pattern) {
				/* Refer to the comment in xfont_create for more information. */
				error("Error: the first font in the cache must be loaded from a font string.");
                exit(EXIT_FAILURE);
			}

			fcpattern = FcPatternDuplicate(draw->fonts->pattern);
			FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
			FcPatternAddBool(fcpattern, FC_SCALABLE, FcTrue);

			FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
			FcDefaultSubstitute(fcpattern);
			match = XftFontMatch(draw->dpy, draw->screen, fcpattern, &result);

			FcCharSetDestroy(fccharset);
			FcPatternDestroy(fcpattern);

			if (match) {
				usedfont = xfont_create(draw, NULL, match);
				if (usedfont && XftCharExists(draw->dpy, usedfont->xfont, (uint32)utf8codepoint)) {
					for (curfont = draw->fonts; curfont->next; curfont = curfont->next)
						; /* NOP */
					curfont->next = usedfont;
				} else {
					xfont_free(usedfont);
                    nomatches.idx += 1; nomatches.codepoint[nomatches.idx % nomatches_len] = utf8codepoint;
no_match:
					usedfont = draw->fonts;
				}
			}
		}
	}
	if (d)
		XftDrawDestroy(d);

	return (int)((uint32)x + (render ? w : 0));
}

void
draw_pic(Drw *draw, int x, int y, uint32 w, uint32 h, Picture pic)
{
	if (!draw)
		return;
	XRenderComposite(draw->dpy, PictOpOver, pic, None, draw->picture, 0, 0, 0, 0, x, y, w, h);
}

void
draw_map(Drw *draw, Window win, int x, int y, uint32 w, uint32 h)
{
	if (!draw)
		return;

	XCopyArea(draw->dpy, draw->drawable, win, draw->gc, x, y, w, h, x, y);
	XSync(draw->dpy, False);
}

uint32
draw_fontset_getwidth(Drw *draw, const char *text)
{
	if (!draw || !draw->fonts || !text)
		return 0;
	return (uint32)draw_text(draw, 0, 0, 0, 0, 0, text, 0);
}

uint32
draw_fontset_getwidth_clamp(Drw *draw, const char *text, uint32 n)
{
	uint32 tmp = 0;
	if (draw && draw->fonts && text && n)
		tmp = (uint32)draw_text(draw, 0, 0, 0, 0, 0, text, (int)n);
	return (uint32)MIN(n, tmp);
}

void
draw_font_getexts(Fnt *font, const char *text, uint32 len, uint32 *w, uint32 *h)
{
	XGlyphInfo ext;

	if (!font || !text)
		return;

	XftTextExtentsUtf8(font->dpy, font->xfont, (XftChar8 *)text, (int)len, &ext);
	if (w)
		*w = (uint32)ext.xOff;
	if (h)
		*h = font->h;
}

Cur *
draw_cur_create(Drw *draw, int shape)
{
	Cur *cur;

	if (!draw || !(cur = malloc2_zero(sizeof(Cur))))
		return NULL;

	cur->cursor = XCreateFontCursor(draw->dpy, (uint32)shape);

	return cur;
}

void
draw_cur_free(Drw *draw, Cur *cursor)
{
	if (!cursor)
		return;

	XFreeCursor(draw->dpy, cursor->cursor);
	free(cursor);
}

#endif /* DRAW_C */
