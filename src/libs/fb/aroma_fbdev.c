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
 * File: aroma_fbdev.c
 * Description: fbdev framebuffer driver.
 *
 * This is part of libaroma, an embedded ui toolkit.
 *
 * Author(s):
 *   - Ahmad Amarullah (@amarullz) - 26/01/2015
 *   - Michael Jauregui (@MLXProjects) - 17/01/2023
 *   - Mohammad Afaneh (@afaneh92) - 09/12/2024
 *
 */

#include <stdbool.h>
#include <sys/mman.h>
#include "aroma_fb.h"

static GRSurface gr_framebuffer[2];
static bool double_buffered;
static GRSurface* gr_draw = NULL;
static int displayed_buffer;

static struct fb_var_screeninfo vi;
static struct fb_fix_screeninfo fi;
static int fb_fd = -1;
static __u32 smem_len;

static void set_displayed_framebuffer(unsigned n)
{
    if (n > 1 || !double_buffered) return;

    vi.yres_virtual = gr_framebuffer[0].height * 2;
    vi.yoffset = n * gr_framebuffer[0].height;
    vi.bits_per_pixel = gr_framebuffer[0].pixel_bytes * 8;
    if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        LOGW("active fb swap failed");
    } else {
        if (ioctl(fb_fd, FBIOPAN_DISPLAY, &vi) < 0) {
            LOGW("pan failed");
        }
    }
    displayed_buffer = n;
}

int fbdev_flush(LIBAROMA_FBP me) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;

    if (vi.red.offset == 8 || vi.red.offset == 16) {
        // In case of BGRA, do some byte swapping
        unsigned char* ucfb_vaddr = (unsigned char*)gr_draw->data;
        int idx;
        for (idx = 0 ; idx < (gr_draw->height * gr_draw->row_bytes);
                idx += 4) {
            unsigned char tmp = ucfb_vaddr[idx];
            ucfb_vaddr[idx    ] = ucfb_vaddr[idx + 2];
            ucfb_vaddr[idx + 2] = tmp;
        }
    }
    if (double_buffered) {
        // Copy from the in-memory surface to the framebuffer.
        memcpy(gr_framebuffer[1-displayed_buffer].data, gr_draw->data,
               gr_draw->height * gr_draw->row_bytes);
        set_displayed_framebuffer(1-displayed_buffer);
    } else {
        // Copy from the in-memory surface to the framebuffer.
        memcpy(gr_framebuffer[0].data, gr_draw->data,
               gr_draw->height * gr_draw->row_bytes);
    }

    mi->buffer = gr_draw->data;

    return 1;
}

void fbdev_set_dpi(LIBAROMA_FBP me) {
    if (me == NULL) {
        return;
    }
    me->dpi = 0;
    int dpi_fallback = floor(min(vi.xres,vi.yres)/160) * 80;
    if ((vi.width<= 0) || (vi.height <= 0)) {
        /* phone dpi */
        me->dpi = dpi_fallback;
    } else {
        /* calculate dpi */
        me->dpi = round(vi.xres / (vi.width * 0.039370) / 80) * 80;
    }
    if ((me->dpi<120)||(me->dpi>960)) {
        me->dpi = dpi_fallback;
    }
}

int fbdev_start_post(LIBAROMA_FBP me) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    libaroma_mutex_lock(mi->mutex);
    return 1;
}

int fbdev_end_post(LIBAROMA_FBP me) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    fbdev_flush(me);
    libaroma_mutex_unlock(mi->mutex);
    return 1;
}

int fbdev_post_32bit(
    LIBAROMA_FBP me, uint16_t *__restrict src,
    int dx, int dy, int dw, int dh,
    int sx, int sy, int sw, __unused int sh
    ) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    int sstride = (sw - dw) * 2;
    int dstride = (mi->line - (dw * mi->pixsz));
    uint32_t *copy_dst = (uint32_t *) (((uint8_t *) mi->buffer)+(mi->line * dy)+(dx * mi->pixsz));
    uint16_t *copy_src = (uint16_t *) (src + (sw * sy) + sx);
    libaroma_blt_align_to32_pos(copy_dst, copy_src, dw, dh, dstride, sstride, mi->rgb_pos);
    return 1;
}

int fbdev_snapshoot_32bit(LIBAROMA_FBP me, uint16_t *dst) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    libaroma_blt_align_to16_pos(dst, (uint32_t *) mi->buffer, me->w, me->h, 0, mi->stride, mi->rgb_pos);
    return 1;
}

void fbdev_init_32bit(LIBAROMA_FBP me) {
    if (me == NULL) {
        return;
    }
    LOGI("fbdev init 32bit colorspace");
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;

    /* calculate stride size */
    mi->stride = mi->line - (me->w * mi->pixsz);

    /* gralloc framebuffer subpixel position style */
    if (vi.red.offset==8) {
        libaroma_fb_changecolorspace(me,16,8,0);
    } else {
        libaroma_fb_changecolorspace(me,0,8,16);
    }

    /* set fbdev sync callbacks */
    me->start_post = &fbdev_start_post;
    me->end_post = &fbdev_end_post;
    me->post = &fbdev_post_32bit;
    me->snapshoot = &fbdev_snapshoot_32bit;
}

int fbdev_post_16bit(
    LIBAROMA_FBP me, uint16_t *__restrict src,
    int dx, int dy, int dw, int dh,
    int sx, int sy, int sw, __unused int sh
    ) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    int sstride = (sw - dw) * 2;
    int dstride = (mi->line - (dw * mi->pixsz));
    uint16_t *copy_dst = (uint16_t *) (((uint8_t *) mi->buffer)+(mi->line * dy)+(dx * mi->pixsz));
    uint16_t *copy_src = (uint16_t *) (src + (sw * sy) + sx);
    libaroma_blt_align16(copy_dst, copy_src, dw, dh, dstride, sstride);
    return 1;
}

int fbdev_snapshoot_16bit(LIBAROMA_FBP me, uint16_t *dst) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    libaroma_blt_align16(dst, (uint16_t *) mi->buffer, me->w, me->h, 0, mi->stride);
    return 1;
}

void fbdev_init_16bit(LIBAROMA_FBP me) {
    if (me == NULL) {
        return;
    }
    LOGI("fbdev init 16bit colorspace");
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;

    /* fix not standard 16bit framebuffer */
    if (mi->line / 4 == me->w) {
        mi->line = mi->line / 2;
    }

    /* calculate stride size */
    mi->stride = mi->line - (me->w * 2);

    /* set fbdev sync callbacks */
    me->start_post = &fbdev_start_post;
    me->end_post = &fbdev_end_post;
    me->post = &fbdev_post_16bit;
    me->snapshoot = &fbdev_snapshoot_16bit;
}

void fbdev_dump(LINUXFBDR_INTERNALP mi) {
    LOGI("FRAMEBUFFER INFORMATIONS:");
    LOGI("VAR");
    LOGI(" xres                : %i", vi.xres);
    LOGI(" yres                : %i", vi.yres);
    LOGI(" xres_virtual        : %i", vi.xres_virtual);
    LOGI(" yres_virtual        : %i", vi.yres_virtual);
    LOGI(" xoffset             : %i", vi.xoffset);
    LOGI(" yoffset             : %i", vi.yoffset);
    LOGI(" bits_per_pixel      : %i", vi.bits_per_pixel);
    LOGI(" grayscale           : %i", vi.grayscale);
    LOGI(" red                 : %i, %i, %i", vi.red.offset, vi.red.length, vi.red.msb_right);
    LOGI(" green               : %i, %i, %i", vi.green.offset, vi.green.length, vi.red.msb_right);
    LOGI(" blue                : %i, %i, %i", vi.blue.offset, vi.blue.length, vi.red.msb_right);
    LOGI(" transp              : %i, %i, %i", vi.transp.offset, vi.transp.length, vi.red.msb_right);
    LOGI(" nonstd              : %i", vi.nonstd);
    LOGI(" activate            : %i", vi.activate);
    LOGI(" height              : %i", vi.height);
    LOGI(" width               : %i", vi.width);
    LOGI(" accel_flags         : %i", vi.accel_flags);
    LOGI(" pixclock            : %i", vi.pixclock);
    LOGI(" left_margin         : %i", vi.left_margin);
    LOGI(" right_margin        : %i", vi.right_margin);
    LOGI(" upper_margin        : %i", vi.upper_margin);
    LOGI(" lower_margin        : %i", vi.lower_margin);
    LOGI(" hsync_len           : %i", vi.hsync_len);
    LOGI(" vsync_len           : %i", vi.vsync_len);
    LOGI(" sync                : %i", vi.sync);
    LOGI(" rotate              : %i", vi.rotate);
    LOGI("FIX");
    LOGI(" id                  : %s", fi.id);
    LOGI(" smem_len            : %i", fi.smem_len);
    LOGI(" type                : %i", fi.type);
    LOGI(" type_aux            : %i", fi.type_aux);
    LOGI(" visual              : %i", fi.visual);
    LOGI(" xpanstep            : %i", fi.xpanstep);
    LOGI(" ypanstep            : %i", fi.ypanstep);
    LOGI(" ywrapstep           : %i", fi.ywrapstep);
    LOGI(" line_length         : %i", fi.line_length);
    LOGI(" accel               : %i", fi.accel);
    LOGI(" line size           : %i", mi->line);
}

void fbdev_release(LIBAROMA_FBP me) {
    if (me == NULL) {
        return;
    }

    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;

    if (mi == NULL) {
        return;
    }

    /* unmap */
    if (mi->buffer != NULL) {
        LOGI("fbdev munmap buffer");
        munmap(mi->buffer, fi.smem_len);
        mi->buffer = NULL;
    }

    /* close fb */
    if (fb_fd >= 0) {
        LOGI("fbdev close fb-fd");
        close(fb_fd);
        fb_fd = -1;
    }

    if (gr_draw) {
        free(gr_draw->data);
        free(gr_draw);
    }
    gr_draw = NULL;
    munmap(gr_framebuffer[0].data, smem_len);

    /* destroy mutex & cond */
    libaroma_mutex_free(mi->mutex);

    /* free fbdev internal data */
    LOGI("fbdev free internal data");
    free(me->internal);
}

int fbdev_init(LIBAROMA_FBP me) {
    if (me == NULL) {
        return 0;
    }

    LOGS("aroma initialized fbdev internal data");

    /* allocating internal data */
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) calloc(sizeof(LINUXFBDR_INTERNAL),1);
    if (!mi) {
        LOGE("allocating fbdev internal data - memory error");
        return 0;
    }

    /* set internal address */
    me->internal = (void *) mi;

    /* set release callback */
    me->release = &fbdev_release;
  
    /* init mutex & cond */
    libaroma_mutex_init(mi->mutex);

    int retry = 20;
    int fd = -1;
    while (fd == -1) {
        fd = open("/dev/graphics/fb0", O_RDWR);
        if (fd == -1) {
            if (--retry) {
                // wait for init to create the device node
                LOGW("cannot open fb0 (retrying)");
                usleep(100000);
            } else {
                LOGE("cannot open fb0 (giving up)");
                goto error;
            }
        }
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        LOGE("failed to get fb0 info (FBIOGET_VSCREENINFO)");
        close(fd);
        goto error;
    }

    if (vi.red.length != 8) {
        // Changing fb_var_screeninfo can affect fb_fix_screeninfo,
        // so this needs done before querying for fi.
        LOGI("Forcing pixel format: RGB_565");
        vi.blue.offset    = 0;
        vi.green.offset   = 5;
        vi.red.offset     = 11;
        vi.blue.length    = 5;
        vi.green.length   = 6;
        vi.red.length     = 5;
        vi.blue.msb_right = 0;
        vi.green.msb_right = 0;
        vi.red.msb_right = 0;
        vi.transp.offset  = 0;
        vi.transp.length  = 0;
        vi.bits_per_pixel = 16;

        if (ioctl(fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
            LOGE("failed to put force_rgb_565 fb0 info");
            close(fd);
            goto error;
        }
    }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        LOGE("failed to get fb0 info (FBIOGET_FSCREENINFO)");
        close(fd);
        goto error;
    }

    /* dump display info */
    fbdev_dump(mi);

    /* set libaroma framebuffer instance values */
    me->w = vi.xres;			/* width */
    me->h = vi.yres;			/* height */
    me->sz = me->w * me->h;		/* width x height */

    /* set internal values */
    mi->line = fi.line_length;		/* line memory size */
    mi->depth = vi.bits_per_pixel;	/* color depth */
    mi->pixsz = mi->depth >> 3;		/* pixel size per byte */
    mi->fb_sz = (vi.xres_virtual * vi.yres_virtual * mi->pixsz);

    void* bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bits == MAP_FAILED) {
        LOGE("failed to mmap framebuffer");
        close(fd);
        goto error;
    }

    memset(bits, 0, fi.smem_len);

    gr_framebuffer[0].width = vi.xres;
    gr_framebuffer[0].height = vi.yres;
    gr_framebuffer[0].row_bytes = fi.line_length;
    gr_framebuffer[0].pixel_bytes = vi.bits_per_pixel / 8;
    //LOGI("Forcing line length");
    //vi.xres_virtual = fi.line_length / gr_framebuffer[0].pixel_bytes;
    gr_framebuffer[0].data = (uint8_t *)(bits);
    if (vi.bits_per_pixel == 16) {
        LOGI("setting GGL_PIXEL_FORMAT_RGB_565");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGB_565;
    } else if (vi.red.offset == 8 || vi.red.offset == 16) {
        LOGI("setting GGL_PIXEL_FORMAT_BGRA_8888");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_BGRA_8888;
    } else if (vi.red.offset == 0) {
        LOGI("setting GGL_PIXEL_FORMAT_RGBA_8888");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGBA_8888;
    } else if (vi.red.offset == 24) {
        LOGI("setting GGL_PIXEL_FORMAT_RGBX_8888");
        gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGBX_8888;
    } else {
        if (vi.red.length == 8) {
            LOGI("No valid pixel format detected, trying GGL_PIXEL_FORMAT_RGBX_8888");
            gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGBX_8888;
        } else {
            LOGI("No valid pixel format detected, trying GGL_PIXEL_FORMAT_RGB_565");
            gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGB_565;
        }
    }

    // Drawing directly to the framebuffer takes about 5 times longer.
    // Instead, we will allocate some memory and draw to that, then
    // memcpy the data into the framebuffer later.
    gr_draw = (GRSurface*) malloc(sizeof(GRSurface));
    if (!gr_draw) {
        LOGE("failed to allocate gr_draw");
        close(fd);
        munmap(bits, fi.smem_len);
        goto error;
    }
    memcpy(gr_draw, gr_framebuffer, sizeof(GRSurface));
    gr_draw->data = (unsigned char*) calloc(gr_draw->height * gr_draw->row_bytes, 1);
    if (!gr_draw->data) {
        LOGE("failed to allocate in-memory surface");
        close(fd);
        free(gr_draw);
        munmap(bits, fi.smem_len);
        goto error;
    }

    /* check if we can use double buffering */
    if (vi.yres * fi.line_length * 2 <= fi.smem_len) {
        double_buffered = true;
        me->double_buffer = 1;
        LOGI("double buffered");

        memcpy(gr_framebuffer+1, gr_framebuffer, sizeof(GRSurface));
        gr_framebuffer[1].data = gr_framebuffer[0].data +
            gr_framebuffer[0].height * gr_framebuffer[0].row_bytes;

    } else {
        double_buffered = false;
        me->double_buffer = 0;
        LOGI("single buffered");
    }
    fb_fd = fd;
    set_displayed_framebuffer(0);

    LOGI("framebuffer: %d (%d x %d)", fb_fd, gr_draw->width, gr_draw->height);

    smem_len = fi.smem_len;

    /* swap buffer now */
    fbdev_flush(me);

    if (mi->pixsz == 2) {
        /* init colorspace */
        fbdev_init_16bit(me);
    } else {
        /* init colorspace */
        fbdev_init_32bit(me);
    }

    /* set config */
    me->setrgb = &libaroma_fb_changecolorspace;

    /* set dpi */
    fbdev_set_dpi(me);

    return 1;

error:
    free(mi);
    return 0;
}
