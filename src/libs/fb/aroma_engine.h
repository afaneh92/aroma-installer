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
 * File: aroma_engine.h
 * Description: aroma engine headers.
 *
 * This is part of libaroma, an embedded ui toolkit.
 *
 * Author(s):
 *   - Ahmad Amarullah (@amarullz) - 26/01/2015
 *   - Michael Jauregui (@MLXProjects) - 17/01/2023
 *   - Mohammad Afaneh (@afaneh92) - 09/12/2024
 *
 */

#ifndef __aroma_engine_h__
#define __aroma_engine_h__

/* color macros */
#ifdef RGB
#pragma message "RGB already defined, overriding"
#endif
#define RGB(X) libaroma_rgb_to16(0x##X)
#define libaroma_color_close_b libaroma_color_close_r

/* inline color functions */
static inline uint8_t libaroma_color_r(uint16_t rgb) {
    return ((uint8_t) (((((uint16_t)(rgb)) & 0xF800)) >> 8));
}
static inline uint8_t libaroma_color_g(uint16_t rgb) {
    return ((uint8_t) (((((uint16_t)(rgb)) & 0x07E0)) >> 3));
}
static inline uint8_t libaroma_color_b(uint16_t rgb) {
    return ((uint8_t) (((((uint16_t)(rgb)) & 0x001F)) << 3));
}
static inline uint8_t libaroma_color_hi_g(uint8_t v){
    return (v | (v >> 6));
}
static inline uint8_t libaroma_color_r32(uint32_t rgb) {
    return (uint8_t) ((rgb >> 16) & 0xff);
}
static inline uint8_t libaroma_color_g32(uint32_t rgb) {
    return (uint8_t) ((rgb >> 8) & 0xff);
}
static inline uint8_t libaroma_color_b32(uint32_t rgb) {
    return (uint8_t) (rgb & 0xff);
}
static inline uint8_t libaroma_color_a32(uint32_t rgb) {
    return (uint8_t) ((rgb >> 24) & 0xff);
}
static inline uint8_t libaroma_color_close_r(uint8_t c) {
    return (((uint8_t) c) >> 3 << 3);
}
static inline uint8_t libaroma_color_close_g(uint8_t c) {
    return (((uint8_t) c) >> 2 << 2);
}
static inline uint8_t libaroma_color_left(uint8_t r, uint8_t g, uint8_t b) {
    return ((((r - libaroma_color_close_r(r)) & 7) << 5) | (((g - libaroma_color_close_g(g)) & 3) << 3) | ((b - libaroma_color_close_b(b)) & 7));
}
static inline uint16_t libaroma_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)((r >> 3) << 11)|((g >> 2) << 5) | (b >> 3));
}
static inline uint32_t libaroma_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t) (((r & 0xff)<<16)|((g & 0xff)<<8)|(b & 0xff)|((a & 0xff) << 24));
}
static inline uint32_t libaroma_rgb32(uint8_t r, uint8_t g, uint8_t b) {
    return libaroma_rgba(r, g, b, 0xff);
}
static inline uint16_t libaroma_rgb_to16(uint32_t rgb) {
    return libaroma_rgb(libaroma_color_r32(rgb), libaroma_color_g32(rgb), libaroma_color_b32(rgb));
}

/* vector color functions */
void libaroma_color_set(uint16_t *__restrict dst, uint16_t color, int n);
void libaroma_color_copy32(uint32_t *__restrict dst, uint16_t *__restrict src, int n, uint8_t *__restrict rgb_pos);
void libaroma_color_copy16(uint16_t *dst, uint32_t *src, int n, uint8_t *rgb_pos);

/* vector alpha blend */
void libaroma_alpha_const(int n, uint16_t *__restrict dst, uint16_t *__restrict bottom, uint16_t *__restrict top, uint8_t alpha);
void libaroma_alpha_const_line(int _Y, int n, uint16_t *__restrict dst, uint16_t *__restrict bottom, uint16_t *__restrict top, uint8_t alpha);
void libaroma_alpha_rgba_fill(int n, uint16_t *__restrict dst, uint16_t *__restrict bottom, uint16_t top, uint8_t alpha);

/* vector blitting */
void libaroma_blt_align16(uint16_t *__restrict dst, uint16_t *__restrict src, int w, int h, int dst_stride, int src_stride);
void libaroma_blt_align16_to32(uint32_t *__restrict dst, uint16_t *__restrict src, int w, int h, int dst_stride, int src_stride);
void libaroma_blt_align_to32_pos(uint32_t *__restrict dst, uint16_t *__restrict src, int w, int h, int dst_stride, int src_stride, uint8_t *rgb_pos);
void libaroma_blt_align_to16_pos(uint16_t *__restrict dst, uint32_t *__restrict src, int w, int h, int dst_stride, int src_stride, uint8_t *__restrict rgb_pos);

/* scalar color functions */
uint16_t libaroma_rgb_from_string(const char * c);
uint32_t libaroma_rgb_to32(uint16_t rgb);

/* scalar alpha blend */
uint16_t libaroma_alpha(uint16_t dcl, uint16_t scl, uint8_t l);
uint32_t libaroma_alpha32(uint16_t dcl, uint16_t scl, uint8_t l);
uint16_t libaroma_alphab(uint16_t scl, uint8_t l);

#endif /* __aroma_engine_h__ */
