/* See LICENSE file for copyright and license details. */

#if !defined(DRAW_C)
#define DRAW_C

#include "cbase.h"
#include "dwm.h"

// adhoc solution for draw shadowing global static draw from dwm.h
#define draw draw2

static void
draw_create_pixmap(Draw *draw) {
    draw->drawable = XCreatePixmap(draw->dpy, draw->root, draw->w, draw->h,
                                   draw->depth);
    draw->picture = XRenderCreatePicture(
        draw->dpy,
        draw->drawable,
        XRenderFindVisualFormat(draw->dpy, draw->visual),
        0,
        NULL
    );
    return;
}

Draw *
draw_create(Display *dpy,
            int32 screen_here,
            Window root_here,
            uint32 w, uint32 h,
            Visual *visual_here,
            uint32 depth_here,
            Colormap cmap
) {
    Draw *draw = malloc2_zero(SIZEOF(*draw));

    draw->dpy = dpy;
    draw->screen = screen_here;
    draw->root = root_here;
    draw->visual = visual_here;
    draw->depth = depth_here;
    draw->cmap = cmap;

    draw->w = w;
    draw->h = h;

    draw_create_pixmap(draw);
    draw->gc = XCreateGC(dpy, draw->drawable, 0, NULL);
    XSetLineAttributes(dpy, draw->gc, 1, LineSolid, CapButt, JoinMiter);

    return draw;
}

void
draw_resize(Draw *draw, uint32 w, uint32 h) {
    if (draw == NULL) {
        return;
    }

    draw->w = w;
    draw->h = h;
    if (draw->picture) {
        XRenderFreePicture(draw->dpy, draw->picture);
    }
    if (draw->drawable) {
        XFreePixmap(draw->dpy, draw->drawable);
    }
    draw_create_pixmap(draw);
    return;
}

void
draw_free(Draw *draw) {
    if (draw == NULL) {
        return;
    }

    XRenderFreePicture(draw->dpy, draw->picture);
    XFreePixmap(draw->dpy, draw->drawable);
    XFreeGC(draw->dpy, draw->gc);

    draw_fontset_free(draw->fonts);

    free2(draw, SIZEOF(*draw));
    return;
}

/* This function is an implementation detail. Library users should use
 * draw_fontset_create instead.
 */
static DwmFont *
xfont_create(Draw *draw, char *fontname, FcPattern *fontpattern) {
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
        if ((xfont = XftFontOpenName(draw->dpy, draw->screen, fontname))
            == NULL) {
            error("Error: cannot load font from name: '%s'.\n", fontname);
            return NULL;
        }
        if ((pattern = FcNameParse((FcChar8 *)fontname)) == NULL) {
            error("Error: cannot parse font name to pattern: '%s'.\n",
                  fontname);
            XftFontClose(draw->dpy, xfont);
            return NULL;
        }
    } else if (fontpattern) {
        if ((xfont = XftFontOpenPattern(draw->dpy, fontpattern)) == NULL) {
            error("Error: cannot load font from pattern.\n");
            return NULL;
        }
    } else {
        error("Error: no font specified.\n");
        fatal(EXIT_FAILURE);
    }

    font = malloc2_zero(SIZEOF(*font));
    font->xfont = xfont;
    font->pattern = pattern;
    font->h = (uint32)(xfont->ascent + xfont->descent);
    font->dpy = draw->dpy;

    face = XftLockFace(xfont);
    font->hbfont = hb_ft_font_create(face, NULL);
    XftUnlockFace(xfont);

    return font;
}

static void
xfont_free(DwmFont *font) {
    if (font == NULL) {
        return;
    }

    if (font->pattern) {
        FcPatternDestroy(font->pattern);
    }
    if (font->hbfont) {
        hb_font_destroy(font->hbfont);
    }

    XftFontClose(font->dpy, font->xfont);
    free2(font, SIZEOF(*font));

    return;
}

DwmFont *
draw_fontset_create(Draw *draw, char *fonts[], int64 fontcount) {
    DwmFont *cur;
    DwmFont *ret = NULL;

    if ((draw == NULL) || (fonts == NULL)) {
        return NULL;
    }

    for (int64 i = 1; i <= fontcount; i += 1) {
        if ((cur = xfont_create(draw, fonts[fontcount - i], NULL))) {
            cur->next = ret;
            ret = cur;
        }
    }
    return (draw->fonts = ret);
}

void
draw_fontset_free(DwmFont *font) {
    if (font) {
        draw_fontset_free(font->next);
        xfont_free(font);
    }
    return;
}

void
draw_clr_create(Draw *draw, XftColor *dest, char *clrname, uint32 alpha) {
    if ((draw == NULL) || (dest == NULL) || (clrname == NULL)) {
        return;
    }

    if (!XftColorAllocName(draw->dpy, draw->visual, draw->cmap,
                           clrname, dest)) {
        error("Error: cannot allocate color '%s'.\n", clrname);
        fatal(EXIT_FAILURE);
    }

    dest->pixel = (dest->pixel & 0x00ffffffU) | (alpha << 24);
    return;
}

XftColor *
draw_scm_create(Draw *draw, char *clrnames[], uint32 alphas[], int64 clrcount) {
    XftColor *ret;

    /* need at least two colors for a scheme */
    if ((draw == NULL) || (clrnames == NULL) || (clrcount < 2)) {
        return NULL;
    }

    ret = malloc2_zero(clrcount*SIZEOF(*ret));

    for (int64 i = 0; i < clrcount; i += 1) {
        draw_clr_create(draw, &ret[i], clrnames[i], alphas[i]);
    }
    return ret;
}

void
draw_setfontset(Draw *draw, DwmFont *set) {
    if (draw) {
        draw->fonts = set;
    }
    return;
}

void
draw_setscheme(Draw *draw, XftColor *scm) {
    if (draw) {
        draw->scheme = scm;
    }
    return;
}

Picture
draw_picture_create_resized(Draw *draw,
                            char *src,
                            uint32 src_w, uint32 src_h,
                            uint32 dst_w, uint32 dst_h
) {
    enum {
        argb_depth = 32,
        argb_bits_per_pixel = 32,
        xrender_fixed_shift = 16,
        xrender_fixed_one = 1 << xrender_fixed_shift,
        xrender_scale_limit = 2,
    };

    if ((src_w <= dst_w*xrender_scale_limit)
        && (src_h <= dst_h*xrender_scale_limit)) {
        Pixmap pm;
        Picture pic;
        GC gc;
        XTransform xf;
        XImage img = {
            .width = (int32)src_w,
            .height = (int32)src_h,
            .format = ZPixmap,
            .data = src,
            .byte_order = ImageByteOrder(draw->dpy),
            .bitmap_unit = BitmapUnit(draw->dpy),
            .bitmap_bit_order = BitmapBitOrder(draw->dpy),
            .bitmap_pad = argb_bits_per_pixel,
            .depth = argb_depth,
            .bits_per_pixel = argb_bits_per_pixel,
            .obdata = 0,
        };
        XInitImage(&img);

        pm = XCreatePixmap(draw->dpy, draw->root, src_w, src_h, argb_depth);
        gc = XCreateGC(draw->dpy, pm, 0, NULL);
        XPutImage(draw->dpy, pm, gc, &img, 0, 0, 0, 0, src_w, src_h);
        XFreeGC(draw->dpy, gc);

        pic = XRenderCreatePicture(
            draw->dpy,
            pm,
            XRenderFindStandardFormat(draw->dpy, PictStandardARGB32),
            0,
            NULL
        );
        XFreePixmap(draw->dpy, pm);

        XRenderSetPictureFilter(draw->dpy, pic, FilterBilinear, NULL, 0);
        xf.matrix[0][0] = (int32)((src_w << xrender_fixed_shift) / dst_w);
        xf.matrix[0][1] = 0;
        xf.matrix[0][2] = 0;
        xf.matrix[1][0] = 0;
        xf.matrix[1][1] = (int32)((src_h << xrender_fixed_shift) / dst_h);
        xf.matrix[1][2] = 0;
        xf.matrix[2][0] = 0;
        xf.matrix[2][1] = 0;
        xf.matrix[2][2] = xrender_fixed_one;
        XRenderSetPictureTransform(draw->dpy, pic, &xf);

        return pic;
    }

    {
        Pixmap pm;
        Picture pic;
        GC gc;
        Imlib_Image origin;
        Imlib_Image scaled;

        if ((origin = imlib_create_image_using_data((int32)src_w,
                                                    (int32)src_h,
                                                    (DATA32 *)src)) == NULL) {
            return None;
        }

        imlib_context_set_image(origin);
        imlib_image_set_has_alpha(1);
        scaled = imlib_create_cropped_scaled_image(0, 0,
                                                   (int32)src_w, (int32)src_h,
                                                   (int32)dst_w, (int32)dst_h);
        imlib_free_image_and_decache();
        if (scaled == NULL) {
            return None;
        }
        imlib_context_set_image(scaled);
        imlib_image_set_has_alpha(1);

        {
            XImage img = {
                .width = (int32)dst_w,
                .height = (int32)dst_h,
                .format = ZPixmap,
                .data = (char *)imlib_image_get_data_for_reading_only(),
                .byte_order = ImageByteOrder(draw->dpy),
                .bitmap_unit = BitmapUnit(draw->dpy),
                .bitmap_bit_order = BitmapBitOrder(draw->dpy),
                .bitmap_pad = argb_bits_per_pixel,
                .depth = argb_depth,
                .bits_per_pixel = argb_bits_per_pixel,
                .obdata = 0,
            };
            XInitImage(&img);

            pm = XCreatePixmap(draw->dpy, draw->root, dst_w, dst_h,
                               argb_depth);
            gc = XCreateGC(draw->dpy, pm, 0, NULL);
            XPutImage(draw->dpy, pm, gc, &img, 0, 0, 0, 0, dst_w, dst_h);
            imlib_free_image_and_decache();
            XFreeGC(draw->dpy, gc);

            pic = XRenderCreatePicture(
                draw->dpy,
                pm,
                XRenderFindStandardFormat(draw->dpy, PictStandardARGB32),
                0,
                NULL
            );
            XFreePixmap(draw->dpy, pm);
        }

        return pic;
    }
}

void
draw_rect(Draw *draw,
          int32 x, int32 y,
          uint32 w, uint32 h,
          int32 filled,
          int32 invert
) {
    ulong pixel;

    if ((draw == NULL) || (draw->scheme == NULL)) {
        return;
    }

    if (invert) {
        pixel = draw->scheme[ColBg].pixel;
    } else {
        pixel = draw->scheme[ColFg].pixel;
    }
    XSetForeground(draw->dpy, draw->gc, pixel);

    if (filled) {
        XFillRectangle(draw->dpy, draw->drawable, draw->gc, x, y, w, h);
    } else {
        XDrawRectangle(draw->dpy, draw->drawable, draw->gc, x, y, w - 1,
                       h - 1);
    }
    return;
}

static void
hb_position_scale(hb_glyph_position_t *pos, double scale) {
    pos->x_advance = (hb_position_t)(pos->x_advance*scale);
    pos->y_advance = (hb_position_t)(pos->y_advance*scale);
    pos->x_offset = (hb_position_t)(pos->x_offset*scale);
    pos->y_offset = (hb_position_t)(pos->y_offset*scale);
    return;
}

int32
draw_text(Draw *draw,
          int32 x, int32 y,
          uint32 w, uint32 h,
          uint32 lpad,
          char *text,
          int32 invert) {
    enum {
        utf8_max_bytes = 4,
        nomatches_len = 64,
        hb_position_shift = 6,
        glyph_width_tolerance = 10,
        zero_width_joiner = 0x200D,
        zero_width_non_joiner = 0x200C,
        variation_selector_min = 0xFE00,
        variation_selector_max = 0xFE0F,
        emoji_modifier_min = 0x1F3FB,
        emoji_modifier_max = 0x1F3FF,
        tag_char_min = 0xE0020,
        tag_char_max = 0xE007F,
    };
    bool render = ((x != 0) || (y != 0) || (w > 0) || (h > 0));
    XftDraw *d = NULL;
    hb_buffer_t *buffer = NULL;
    int32 ellipsis_x = 0;
    uint32 ellipsis_w = 0;
    static struct {
        uint32 codepoint[nomatches_len];
        uint32 idx;
        int32 padding;
    } nomatches;
    static uint32 ellipsis_width = 0;

    if (draw == NULL) {
        return 0;
    }
    if (render) {
        if ((draw->scheme == NULL) || (w == 0)) {
            return 0;
        }
    }
    if ((text == NULL) || (draw->fonts == NULL)) {
        return 0;
    }

    if (render) {
        if (invert) {
            XSetForeground(draw->dpy, draw->gc, draw->scheme[ColFg].pixel);
        } else {
            XSetForeground(draw->dpy, draw->gc, draw->scheme[ColBg].pixel);
        }

        XFillRectangle(draw->dpy, draw->drawable, draw->gc, x, y, w, h);
        if ((d = XftDrawCreate(draw->dpy, draw->drawable, draw->visual,
                                draw->cmap)) == NULL) {
            return 0;
        }

        x += lpad;
        w -= lpad;
    } else {
        if (invert) {
            w = (uint32)invert;
        } else {
            w = (uint32)~invert;
        }
    }

    if (ellipsis_width == 0) {
        if (render) {
            ellipsis_width = draw_fontset_getwidth(draw, "...");
        }
    }

    if ((buffer = hb_buffer_create()) == NULL) {
        if (d) {
            XftDrawDestroy(d);
        }
        return 0;
    }

    while (*text) {
        uint32 utf8codepoint;
        int32 utf8charlen;
        DwmFont *curfont;
        DwmFont *usedfont;
        int32 chunk_len = 0;
        uint32 glyph_count = 0;
        hb_glyph_info_t *info;
        hb_glyph_position_t *pos;
        uint32 i = 0;
        bool overflow = false;
        uint32 ew = 0;
        double last_scale = 1.0;

        utf8charlen = utf8_decode_raw((char *)text, &utf8codepoint,
                                      utf8_max_bytes);

        usedfont = NULL;
        for (curfont = draw->fonts; curfont; curfont = curfont->next) {
            if (XftCharExists(draw->dpy, curfont->xfont,
                              (uint32)utf8codepoint)) {
                usedfont = curfont;
                break;
            }
        }

        if (usedfont == NULL) {
            bool is_missing = false;

            for (int32 k = 0; k < nomatches_len; k += 1) {
                if (utf8codepoint == nomatches.codepoint[k]) {
                    is_missing = true;
                    break;
                }
            }

            if (!is_missing) {
                FcCharSet *fccharset = FcCharSetCreate();
                FcPattern *fcpattern = NULL;
                XftResult result;
                FcPattern *match = NULL;

                if (fccharset == NULL) {
                    error("Error: cannot create font character set.\n");
                    fatal(EXIT_FAILURE);
                }

                if (!FcCharSetAddChar(fccharset, (uint32)utf8codepoint)) {
                    FcCharSetDestroy(fccharset);
                    error("Error: cannot add character to font set.\n");
                    fatal(EXIT_FAILURE);
                }

                if (draw->fonts->pattern == NULL) {
                    FcCharSetDestroy(fccharset);
                    error("Error: the first font in the cache must be loaded "
                          "from a font string.\n");
                    fatal(EXIT_FAILURE);
                }

                fcpattern = FcPatternDuplicate(draw->fonts->pattern);
                if (fcpattern == NULL) {
                    FcCharSetDestroy(fccharset);
                    error("Error: cannot duplicate font pattern.\n");
                    fatal(EXIT_FAILURE);
                }

                if (!FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset)) {
                    FcCharSetDestroy(fccharset);
                    FcPatternDestroy(fcpattern);
                    error("Error: cannot add character set to font pattern.\n");
                    fatal(EXIT_FAILURE);
                }
                if (!FcPatternAddBool(fcpattern, FC_SCALABLE, FcTrue)) {
                    FcCharSetDestroy(fccharset);
                    FcPatternDestroy(fcpattern);
                    error("Error: cannot mark font pattern as scalable.\n");
                    fatal(EXIT_FAILURE);
                }

                if (!FcConfigSubstitute(NULL, fcpattern, FcMatchPattern)) {
                    FcCharSetDestroy(fccharset);
                    FcPatternDestroy(fcpattern);
                    error("Error: cannot substitute font pattern.\n");
                    fatal(EXIT_FAILURE);
                }
                FcDefaultSubstitute(fcpattern);
                match = XftFontMatch(draw->dpy, draw->screen, fcpattern,
                                      &result);

                FcCharSetDestroy(fccharset);
                FcPatternDestroy(fcpattern);

                if (match) {
                    usedfont = xfont_create(draw, NULL, match);
                    if (usedfont) {
                        if (XftCharExists(draw->dpy, usedfont->xfont,
                                          (uint32)utf8codepoint)) {
                            for (curfont = draw->fonts; curfont->next;
                                 curfont = curfont->next) {
                                /* NOP */
                            }
                            curfont->next = usedfont;
                        } else {
                            xfont_free(usedfont);
                            usedfont = NULL;
                        }
                    }
                }
            }

            if (usedfont == NULL) {
                nomatches.idx += 1;
                nomatches.codepoint[nomatches.idx % nomatches_len]
                    = utf8codepoint;
                usedfont = draw->fonts;
            }
        }

        if ((usedfont == NULL) || (usedfont->hbfont == NULL)) {
            break;
        }

        {
            char *scan = (char *)text;

            while (*scan) {
                uint32 cp;
                int32 clen;
                DwmFont *f;
                bool is_combo = false;

                clen = utf8_decode_raw(scan, &cp, utf8_max_bytes);

                /* Preserve complex emoji joiners and variation selectors in
                 * the current chunk. */
                if (cp == zero_width_joiner) {
                    is_combo = true;
                } else if (cp == zero_width_non_joiner) {
                    is_combo = true;
                } else if ((cp >= variation_selector_min)
                           && (cp <= variation_selector_max)) {
                    is_combo = true;
                } else if ((cp >= emoji_modifier_min)
                           && (cp <= emoji_modifier_max)) {
                    is_combo = true;
                } else if ((cp >= tag_char_min) && (cp <= tag_char_max)) {
                    is_combo = true;
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
                    if (f) {
                        break;
                    }
                    if (chunk_len > 0) {
                        break;
                    }
                }

                scan += clen;
                chunk_len += clen;
            }
        }

        if (chunk_len == 0) {
            chunk_len = utf8charlen;
        }

        hb_buffer_clear_contents(buffer);
        hb_buffer_add_utf8(buffer, text, chunk_len, 0, -1);
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(usedfont->hbfont, buffer, NULL, 0);

        info = hb_buffer_get_glyph_infos(buffer, &glyph_count);
        pos = hb_buffer_get_glyph_positions(buffer, &glyph_count);

        for (i = 0; i < glyph_count; i += 1) {
            FT_UInt glyph_index = info[i].codepoint;
            XGlyphInfo ext;
            int32 hb_adv = pos[i].x_advance >> hb_position_shift;
            int32 xft_adv;
            uint32 tmpw;
            bool scaled = false;

            XftGlyphExtents(draw->dpy, usedfont->xfont, &glyph_index, 1,
                            &ext);
            xft_adv = ext.xOff;

            if ((hb_adv > 0)
                && (xft_adv > 0)
                && (abs(hb_adv - xft_adv) > glyph_width_tolerance)) {
                double scale = (double)xft_adv / (double)hb_adv;

                hb_position_scale(&pos[i], scale);
                tmpw = (uint32)xft_adv;
                last_scale = scale;
                scaled = true;
            }

            if (!scaled
                && (hb_adv == 0)
                && (ext.xOff == 0)
                && (last_scale != 1.0)) {
                hb_position_scale(&pos[i], last_scale);
                tmpw = 0;
                scaled = true;
            }

            if (!scaled) {
                tmpw = (uint32)hb_adv;
                last_scale = 1.0;
            }

            if (ew + ellipsis_width <= w) {
                ellipsis_x = x + (int32)ew;
                ellipsis_w = w - ew;
            }
            if (ew + tmpw > w) {
                overflow = true;
                break;
            }
            ew += tmpw;
        }

        if ((i > 0) && render) {
            XftGlyphFontSpec *specs;
            int32 cx = x;
            int32 ty;
            XftColor *fg_color;

            specs = malloc2_zero(i*SIZEOF(*specs));
            ty = y + (int32)((h - usedfont->h) / 2)
                 + usedfont->xfont->ascent;

            for (uint32 j = 0; j < i; j += 1) {
                specs[j].font = usedfont->xfont;
                specs[j].glyph = info[j].codepoint;
                specs[j].x = (int16)(
                    cx + (pos[j].x_offset >> hb_position_shift)
                );
                specs[j].y = (int16)(
                    ty - (pos[j].y_offset >> hb_position_shift)
                );
                cx += pos[j].x_advance >> hb_position_shift;
            }

            if (invert) {
                fg_color = &draw->scheme[ColBg];
            } else {
                fg_color = &draw->scheme[ColFg];
            }

            XftDrawGlyphFontSpec(d, fg_color, specs, (int32)i);
            free2(specs, i*SIZEOF(*specs));
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
        return (int32)((uint32)x + w);
    }
    return x;
}

void
draw_pic(Draw *draw,
         int32 x, int32 y,
         uint32 w, uint32 h,
         Picture pic
) {
    if (draw == NULL) {
        return;
    }

    XRenderComposite(draw->dpy, PictOpOver, pic, None, draw->picture, 0, 0,
                     0, 0, x, y, w, h);
    return;
}

void
draw_map(Draw *draw, Window win, int32 x, int32 y, uint32 w, uint32 h) {
    if (draw == NULL) {
        return;
    }

    XCopyArea(draw->dpy, draw->drawable, win, draw->gc, x, y, w, h, x, y);
    XSync(draw->dpy, False);
    return;
}

uint32
draw_fontset_getwidth(Draw *draw, char *text) {
    if ((draw == NULL) || (draw->fonts == NULL) || (text == NULL)) {
        return 0;
    }
    return (uint32)draw_text(draw, 0, 0, 0, 0, 0, text, 0);
}

uint32
draw_fontset_getwidth_clamp(Draw *draw, char *text, uint32 n) {
    uint32 tmp = 0;

    if (draw && draw->fonts && text && (n > 0)) {
        tmp = (uint32)draw_text(draw, 0, 0, 0, 0, 0, text, (int32)n);
    }
    return (uint32)MIN(n, tmp);
}

void
draw_font_getexts(DwmFont *font,
                  char *text,
                  uint32 len,
                  uint32 *w,
                  uint32 *h
) {
    XGlyphInfo ext;

    if ((font == NULL) || (text == NULL)) {
        return;
    }

    XftTextExtentsUtf8(font->dpy, font->xfont, (XftChar8 *)text,
                       (int32)len, &ext);
    if (w) {
        *w = (uint32)ext.xOff;
    }
    if (h) {
        *h = font->h;
    }
    return;
}

Cursor
draw_cur_create(Draw *draw, int32 shape) {
    Cursor cur;

    cur = XCreateFontCursor(draw->dpy, (uint32)shape);

    return cur;
}

#undef draw

#endif /* DRAW_C */
