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
#include <hb.h>
#include <hb-ft.h>

#include "dwm.h"
#include "draw.h"
#include "util.c"
#include "utf8.c"

Draw *
draw_create(Display *dpy, int screen, Window root, uint32 w, uint32 h, Visual *visual, uint32 depth, Colormap cmap)
{
    Draw *draw = malloc2_zero(SIZEOF(Draw));

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
draw_resize(Draw *draw, uint32 w, uint32 h)
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
draw_free(Draw *draw)
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
static DwmFont *
xfont_create(Draw *draw, const char *fontname, FcPattern *fontpattern)
{
    DwmFont *font;
    XftFont *xfont = NULL;
    FcPattern *pattern = NULL;
    FT_Face face;

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

    font = malloc2_zero(sizeof(DwmFont));
    font->xfont = xfont;
    font->pattern = pattern;
    font->h = (uint32) (xfont->ascent + xfont->descent);
    font->dpy = draw->dpy;

    face = XftLockFace(xfont);
    font->hbfont = hb_ft_font_create(face, NULL);
    XftUnlockFace(xfont);

    return font;
}

static void
xfont_free(DwmFont *font)
{
    if (!font)
        return;
    if (font->pattern)
        FcPatternDestroy(font->pattern);
    if (font->hbfont) {
        hb_font_destroy(font->hbfont);
    }
    XftFontClose(font->dpy, font->xfont);
    free(font);
    return;
}

DwmFont*
draw_fontset_create(Draw* draw, const char *fonts[], int64 fontcount)
{
    DwmFont *cur, *ret = NULL;

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
draw_fontset_free(DwmFont *font)
{
    if (font) {
        draw_fontset_free(font->next);
        xfont_free(font);
    }
    return;
}

void
draw_clr_create(Draw *draw, XftColor *dest, const char *clrname, uint32 alpha)
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

XftColor *
draw_scm_create(Draw *draw, const char *clrnames[], const uint32 alphas[], int64 clrcount)
{
    XftColor *ret;

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
draw_setfontset(Draw *draw, DwmFont *set)
{
    if (draw)
        draw->fonts = set;
}

void
draw_setscheme(Draw *draw, XftColor *scm)
{
    if (draw)
        draw->scheme = scm;
}

Picture
draw_picture_create_resized(Draw *draw, char *src, uint32 srcw, uint32 srch, uint32 dstw, uint32 dsth) {
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
draw_rect(Draw *draw, int x, int y, uint32 w, uint32 h, int filled, int invert)
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
draw_text(Draw *draw, int x, int y, uint32 w, uint32 h, uint32 lpad, const char *text, int invert)
{
    int render = 0;
    XftDraw *d = NULL;
    DwmFont *usedfont = NULL;
    hb_buffer_t *buffer = NULL;
    int ellipsis_x = 0;
    uint32 ellipsis_w = 0;
    enum { nomatches_len = 64 };
    static struct {
        uint32 codepoint[nomatches_len];
        uint32 idx;
        int padding;
    } nomatches;
    static uint32 ellipsis_width = 0;

    if (x || y || w || h) {
        render = 1;
    }

    if (!draw) {
        return 0;
    }
    if (render) {
        if (!draw->scheme || !w) {
            return 0;
        }
    }
    if (!text || !draw->fonts) {
        return 0;
    }

    if (!render) {
        if (invert) {
            w = (uint32)invert;
        } else {
            w = (uint32)~invert;
        }
    } else {
        if (invert) {
            XSetForeground(draw->dpy, draw->gc, draw->scheme[ColFg].pixel);
        } else {
            XSetForeground(draw->dpy, draw->gc, draw->scheme[ColBg].pixel);
        }
        XFillRectangle(draw->dpy, draw->drawable, draw->gc, x, y, w, h);
        d = XftDrawCreate(draw->dpy, draw->drawable, draw->visual, draw->cmap);
        x += lpad;
        w -= lpad;
    }

    if (!ellipsis_width) {
        if (render) {
            ellipsis_width = draw_fontset_getwidth(draw, "...");
        }
    }

    buffer = hb_buffer_create();

    while (*text) {
        uint32 utf8codepoint = 0;
        int32 utf8charlen = 0;
        DwmFont *curfont = NULL;
        DwmFont *nextfont = NULL;
        int charexists = 0;
        char *scan = NULL;
        int chunk_len = 0;

        utf8charlen = utf8_decode((char *)text, &utf8codepoint, 4);

        for (curfont = draw->fonts; curfont; curfont = curfont->next) {
            if (XftCharExists(draw->dpy, curfont->xfont, (uint32)utf8codepoint)) {
                nextfont = curfont;
                charexists = 1;
                break;
            }
        }

        if (!charexists) {
            int is_missing = 0;
            int32 k = 0;
            for (k = 0; k < nomatches_len; k += 1) {
                if (utf8codepoint == nomatches.codepoint[k]) {
                    is_missing = 1;
                    break;
                }
            }

            if (!is_missing) {
                FcCharSet *fccharset = FcCharSetCreate();
                FcPattern *fcpattern = NULL;
                XftResult result;
                FcPattern *match = NULL;

                FcCharSetAddChar(fccharset, (uint32)utf8codepoint);

                if (!draw->fonts->pattern) {
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
                    nextfont = xfont_create(draw, NULL, match);
                    if (nextfont) {
                        if (XftCharExists(draw->dpy, nextfont->xfont, (uint32)utf8codepoint)) {
                            for (curfont = draw->fonts; curfont->next; curfont = curfont->next) {
                                /* NOP */
                            }
                            curfont->next = nextfont;
                        } else {
                            xfont_free(nextfont);
                            nextfont = NULL;
                        }
                    }
                }
            }

            if (!nextfont) {
                nomatches.idx += 1;
                nomatches.codepoint[nomatches.idx % nomatches_len] = utf8codepoint;
                nextfont = draw->fonts;
            }
        }

        usedfont = nextfont;
        scan = (char *)text;

        while (*scan) {
            uint32 cp = 0;
            int32 clen = 0;
            DwmFont *f = NULL;
            int is_combo = 0;

            clen = utf8_decode(scan, &cp, 4);

            /* preserve complex emoji joiners and variation selectors in the current chunk */
            if (cp == 0x200D) {
                is_combo = 1;
            } else if (cp == 0x200C) {
                is_combo = 1;
            } else if (cp >= 0xFE00 && cp <= 0xFE0F) {
                is_combo = 1;
            } else if (cp >= 0x1F3FB && cp <= 0x1F3FF) {
                is_combo = 1;
            } else if (cp >= 0xE0020 && cp <= 0xE007F) {
                is_combo = 1;
            }

            if (is_combo) {
                f = usedfont;
            } else {
                for (f = draw->fonts; f; f = f->next) {
                    if (XftCharExists(draw->dpy, f->xfont, (uint32)cp)) {
                        break;
                    }
                }
            }

            if (f != usedfont) {
                if (f != NULL) {
                    break;
                }
                if (chunk_len > 0) {
                    break;
                }
            }

            scan += clen;
            chunk_len += clen;
        }

        if (chunk_len == 0) {
            chunk_len = utf8charlen;
        }

        hb_buffer_clear_contents(buffer);
        hb_buffer_add_utf8(buffer, text, chunk_len, 0, -1);
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(usedfont->hbfont, buffer, NULL, 0);

        uint32 glyph_count = 0;
        hb_glyph_info_t *info = NULL;
        hb_glyph_position_t *pos = NULL;

        info = hb_buffer_get_glyph_infos(buffer, &glyph_count);
        pos = hb_buffer_get_glyph_positions(buffer, &glyph_count);

        uint32 i = 0;
        int overflow = 0;
        uint32 ew = 0;
        double last_scale = 1.0;

        for (i = 0; i < glyph_count; i += 1) {
            uint32 tmpw = 0;
            FT_UInt glyph_index = info[i].codepoint;
            XGlyphInfo ext;
            int hb_adv = pos[i].x_advance >> 6;
            int xft_adv = 0;
            int scaled = 0;

            XftGlyphExtents(draw->dpy, usedfont->xfont, &glyph_index, 1, &ext);
            xft_adv = ext.xOff;

            if (hb_adv > 0) {
                if (xft_adv > 0) {
                    if (abs(hb_adv - xft_adv) > 10) {
                        last_scale = (double)xft_adv / (double)hb_adv;
                        pos[i].x_advance = (hb_position_t)(pos[i].x_advance * last_scale);
                        pos[i].y_advance = (hb_position_t)(pos[i].y_advance * last_scale);
                        pos[i].x_offset = (hb_position_t)(pos[i].x_offset * last_scale);
                        pos[i].y_offset = (hb_position_t)(pos[i].y_offset * last_scale);
                        tmpw = (uint32)xft_adv;
                        scaled = 1;
                    }
                }
            }

            if (scaled == 0) {
                if (hb_adv == 0) {
                    if (ext.xOff == 0) {
                        if (last_scale != 1.0) {
                            pos[i].x_advance = (hb_position_t)(pos[i].x_advance * last_scale);
                            pos[i].y_advance = (hb_position_t)(pos[i].y_advance * last_scale);
                            pos[i].x_offset = (hb_position_t)(pos[i].x_offset * last_scale);
                            pos[i].y_offset = (hb_position_t)(pos[i].y_offset * last_scale);
                            tmpw = 0;
                            scaled = 1;
                        }
                    }
                }
            }

            if (scaled == 0) {
                tmpw = (uint32)hb_adv;
                last_scale = 1.0;
            }

            if (ew + ellipsis_width <= w) {
                ellipsis_x = x + (int)ew;
                ellipsis_w = w - ew;
            }
            if (ew + tmpw > w) {
                overflow = 1;
                break;
            }
            ew += tmpw;
        }

        if (i > 0) {
            if (render) {
                XftGlyphFontSpec *specs = malloc2_zero(i * SIZEOF(XftGlyphFontSpec));
                int cx = x;
                int ty = y + (int)((h - usedfont->h) / 2) + usedfont->xfont->ascent;
                XftColor *fg_color = NULL;
                uint32 j = 0;

                for (j = 0; j < i; j += 1) {
                    specs[j].font = usedfont->xfont;
                    specs[j].glyph = info[j].codepoint;
                    specs[j].x = (short)(cx + (pos[j].x_offset >> 6));
                    specs[j].y = (short)(ty - (pos[j].y_offset >> 6));
                    cx += pos[j].x_advance >> 6;
                }

                if (invert) {
                    fg_color = &draw->scheme[ColBg];
                } else {
                    fg_color = &draw->scheme[ColFg];
                }

                XftDrawGlyphFontSpec(d, fg_color, specs, (int)i);
                free2(specs, i * SIZEOF(XftGlyphFontSpec));
            }
        }

        x += ew;
        w -= ew;

        if (overflow) {
            if (render) {
                draw_text(draw, ellipsis_x, y, ellipsis_w, h, 0, "...", invert);
            }
            break;
        }

        text += chunk_len;
    }

    hb_buffer_destroy(buffer);

    if (d) {
        XftDrawDestroy(d);
    }

    if (render) {
        return (int)((uint32)x + w);
    } else {
        return (int)x;
    }
}

void
draw_pic(Draw *draw, int x, int y, uint32 w, uint32 h, Picture pic)
{
    if (!draw)
        return;
    XRenderComposite(draw->dpy, PictOpOver, pic, None, draw->picture, 0, 0, 0, 0, x, y, w, h);
}

void
draw_map(Draw *draw, Window win, int x, int y, uint32 w, uint32 h)
{
    if (!draw)
        return;

    XCopyArea(draw->dpy, draw->drawable, win, draw->gc, x, y, w, h, x, y);
    XSync(draw->dpy, False);
}

uint32
draw_fontset_getwidth(Draw *draw, const char *text)
{
    if (!draw || !draw->fonts || !text)
        return 0;
    return (uint32)draw_text(draw, 0, 0, 0, 0, 0, text, 0);
}

uint32
draw_fontset_getwidth_clamp(Draw *draw, const char *text, uint32 n)
{
    uint32 tmp = 0;
    if (draw && draw->fonts && text && n)
        tmp = (uint32)draw_text(draw, 0, 0, 0, 0, 0, text, (int)n);
    return (uint32)MIN(n, tmp);
}

void
draw_font_getexts(DwmFont *font, const char *text, uint32 len, uint32 *w, uint32 *h)
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

Cursor *
draw_cur_create(Draw *draw, int shape)
{
    Cursor *cur;

    if (!draw || !(cur = malloc2_zero(sizeof(Cursor))))
        return NULL;

    *cur = XCreateFontCursor(draw->dpy, (uint32)shape);

    return cur;
}

void
draw_cur_free(Draw *draw, Cursor *cursor)
{
    if (!cursor)
        return;

    XFreeCursor(draw->dpy, *cursor);
    free(cursor);
}

#endif /* DRAW_C */
