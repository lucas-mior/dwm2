/* See LICENSE file for copyright and license details. */

#if !defined(DRAW_C)
#define DRAW_C

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_draw 1
#elif !defined(TESTING_draw)
#define TESTING_draw 0
#endif

#include "cbase.h"
#include "dwm.h"

static void
draw_create_pixmap(Draw *ctx) {
    uint32 w;
    uint32 h;
    uint32 x_depth;

    ASSERT(ctx != NULL);
    ASSERT_POSITIVE(ctx->w);
    ASSERT_POSITIVE(ctx->h);
    ASSERT_POSITIVE(ctx->depth);

    w = (uint32)ctx->w;
    h = (uint32)ctx->h;
    x_depth = (uint32)ctx->depth;

    ctx->drawable = XCreatePixmap(ctx->dpy, ctx->root, w, h, x_depth);
    ctx->picture = XRenderCreatePicture(
        ctx->dpy,
        ctx->drawable,
        XRenderFindVisualFormat(ctx->dpy, ctx->visual),
        0,
        NULL
    );
    return;
}

static Draw *
draw_create(Display *dpy,
            int32 screen_number,
            Window root_window,
            int32 w, int32 h,
            Visual *draw_visual,
            int32 draw_depth,
            Colormap cmap
) {
    Draw *ctx = malloc2_zero(SIZEOF(*ctx));

    if ((w <= 0) || (h <= 0) || (draw_depth <= 0)) {
        error("Error: invalid draw geometry.\n");
        fatal(EXIT_FAILURE);
    }

    ctx->dpy = dpy;
    ctx->screen = screen_number;
    ctx->root = root_window;
    ctx->visual = draw_visual;
    ctx->depth = draw_depth;
    ctx->cmap = cmap;

    ctx->w = w;
    ctx->h = h;

    draw_create_pixmap(ctx);
    ctx->gc = XCreateGC(dpy, ctx->drawable, 0, NULL);
    XSetLineAttributes(dpy, ctx->gc, 1, LineSolid, CapButt, JoinMiter);

    return ctx;
}

static void
draw_resize(Draw *ctx, int32 w, int32 h) {
    if (ctx == NULL) {
        return;
    }
    if ((w <= 0) || (h <= 0)) {
        return;
    }

    ctx->w = w;
    ctx->h = h;
    if (ctx->picture) {
        XRenderFreePicture(ctx->dpy, ctx->picture);
    }
    if (ctx->drawable) {
        XFreePixmap(ctx->dpy, ctx->drawable);
    }
    draw_create_pixmap(ctx);
    return;
}

static void
draw_free(Draw *ctx) {
    if (ctx == NULL) {
        return;
    }

    XRenderFreePicture(ctx->dpy, ctx->picture);
    XFreePixmap(ctx->dpy, ctx->drawable);
    XFreeGC(ctx->dpy, ctx->gc);

    draw_fontset_free(ctx->fonts);

    free2(ctx, SIZEOF(*ctx));
    return;
}

/* This function is an implementation detail. Library users should use
 * draw_fontset_create instead.
 */
static DwmFont *
xfont_create(Draw *ctx, char *fontname, FcPattern *fontpattern) {
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
        if ((xfont = XftFontOpenName(ctx->dpy, ctx->screen, fontname))
            == NULL) {
            error("Error: cannot load font from name: '%s'.\n", fontname);
            return NULL;
        }
        if ((pattern = FcNameParse((FcChar8 *)fontname)) == NULL) {
            error("Error: cannot parse font name to pattern: '%s'.\n",
                  fontname);
            XftFontClose(ctx->dpy, xfont);
            return NULL;
        }
    } else if (fontpattern) {
        if ((xfont = XftFontOpenPattern(ctx->dpy, fontpattern)) == NULL) {
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
    font->h = xfont->ascent + xfont->descent;
    font->dpy = ctx->dpy;

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

static DwmFont *
draw_fontset_create(Draw *ctx, char *fonts[], int64 fontcount) {
    DwmFont *cur;
    DwmFont *ret = NULL;

    if ((ctx == NULL) || (fonts == NULL)) {
        return NULL;
    }

    for (int64 i = 1; i <= fontcount; i += 1) {
        if ((cur = xfont_create(ctx, fonts[fontcount - i], NULL))) {
            cur->next = ret;
            ret = cur;
        }
    }
    return (ctx->fonts = ret);
}

static void
draw_fontset_free(DwmFont *font) {
    if (font) {
        draw_fontset_free(font->next);
        xfont_free(font);
    }
    return;
}

static void
draw_clr_create(Draw *ctx, XftColor *dest, char *clrname, uint32 alpha) {
    if ((ctx == NULL) || (dest == NULL) || (clrname == NULL)) {
        return;
    }

    if (!XftColorAllocName(ctx->dpy, ctx->visual, ctx->cmap,
                           clrname, dest)) {
        error("Error: cannot allocate color '%s'.\n", clrname);
        fatal(EXIT_FAILURE);
    }

    dest->pixel = (dest->pixel & 0x00ffffffU) | (alpha << 24);
    return;
}

static XftColor *
draw_scm_create(Draw *ctx, char *clrnames[], uint32 alphas[], int64 clrcount) {
    XftColor *ret;

    /* need at least two colors for a scheme */
    if ((ctx == NULL) || (clrnames == NULL) || (clrcount < 2)) {
        return NULL;
    }

    ret = malloc2_zero(clrcount*SIZEOF(*ret));

    for (int64 i = 0; i < clrcount; i += 1) {
        draw_clr_create(ctx, &ret[i], clrnames[i], alphas[i]);
    }
    return ret;
}

static void
draw_setfontset(Draw *ctx, DwmFont *set) {
    if (ctx) {
        ctx->fonts = set;
    }
    return;
}

static void
draw_setscheme(Draw *ctx, XftColor *scm) {
    if (ctx) {
        ctx->scheme = scm;
    }
    return;
}

static Picture
draw_picture_create_resized(Draw *ctx,
                            uint32 *src,
                            int32 src_w, int32 src_h,
                            int32 dst_w, int32 dst_h
) {
    enum {
        argb_depth = 32,
        argb_bits_per_pixel = 32,
        xrender_fixed_shift = 16,
        xrender_fixed_one = 1 << xrender_fixed_shift,
        xrender_scale_limit = 2,
    };
    uint32 src_xw;
    uint32 src_xh;
    uint32 dst_xw;
    uint32 dst_xh;
    uint32 x_depth;

    if ((ctx == NULL) || (src == NULL)) {
        return None;
    }
    if ((src_w <= 0) || (src_h <= 0) || (dst_w <= 0) || (dst_h <= 0)) {
        return None;
    }

    src_xw = (uint32)src_w;
    src_xh = (uint32)src_h;
    dst_xw = (uint32)dst_w;
    dst_xh = (uint32)dst_h;
    x_depth = (uint32)argb_depth;

    if ((src_w <= dst_w*xrender_scale_limit)
        && (src_h <= dst_h*xrender_scale_limit)) {
        Pixmap pm;
        Picture pic;
        GC gc;
        XTransform xf;
        XImage img = {
            .width = src_w,
            .height = src_h,
            .format = ZPixmap,
            .data = (char *)src,
            .byte_order = ImageByteOrder(ctx->dpy),
            .bitmap_unit = BitmapUnit(ctx->dpy),
            .bitmap_bit_order = BitmapBitOrder(ctx->dpy),
            .bitmap_pad = argb_bits_per_pixel,
            .depth = argb_depth,
            .bits_per_pixel = argb_bits_per_pixel,
            .obdata = 0,
        };
        XInitImage(&img);

        pm = XCreatePixmap(ctx->dpy, ctx->root, src_xw, src_xh, x_depth);
        gc = XCreateGC(ctx->dpy, pm, 0, NULL);
        XPutImage(ctx->dpy, pm, gc, &img, 0, 0, 0, 0, src_xw, src_xh);
        XFreeGC(ctx->dpy, gc);

        pic = XRenderCreatePicture(
            ctx->dpy,
            pm,
            XRenderFindStandardFormat(ctx->dpy, PictStandardARGB32),
            0,
            NULL
        );
        XFreePixmap(ctx->dpy, pm);

        XRenderSetPictureFilter(ctx->dpy, pic, FilterBilinear, NULL, 0);
        xf.matrix[0][0] = (src_w << xrender_fixed_shift) / dst_w;
        xf.matrix[0][1] = 0;
        xf.matrix[0][2] = 0;
        xf.matrix[1][0] = 0;
        xf.matrix[1][1] = (src_h << xrender_fixed_shift) / dst_h;
        xf.matrix[1][2] = 0;
        xf.matrix[2][0] = 0;
        xf.matrix[2][1] = 0;
        xf.matrix[2][2] = xrender_fixed_one;
        XRenderSetPictureTransform(ctx->dpy, pic, &xf);

        return pic;
    }

    {
        Pixmap pm;
        Picture pic;
        GC gc;
        Imlib_Image origin;
        Imlib_Image scaled;

        if ((origin = imlib_create_image_using_copied_data(src_w, src_h,
                                                           src)) == NULL) {
            return None;
        }

        imlib_context_set_image(origin);
        imlib_image_set_has_alpha(1);
        scaled = imlib_create_cropped_scaled_image(0, 0,
                                                   src_w, src_h,
                                                   dst_w, dst_h);
        imlib_free_image_and_decache();
        if (scaled == NULL) {
            return None;
        }
        imlib_context_set_image(scaled);
        imlib_image_set_has_alpha(1);

        {
            XImage img = {
                .width = dst_w,
                .height = dst_h,
                .format = ZPixmap,
                .data = (char *)imlib_image_get_data_for_reading_only(),
                .byte_order = ImageByteOrder(ctx->dpy),
                .bitmap_unit = BitmapUnit(ctx->dpy),
                .bitmap_bit_order = BitmapBitOrder(ctx->dpy),
                .bitmap_pad = argb_bits_per_pixel,
                .depth = argb_depth,
                .bits_per_pixel = argb_bits_per_pixel,
                .obdata = 0,
            };
            XInitImage(&img);

            pm = XCreatePixmap(ctx->dpy, ctx->root, dst_xw, dst_xh,
                               x_depth);
            gc = XCreateGC(ctx->dpy, pm, 0, NULL);
            XPutImage(ctx->dpy, pm, gc, &img, 0, 0, 0, 0, dst_xw, dst_xh);
            imlib_free_image_and_decache();
            XFreeGC(ctx->dpy, gc);

            pic = XRenderCreatePicture(
                ctx->dpy,
                pm,
                XRenderFindStandardFormat(ctx->dpy, PictStandardARGB32),
                0,
                NULL
            );
            XFreePixmap(ctx->dpy, pm);
        }

        return pic;
    }
}

static void
draw_rect(Draw *ctx,
          int32 x, int32 y,
          int32 w, int32 h,
          int32 filled,
          int32 invert
) {
    ulong pixel;
    uint32 xw;
    uint32 xh;

    if ((ctx == NULL) || (ctx->scheme == NULL)) {
        return;
    }
    if ((w <= 0) || (h <= 0)) {
        return;
    }
    xw = (uint32)w;
    xh = (uint32)h;

    if (invert) {
        pixel = ctx->scheme[ColBg].pixel;
    } else {
        pixel = ctx->scheme[ColFg].pixel;
    }
    XSetForeground(ctx->dpy, ctx->gc, pixel);

    if (filled) {
        XFillRectangle(ctx->dpy, ctx->drawable, ctx->gc, x, y, xw, xh);
    } else {
        XDrawRectangle(ctx->dpy, ctx->drawable, ctx->gc, x, y, xw - 1, xh - 1);
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

static int32
draw_text(Draw *ctx,
          int32 x, int32 y,
          int32 w, int32 h,
          int32 lpad,
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
    int32 ellipsis_w = 0;
    static struct {
        uint32 codepoint[nomatches_len];
        int32 idx;
        int32 padding;
    } nomatches;
    static int32 ellipsis_width = 0;

    if (ctx == NULL) {
        return 0;
    }
    if (render) {
        if ((ctx->scheme == NULL) || (w <= 0) || (h <= 0) || (lpad < 0)) {
            return 0;
        }
        if (lpad > w) {
            return 0;
        }
    }
    if ((text == NULL) || (ctx->fonts == NULL)) {
        return 0;
    }

    if (render) {
        uint32 xw = (uint32)w;
        uint32 xh = (uint32)h;

        if (invert) {
            XSetForeground(ctx->dpy, ctx->gc, ctx->scheme[ColFg].pixel);
        } else {
            XSetForeground(ctx->dpy, ctx->gc, ctx->scheme[ColBg].pixel);
        }

        XFillRectangle(ctx->dpy, ctx->drawable, ctx->gc, x, y, xw, xh);
        if ((d = XftDrawCreate(ctx->dpy, ctx->drawable, ctx->visual,
                                ctx->cmap)) == NULL) {
            return 0;
        }

        x += lpad;
        w -= lpad;
    } else {
        if (invert) {
            w = invert;
        } else {
            w = INT32_MAX;
        }
    }

    if (ellipsis_width == 0) {
        if (render) {
            ellipsis_width = draw_fontset_getwidth(ctx, "...");
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
        int32 ew = 0;
        double last_scale = 1.0;

        utf8charlen = utf8_decode_raw((char *)text, &utf8codepoint,
                                      utf8_max_bytes);

        usedfont = NULL;
        for (curfont = ctx->fonts; curfont; curfont = curfont->next) {
            if (XftCharExists(ctx->dpy, curfont->xfont, utf8codepoint)) {
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

                if (!FcCharSetAddChar(fccharset, utf8codepoint)) {
                    FcCharSetDestroy(fccharset);
                    error("Error: cannot add character to font set.\n");
                    fatal(EXIT_FAILURE);
                }

                if (ctx->fonts->pattern == NULL) {
                    FcCharSetDestroy(fccharset);
                    error("Error: the first font in the cache must be loaded "
                          "from a font string.\n");
                    fatal(EXIT_FAILURE);
                }

                fcpattern = FcPatternDuplicate(ctx->fonts->pattern);
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
                match = XftFontMatch(ctx->dpy, ctx->screen, fcpattern,
                                      &result);

                FcCharSetDestroy(fccharset);
                FcPatternDestroy(fcpattern);

                if (match) {
                    usedfont = xfont_create(ctx, NULL, match);
                    if (usedfont) {
                        if (XftCharExists(ctx->dpy, usedfont->xfont,
                                          utf8codepoint)) {
                            for (curfont = ctx->fonts; curfont->next;
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
                usedfont = ctx->fonts;
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
                    for (f = ctx->fonts; f; f = f->next) {
                        if (XftCharExists(ctx->dpy, f->xfont, cp)) {
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
            int32 tmpw = 0;
            bool scaled = false;

            XftGlyphExtents(ctx->dpy, usedfont->xfont, &glyph_index, 1,
                            &ext);
            xft_adv = ext.xOff;

            if ((hb_adv > 0)
                && (xft_adv > 0)
                && (abs(hb_adv - xft_adv) > glyph_width_tolerance)) {
                double scale = (double)xft_adv / (double)hb_adv;

                hb_position_scale(&pos[i], scale);
                tmpw = xft_adv;
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
                tmpw = hb_adv;
                last_scale = 1.0;
            }

            if (ew + ellipsis_width <= w) {
                ellipsis_x = x + ew;
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
            ty = y + (h - usedfont->h) / 2 + usedfont->xfont->ascent;

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
                fg_color = &ctx->scheme[ColBg];
            } else {
                fg_color = &ctx->scheme[ColFg];
            }

            XftDrawGlyphFontSpec(d, fg_color, specs, (int32)i);
            free2(specs, i*SIZEOF(*specs));
        }

        x += ew;
        w -= ew;

        if (overflow) {
            if (render) {
                draw_text(ctx, ellipsis_x, y, ellipsis_w, h, 0, "...", invert);
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
        return x + w;
    }
    return x;
}

static void
draw_pic(Draw *ctx,
         int32 x, int32 y,
         int32 w, int32 h,
         Picture pic
) {
    uint32 xw;
    uint32 xh;

    if (ctx == NULL) {
        return;
    }
    if ((w <= 0) || (h <= 0)) {
        return;
    }

    xw = (uint32)w;
    xh = (uint32)h;

    XRenderComposite(ctx->dpy, PictOpOver, pic, None, ctx->picture, 0, 0,
                     0, 0, x, y, xw, xh);
    return;
}

static void
draw_map(Draw *ctx, Window win, int32 x, int32 y, int32 w, int32 h) {
    uint32 xw;
    uint32 xh;

    if (ctx == NULL) {
        return;
    }
    if ((w <= 0) || (h <= 0)) {
        return;
    }

    xw = (uint32)w;
    xh = (uint32)h;

    XCopyArea(ctx->dpy, ctx->drawable, win, ctx->gc, x, y, xw, xh, x, y);
    XSync(ctx->dpy, False);
    return;
}

static int32
draw_fontset_getwidth(Draw *ctx, char *text) {
    if ((ctx == NULL) || (ctx->fonts == NULL) || (text == NULL)) {
        return 0;
    }
    return draw_text(ctx, 0, 0, 0, 0, 0, text, 0);
}

static int32
draw_fontset_getwidth_clamp(Draw *ctx, char *text, int32 n) {
    int32 tmp = 0;

    if (ctx && ctx->fonts && text && (n > 0)) {
        tmp = draw_text(ctx, 0, 0, 0, 0, 0, text, n);
    }
    return MIN(n, tmp);
}

static void
draw_font_getexts(DwmFont *font,
                  char *text,
                  int32 len,
                  int32 *w,
                  int32 *h
) {
    XGlyphInfo ext;

    if ((font == NULL) || (text == NULL)) {
        return;
    }
    if (len <= 0) {
        return;
    }

    XftTextExtentsUtf8(font->dpy, font->xfont, (XftChar8 *)text,
                       len, &ext);
    if (w) {
        *w = ext.xOff;
    }
    if (h) {
        *h = font->h;
    }
    return;
}

static Cursor
draw_cur_create(Draw *ctx, int32 shape) {
    Cursor cur;

    if ((ctx == NULL) || (shape < 0)) {
        return None;
    }

    cur = XCreateFontCursor(ctx->dpy, (uint32)shape);

    return cur;
}

#if TESTING_draw

#define CBASE_IMPLEMENT
#include "cbase.h"

int
main(void) {
    hb_glyph_position_t pos = {
        .x_advance = 64,
        .y_advance = -32,
        .x_offset = 16,
        .y_offset = -8,
    };

    hb_position_scale(&pos, 0.5);

    ASSERT_EQUAL(pos.x_advance, 32);
    ASSERT_EQUAL(pos.y_advance, -16);
    ASSERT_EQUAL(pos.x_offset, 8);
    ASSERT_EQUAL(pos.y_offset, -4);

    exit(EXIT_SUCCESS);
}

#endif /* TESTING_draw */

#endif /* DRAW_C */
