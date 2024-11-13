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
 * File: aroma_fb.h
 * Description: aroma framebuffer driver header.
 *
 * This is part of libaroma, an embedded ui toolkit.
 *
 * Author(s):
 *   - Ahmad Amarullah (@amarullz) - 26/01/2015
 *   - Michael Jauregui (@MLXProjects) - 17/01/2023
 *   - Mohammad Afaneh (@afaneh92) - 09/12/2024
 *
 */

#ifndef __aroma_fb_h__
#define __aroma_fb_h__

#include <linux/types.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <aroma.h>

#define LIBAROMA_MUTEX pthread_mutex_t
#define libaroma_mutex_init(x) pthread_mutex_init(&x,NULL)
#define libaroma_mutex_free(x) pthread_mutex_destroy(&x)
#define libaroma_mutex_lock(x) pthread_mutex_lock(&x)
#define libaroma_mutex_unlock(x) pthread_mutex_unlock(&x)

/*
 * Typedef	: LIBAROMA_FB
 * Descriptions	: Framebuffer type structure
 */
typedef struct _LIBAROMA_FB LIBAROMA_FB;
typedef struct _LIBAROMA_FB * LIBAROMA_FBP;

typedef struct _LINUXFBDR_INTERNAL LINUXFBDR_INTERNAL;
typedef struct _LINUXFBDR_INTERNAL * LINUXFBDR_INTERNALP;

/* structure : internal framebuffer data */
struct _LINUXFBDR_INTERNAL{
    int			fb;				/* framebuffer handler */
    int			fb_sz;				/* framebuffer memory size */
    void		*buffer;				/* direct buffer */
    int			stride;				/* stride size */
    int			line;				/* line size */
    uint8_t		depth;				/* color depth */
    uint8_t		pixsz;				/* memory size per pixel */
    uint8_t		rgb_pos[6];			/* framebuffer 32bit rgb position */

    uint8_t		double_buffering;		/* is double buffering? */

    pthread_t		thread;				/* flush thread handle */
    pthread_mutex_t	mutex;
    pthread_cond_t	cond;
};

/* structure : libaroma framebuffer data */
struct _LIBAROMA_FB{
    /* main info */
    int			w;				/* width */
    int			h;				/* height */
    int			sz;				/* width x height */
    uint8_t		double_buffer;			/* is double buffer driver */
    void		*internal;			/* driver internal data */

    /* callbacks */
    void (*release)(LIBAROMA_FBP);
    int (*snapshoot)(LIBAROMA_FBP, uint16_t *);

    /* post callbacks */
    int (*start_post)(LIBAROMA_FBP);
    int (*post)(LIBAROMA_FBP, uint16_t *__restrict, int, int, int, int, int, int, int, int);
    int (*end_post)(LIBAROMA_FBP);

    /* rgb setting callback */
    void (*setrgb)(LIBAROMA_FBP, uint8_t, uint8_t, uint8_t);

    /* Optional - DPI */
    int			dpi;
    uint8_t		bigscreen;

    /* post flag */
    uint8_t		onpost;

    /* AROMA CORE Runtime Data */
    uint16_t		*canvas;
};

typedef struct GRSurface {
    int width;
    int height;
    int row_bytes;
    int pixel_bytes;
    unsigned char* data;
    __u32 format;
} GRSurface;

enum GGLPixelFormat {
    // these constants need to match those
    // in graphics/PixelFormat.java, ui/PixelFormat.h, BlitHardware.h
    GGL_PIXEL_FORMAT_UNKNOWN    =   0,
    GGL_PIXEL_FORMAT_NONE       =   0,

    GGL_PIXEL_FORMAT_RGBA_8888   =   1,  // 4x8-bit ARGB
    GGL_PIXEL_FORMAT_RGBX_8888   =   2,  // 3x8-bit RGB stored in 32-bit chunks
    GGL_PIXEL_FORMAT_RGB_888     =   3,  // 3x8-bit RGB
    GGL_PIXEL_FORMAT_RGB_565     =   4,  // 16-bit RGB
    GGL_PIXEL_FORMAT_BGRA_8888   =   5,  // 4x8-bit BGRA
    GGL_PIXEL_FORMAT_RGBA_5551   =   6,  // 16-bit RGBA
    GGL_PIXEL_FORMAT_RGBA_4444   =   7,  // 16-bit RGBA

    GGL_PIXEL_FORMAT_A_8         =   8,  // 8-bit A
    GGL_PIXEL_FORMAT_L_8         =   9,  // 8-bit L (R=G=B = L)
    GGL_PIXEL_FORMAT_LA_88       = 0xA,  // 16-bit LA
    GGL_PIXEL_FORMAT_RGB_332     = 0xB,  // 8-bit RGB (non paletted)

    // reserved range. don't use.
    GGL_PIXEL_FORMAT_RESERVED_10 = 0x10,
    GGL_PIXEL_FORMAT_RESERVED_11 = 0x11,
    GGL_PIXEL_FORMAT_RESERVED_12 = 0x12,
    GGL_PIXEL_FORMAT_RESERVED_13 = 0x13,
    GGL_PIXEL_FORMAT_RESERVED_14 = 0x14,
    GGL_PIXEL_FORMAT_RESERVED_15 = 0x15,
    GGL_PIXEL_FORMAT_RESERVED_16 = 0x16,
    GGL_PIXEL_FORMAT_RESERVED_17 = 0x17,

    // reserved/special formats
    GGL_PIXEL_FORMAT_Z_16       =  0x18,
    GGL_PIXEL_FORMAT_S_8        =  0x19,
    GGL_PIXEL_FORMAT_SZ_24      =  0x1A,
    GGL_PIXEL_FORMAT_SZ_8       =  0x1B,

    // reserved range. don't use.
    GGL_PIXEL_FORMAT_RESERVED_20 = 0x20,
    GGL_PIXEL_FORMAT_RESERVED_21 = 0x21,
};

LIBAROMA_FBP libaroma_fb();

LIBAROMA_FBP libaroma_fb_init();

int libaroma_fb_release();

int libaroma_fb_sync();

void libaroma_fb_changecolorspace(LIBAROMA_FBP me, uint8_t r, uint8_t g, uint8_t b);

void fbdev_set_dpi(LIBAROMA_FBP me);

int drm_init(LIBAROMA_FBP me);

int overlay_init(LIBAROMA_FBP me);

int fbdev_init(LIBAROMA_FBP me);

#endif /* __aroma_fb_h__ */
