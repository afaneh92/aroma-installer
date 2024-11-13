/********************************************************************[libaroma]*
 * Copyright (C) 2011-2015 Ahmad Amarullah (http://amarullz.com/)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *______________________________________________________________________________
 *
 * File: aroma_engine.c
 * Description: aroma engine.
 *
 * This is part of libaroma, an embedded ui toolkit.
 *
 * Author(s):
 *   - Ahmad Amarullah (@amarullz) - 06/04/2015
 *   - Michael Jauregui (@MLXProjects) - 17/01/2023
 *   - Mohammad Afaneh (@afaneh92) - 09/12/2024
 *
 */

#include <aroma.h>

static uint8_t libaroma_dither_tresshold_r[64] = {
    1, 7, 3, 5, 0, 8, 2, 6,
    7, 1, 5, 3, 8, 0, 6, 2,
    3, 5, 0, 8, 2, 6, 1, 7,
    5, 3, 8, 0, 6, 2, 7, 1,
    0, 8, 2, 6, 1, 7, 3, 5,
    8, 0, 6, 2, 7, 1, 5, 3,
    2, 6, 1, 7, 3, 5, 0, 8,
    6, 2, 7, 1, 5, 3, 8, 0
};

static uint8_t libaroma_dither_tresshold_g[64] = {
    1, 3, 2, 2, 3, 1, 2, 2,
    2, 2, 0, 4, 2, 2, 4, 0,
    3, 1, 2, 2, 1, 3, 2, 2,
    2, 2, 4, 0, 2, 2, 0, 4,
    1, 3, 2, 2, 3, 1, 2, 2,
    2, 2, 0, 4, 2, 2, 4, 0,
    3, 1, 2, 2, 1, 3, 2, 2,
    2, 2, 4, 0, 2, 2, 0, 4
};

static uint8_t libaroma_dither_tresshold_b[64] = {
    5, 3, 8, 0, 6, 2, 7, 1,
    3, 5, 0, 8, 2, 6, 1, 7,
    8, 0, 6, 2, 7, 1, 5, 3,
    0, 8, 2, 6, 1, 7, 3, 5,
    6, 2, 7, 1, 5, 3, 8, 0,
    2, 6, 1, 7, 3, 5, 0, 8,
    7, 1, 5, 3, 8, 0, 6, 2,
    1, 7, 3, 5, 0, 8, 2, 6
};

uint8_t * libaroma_dither_table_r() {
    return libaroma_dither_tresshold_r;
}

uint8_t * libaroma_dither_table_g() {
    return libaroma_dither_tresshold_g;
}

uint8_t * libaroma_dither_table_b() {
    return libaroma_dither_tresshold_b;
}

uint8_t libaroma_dither_table_pos(int x, int y) {
    return ((y & 7) << 3) + (x & 7);
}

uint8_t libaroma_dither_r(uint8_t p) {
    return libaroma_dither_tresshold_r[p];
}

uint8_t libaroma_dither_g(uint8_t p) {
    return libaroma_dither_tresshold_g[p];
}

uint8_t libaroma_dither_b(uint8_t p) {
    return libaroma_dither_tresshold_b[p];
}

uint16_t libaroma_dither_rgb(int x, int y, uint8_t sr, uint8_t sg, uint8_t sb) {
    uint8_t dither_xy = ((y & 7) << 3) + (x & 7);
    uint8_t r = libaroma_color_close_r(min(sr + libaroma_dither_tresshold_r[dither_xy], 0xff));
    uint8_t g = libaroma_color_close_g(min(sg + libaroma_dither_tresshold_g[dither_xy], 0xff));
    uint8_t b = libaroma_color_close_b(min(sb + libaroma_dither_tresshold_b[dither_xy], 0xff));
    return libaroma_rgb(r, g, b);
}

uint16_t libaroma_dither_mono_rgb(int x, int y, uint8_t sr, uint8_t sg, uint8_t sb) {
    uint8_t dither_xy = libaroma_dither_tresshold_g[((y & 7) << 3) + (x & 7)];
    uint8_t dither_xyrb = dither_xy * 2;
    uint8_t r = libaroma_color_close_r(min(sr + dither_xyrb, 0xff));
    uint8_t g = libaroma_color_close_g(min(sg + dither_xy, 0xff));
    uint8_t b = libaroma_color_close_b(min(sb + dither_xyrb, 0xff));
    return libaroma_rgb(r, g, b);
}

uint16_t libaroma_dither_mono(int x, int y, uint32_t col) {
    return libaroma_dither_mono_rgb(x, y, libaroma_color_r32(col), libaroma_color_g32(col), libaroma_color_b32(col));
}

uint16_t libaroma_dither(int x, int y, uint32_t col) {
    return libaroma_dither_rgb(x, y, libaroma_color_r32(col), libaroma_color_g32(col), libaroma_color_b32(col));
}

uint16_t libaroma_rgb_from_string(const char * c) {
    if (c[0] != '#') {
        return 0;
    }
    char out[9] = {'0', 'x'};
    int i;
    if (strlen(c) == 7) {
        for (i = 1; i < 7; i++) {
            out[i + 1] = c[i];
        }
    } else if (strlen(c) == 4) {
        for (i = 0; i < 3; i++) {
            out[(i * 2) + 2] = c[i + 1];
            out[(i * 2) + 3] = c[i + 1];
        }
    } else {
        return 0;
    }
    out[8] = 0;
    return libaroma_rgb_to16(strtoul(out, NULL, 0));
}

/* Convert 16bit color to 32bit color */
uint32_t libaroma_rgb_to32(uint16_t rgb) {
return libaroma_rgb32(libaroma_color_r(rgb), libaroma_color_g(rgb), libaroma_color_b(rgb));
}

void libaroma_color_set(uint16_t *dst, uint16_t color, int n) {
    int i,left=n%32;
    if (n>=32) {
        for (i=0;i<32;i++) {
            dst[i]=color;
        }
        for (i=32;i<n-left;i+=32) {
            memcpy(dst+i,dst,64);
        }
    }
    if (left>0) {
        for (i=n-left;i<n;i++) {
            dst[i]=color;
        }
    }
}

void libaroma_color_copy32(uint32_t *dst, uint16_t *src, int n, uint8_t *rgb_pos) {
    int i;
    for (i = 0; i < n; i++) {
        uint16_t cl = src[i];
        dst[i] = (((libaroma_color_r(cl) & 0xff) << rgb_pos[0]) | ((libaroma_color_g(cl) & 0xff) << rgb_pos[1]) | ((libaroma_color_b(cl) & 0xff) << rgb_pos[2]));
    }
}

void libaroma_color_copy16(uint16_t *dst, uint32_t *src, int n, uint8_t *rgb_pos) {
    int i;
    for (i = 0; i < n; i++) {
        uint32_t cl = src[i];
        dst[i] = libaroma_rgb((uint8_t) ((cl >> rgb_pos[0]) & 0xff), (uint8_t) ((cl >> rgb_pos[1]) & 0xff), (uint8_t) ((cl >> rgb_pos[2]) & 0xff));
    }
}

uint16_t libaroma_alpha(uint16_t dcl, uint16_t scl, uint8_t l) {
    if (scl == dcl) {
        return scl;
    } else if (l == 0) {
        return dcl;
    } else if (l == 0xff) {
        return scl;
    }
    uint16_t na = l;
    uint16_t fa = 256 - na;
    return (uint16_t) (
        (((libaroma_color_r(dcl) * fa) +
        (libaroma_color_r(scl) * na)) >> 11 << 11) |
        (((libaroma_color_g(dcl) * fa) +
        (libaroma_color_g(scl) * na)) >> 10 << 5) |
        (((libaroma_color_b(dcl) * fa) +
        (libaroma_color_b(scl) * na)) >> 11)
    );
}
uint32_t libaroma_alpha32(uint16_t dcl, uint16_t scl, uint8_t l) {
    if (scl == dcl) {
        return libaroma_rgb_to32(scl);
    } else if (l == 0) {
        return libaroma_rgb_to32(dcl);
    } else if (l == 0xff) {
        return libaroma_rgb_to32(scl);
    }
    uint16_t na = l;
    uint16_t fa = 256 - na;
    return (uint32_t) (
        (((libaroma_color_r(dcl) * fa) +
        (libaroma_color_r(scl) * na)) >> 8 << 16) |
        (((libaroma_color_g(dcl) * fa) +
        (libaroma_color_g(scl) * na)) >> 8 << 8) |
        (((libaroma_color_b(dcl) * fa) +
        (libaroma_color_b(scl) * na)) >> 8) |
        (0xff << 24)
    );
}

uint16_t libaroma_alphab(uint16_t scl, uint8_t l) {
    if (l == 0) {
        return 0;
    } else if (l == 255) {
        return scl;
    }
    uint16_t na = l;
    return (uint16_t) (((libaroma_color_r(scl) * na) >> 11 << 11) | ((libaroma_color_g(scl) * na) >> 10 << 5) | ((libaroma_color_b(scl) * na) >> 11));
}

void libaroma_alpha_const(int n, uint16_t *dst, uint16_t *bottom, uint16_t *top, uint8_t alpha) {
    int i;

    for (i = 0; i < n; i++) {
        dst[i] = libaroma_alpha(bottom[i], top[i], alpha);
    }
}

void libaroma_alpha_const_line(int _Y, int n, uint16_t *dst, uint16_t *bottom, uint16_t *top, uint8_t alpha) {
    int i;
    for (i = 0; i < n; i++) {
        dst[i] = libaroma_dither(i, _Y, libaroma_alpha32(bottom[i], top[i], alpha));
    }
}

void libaroma_alpha_rgba_fill(int n, uint16_t *dst, uint16_t *bottom, uint16_t top, uint8_t alpha) {
    int i;
    for (i = 0; i < n; i++) {
        dst[i] = libaroma_alpha(bottom[i], top, alpha);
    }
}

void libaroma_btl32(int n, uint32_t *dst, const uint16_t *src) {
    int i;
    for (i = 0; i < n; i++) {
        dst[i] = libaroma_rgb_to32(src[i]);
    }
}

void libaroma_blt_align16(uint16_t *__restrict dst, uint16_t *__restrict src, int w, int h, int dst_stride, int src_stride) {
    int i;
    int w2 = w<<1;
    int ds = w2 + dst_stride;
    int ss = w2 + src_stride;
    uint8_t *d = (uint8_t *) dst;
    uint8_t *s = (uint8_t *) src;
    for (i = 0; i < h; i++) {
        memcpy(d+ds*i, s+ss*i, w2);
    }
}

void libaroma_blt_align16_to32(uint32_t *__restrict dst, uint16_t *__restrict src, int w, int h, int dst_stride, int src_stride) {
    int i;
    int dline = w+(dst_stride>>2);
    int sline = w+(src_stride>>1);
    for (i = 0; i < h; i++) {
        libaroma_btl32(w,dst+dline*i,src+sline*i);
    }
}

void libaroma_blt_align_to32_pos(uint32_t *__restrict dst, uint16_t *__restrict src, int w, int h, int dst_stride, int src_stride, uint8_t *rgb_pos) {
    int i;
    int dline = w+(dst_stride>>2);
    int sline = w+(src_stride>>1);
    for (i = 0; i < h; i++) {
        libaroma_color_copy32(dst+dline*i, src+sline*i, w, rgb_pos);
    }
}

void libaroma_blt_align_to16_pos(uint16_t *__restrict dst, uint32_t *__restrict src, int w, int h, int dst_stride, int src_stride, uint8_t *__restrict rgb_pos) {
    int i;
    int dline = w+(dst_stride>>1);
    int sline = w+(src_stride>>2);
    for (i = 0; i < h; i++) {
        libaroma_color_copy16(dst+dline*i, src+sline*i, w, rgb_pos);
    }
}
