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
 * File: aroma_drm.c
 * Description: drm framebuffer driver.
 *
 * This is part of libaroma, an embedded ui toolkit.
 *
 * Author(s):
 *   - Ahmad Amarullah (@amarullz) - 06/04/2015
 *   - Michael Jauregui (@MLXProjects) - 17/01/2023
 *   - Mohammad Afaneh (@afaneh92) - 09/12/2024
 *
 */

#include <drm_fourcc.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "aroma_fb.h"

#define ARRAY_SIZE(A) (sizeof(A)/sizeof(*(A)))

typedef struct drm_surface {
    GRSurface base;
    uint32_t fb_id;
    uint32_t handle;
}drm_surface;

static drm_surface *drm_surfaces[2];
static int current_buffer;
static GRSurface *draw_buf = NULL;

static drmModeCrtc *main_monitor_crtc;
static drmModeCrtc *orig_monitor_crtc;
static drmModeConnector *main_monitor_connector;

static int drm_fd = -1;

static void drm_disable_crtc(int drm_fd, drmModeCrtc *crtc) {
    if (crtc) {
        drmModeSetCrtc(drm_fd, crtc->crtc_id,
                       0, // fb_id
                       0, 0,  // x,y
                       NULL,  // connectors
                       0,     // connector_count
                       NULL); // mode
    }
}

static void drm_enable_crtc(int drm_fd, drmModeCrtc *crtc,
                            struct drm_surface *surface) {
    int32_t ret;

    ret = drmModeSetCrtc(drm_fd, crtc->crtc_id,
                         surface->fb_id,
                         0, 0,  // x,y
                         &main_monitor_connector->connector_id,
                         1,  // connector_count
                         &main_monitor_crtc->mode);

    if (ret)
        LOGW("drmModeSetCrtc failed ret=%d", ret);
}

static void drm_destroy_surface(struct drm_surface *surface) {
    struct drm_gem_close gem_close;
    int ret;

    if(!surface)
        return;

    if (surface->base.data)
        munmap(surface->base.data,
               surface->base.row_bytes * surface->base.height);

    if (surface->fb_id) {
        ret = drmModeRmFB(drm_fd, surface->fb_id);
        if (ret)
            LOGW("drmModeRmFB failed ret=%d", ret);
    }

    if (surface->handle) {
        memset(&gem_close, 0, sizeof(gem_close));
        gem_close.handle = surface->handle;

        ret = drmIoctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
        if (ret)
            LOGW("DRM_IOCTL_GEM_CLOSE failed ret=%d", ret);
    }

    free(surface);
}

static int drm_format_to_bpp(uint32_t format) {
    switch(format) {
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_BGRA8888:
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_BGRX8888:
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_ARGB8888:
        case DRM_FORMAT_XRGB8888:
            return 32;
        case DRM_FORMAT_RGB565:
            return 16;
        default:
            LOGI("Unknown format %d", format);
            return 32;
    }
}

static drm_surface *drm_create_surface(int width, int height) {
    struct drm_surface *surface;
    struct drm_mode_create_dumb create_dumb;
    uint32_t format;
    int ret;

    surface = (struct drm_surface*)calloc(1, sizeof(*surface));
    if (!surface) {
        LOGE("Can't allocate memory");
        return NULL;
    }

    if (1) {
        format = DRM_FORMAT_RGB565;
        LOGI("setting DRM_FORMAT_RGB565");
    } else {
        format = DRM_FORMAT_XBGR8888;
        LOGI("setting DRM_FORMAT_XBGR8888");
    }

    memset(&create_dumb, 0, sizeof(create_dumb));
    create_dumb.height = height;
    create_dumb.width = width;
    create_dumb.bpp = drm_format_to_bpp(format);
    create_dumb.flags = 0;

    ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
    if (ret) {
        LOGE("DRM_IOCTL_MODE_CREATE_DUMB failed ret=%d",ret);
        drm_destroy_surface(surface);
        return NULL;
    }
    surface->handle = create_dumb.handle;

    uint32_t handles[4], pitches[4], offsets[4];

    handles[0] = surface->handle;
    pitches[0] = create_dumb.pitch;
    offsets[0] = 0;

    ret = drmModeAddFB2(drm_fd, width, height,
            format, handles, pitches, offsets,
            &(surface->fb_id), 0);
    if (ret) {
        LOGE("drmModeAddFB2 failed ret=%d", ret);
        drm_destroy_surface(surface);
        return NULL;
    }

    struct drm_mode_map_dumb map_dumb;
    memset(&map_dumb, 0, sizeof(map_dumb));
    map_dumb.handle = create_dumb.handle;
    ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
    if (ret) {
        LOGE("DRM_IOCTL_MODE_MAP_DUMB failed ret=%d",ret);
        drm_destroy_surface(surface);
        return NULL;
    }

    surface->base.height = height;
    surface->base.width = width;
    surface->base.row_bytes = create_dumb.pitch;
    surface->base.pixel_bytes = create_dumb.bpp / 8;
    surface->base.data = (unsigned char*)
                         mmap(NULL,
                              surface->base.height * surface->base.row_bytes,
                              PROT_READ | PROT_WRITE, MAP_SHARED,
                              drm_fd, map_dumb.offset);
    if (surface->base.data == MAP_FAILED) {
        LOGE("mmap() failed");
        drm_destroy_surface(surface);
        return NULL;
    }

    return surface;
}

static drmModeCrtc *find_crtc_for_connector(int fd,
                            drmModeRes *resources,
                            drmModeConnector *connector) {
    int i, j;
    drmModeEncoder *encoder;
    int32_t crtc;

    /*
     * Find the encoder. If we already have one, just use it.
     */
    if (connector->encoder_id)
        encoder = drmModeGetEncoder(fd, connector->encoder_id);
    else
        encoder = NULL;

    if (encoder && encoder->crtc_id) {
        crtc = encoder->crtc_id;
        drmModeFreeEncoder(encoder);
        return drmModeGetCrtc(fd, crtc);
    }

    /*
     * Didn't find anything, try to find a crtc and encoder combo.
     */
    crtc = -1;
    for (i = 0; i < connector->count_encoders; i++) {
        encoder = drmModeGetEncoder(fd, connector->encoders[i]);

        if (encoder) {
            for (j = 0; j < resources->count_crtcs; j++) {
                if (!(encoder->possible_crtcs & (1 << j)))
                    continue;
                crtc = resources->crtcs[j];
                break;
            }
            if (crtc >= 0) {
                drmModeFreeEncoder(encoder);
                return drmModeGetCrtc(fd, crtc);
            }
        }
    }

    return NULL;
}

static drmModeConnector *find_used_connector_by_type(int fd,
                                 drmModeRes *resources,
                                 unsigned type) {
    int i;
    for (i = 0; i < resources->count_connectors; i++) {
        drmModeConnector *connector;

        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector) {
            if ((connector->connector_type == type) &&
                    (connector->connection == DRM_MODE_CONNECTED) &&
                    (connector->count_modes > 0))
                return connector;

            drmModeFreeConnector(connector);
        }
    }
    return NULL;
}

static drmModeConnector *find_first_connected_connector(int fd,
                             drmModeRes *resources) {
    int i;
    for (i = 0; i < resources->count_connectors; i++) {
        drmModeConnector *connector;

        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector) {
            if ((connector->count_modes > 0) &&
                    (connector->connection == DRM_MODE_CONNECTED))
                return connector;

            drmModeFreeConnector(connector);
        }
    }
    return NULL;
}

static drmModeConnector *find_main_monitor(int fd, drmModeRes *resources,
        uint32_t *mode_index) {
    unsigned i = 0;
    int modes;
    /* Look for LVDS/eDP/DSI connectors. Those are the main screens. */
    unsigned kConnectorPriority[] = {
        DRM_MODE_CONNECTOR_LVDS,
        DRM_MODE_CONNECTOR_eDP,
        DRM_MODE_CONNECTOR_DSI,
    };

    drmModeConnector *main_monitor_connector = NULL;
    do {
        main_monitor_connector = find_used_connector_by_type(fd,
                                         resources,
                                         kConnectorPriority[i]);
        i++;
    } while (!main_monitor_connector && i < ARRAY_SIZE(kConnectorPriority));

    /* If we didn't find a connector, grab the first one that is connected. */
    if (!main_monitor_connector)
        main_monitor_connector =
                find_first_connected_connector(fd, resources);

    /* If we still didn't find a connector, give up and return. */
    if (!main_monitor_connector)
        return NULL;

    *mode_index = 0;
    for (modes = 0; modes < main_monitor_connector->count_modes; modes++) {
        if (main_monitor_connector->modes[modes].type &
                DRM_MODE_TYPE_PREFERRED) {
            *mode_index = modes;
            break;
        }
    }

    return main_monitor_connector;
}

static void disable_non_main_crtcs(int fd,
                    drmModeRes *resources,
                    drmModeCrtc* main_crtc) {
    int i;
    drmModeCrtc* crtc;

    for (i = 0; i < resources->count_connectors; i++) {
        drmModeConnector *connector;

        connector = drmModeGetConnector(fd, resources->connectors[i]);
        crtc = find_crtc_for_connector(fd, resources, connector);
        if (crtc->crtc_id != main_crtc->crtc_id)
            drm_disable_crtc(fd, crtc);
        drmModeFreeCrtc(crtc);
    }
}

int drm_flush(LIBAROMA_FBP me) {
    if (me == NULL) {
        return 0;
    }

    int ret;

    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;

    memcpy(drm_surfaces[current_buffer]->base.data,
            draw_buf->data, draw_buf->height * draw_buf->row_bytes);

    ret = drmModePageFlip(drm_fd, main_monitor_crtc->crtc_id,
            drm_surfaces[current_buffer]->fb_id, 0, NULL);

    if (ret < 0) {
        LOGE("drmModePageFlip failed ret=%d", ret);
        return 0;
    }

    current_buffer = 1 - current_buffer;
    mi->buffer = draw_buf->data;

    return 1;
}

int drm_start_post(LIBAROMA_FBP me) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    libaroma_mutex_lock(mi->mutex);
    return 1;
}

int drm_post(
    LIBAROMA_FBP me, uint16_t *__restrict src,
    __unused int dx, __unused int dy, __unused int dw, __unused int dh,
    __unused int sx, __unused int sy, __unused int sw, __unused int sh
    ) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    /* DRM doesn't allow to update regions, so we must blit the entire buffer */
    if (mi->pixsz == 2) {
        libaroma_blt_align16((uint16_t *) mi->buffer, src, me->w, me->h, mi->stride, 0);
    } else {
        libaroma_blt_align16_to32((uint32_t *) mi->buffer, src, me->w, me->h, mi->stride, 0);
    }
    return 1;
}

int drm_end_post(LIBAROMA_FBP me) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    drm_flush(me);
    libaroma_mutex_unlock(mi->mutex);
    return 1;
}

static void drm_dump(LINUXFBDR_INTERNALP mi, drmModeCrtc *main_monitor_crtc, drmModeRes *res) {
    LOGI("DRM DRIVER INFORMATIONS:");
    LOGI("RES");
    LOGI(" count_fbs           : %i", res->count_fbs);
    LOGI(" count_crtcs         : %i", res->count_crtcs);
    LOGI(" count_connectors    : %i", res->count_connectors);
    LOGI(" count_encoders      : %i", res->count_encoders);
    LOGI(" min_width           : %i", res->min_width);
    LOGI(" max_width           : %i", res->max_width);
    LOGI(" min_height          : %i", res->min_height);
    LOGI(" max_height          : %i", res->max_height);
    LOGI("CRTC");
    LOGI(" id                  : %i", main_monitor_crtc->crtc_id);
    LOGI(" x                   : %i", main_monitor_crtc->x);
    LOGI(" y                   : %i", main_monitor_crtc->y);
    LOGI(" width               : %i", main_monitor_crtc->width);
    LOGI(" height              : %i", main_monitor_crtc->height);
    LOGI("MODE");
    LOGI(" hdisplay            : %i", main_monitor_crtc->mode.hdisplay);
    LOGI(" vdisplay            : %i", main_monitor_crtc->mode.vdisplay);
    LOGI(" htotal              : %i", main_monitor_crtc->mode.htotal);
    LOGI(" vtotal              : %i", main_monitor_crtc->mode.vtotal);
    LOGI(" clock               : %i", main_monitor_crtc->mode.clock);
    LOGI(" vrefresh            : %i", main_monitor_crtc->mode.vrefresh);
    LOGI(" name                : %s", main_monitor_crtc->mode.name);
    LOGI("DUMB FB");
    LOGI(" depth               : %i", mi->depth);
    LOGI(" pixsz               : %i", mi->pixsz);
    LOGI(" mem size            : %i", mi->fb_sz);
    LOGI(" line size           : %i", mi->line);
    LOGI(" stride              : %i", mi->stride);
}

void drm_release(LIBAROMA_FBP me) {
    if (me == NULL) {
        return;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    if (mi==NULL) {
        return;
    }
    drm_disable_crtc(drm_fd, main_monitor_crtc);
    drm_destroy_surface(drm_surfaces[0]);
    drm_destroy_surface(drm_surfaces[1]);
    drmModeFreeCrtc(main_monitor_crtc);
    drmModeFreeConnector(main_monitor_connector);
    if (orig_monitor_crtc) {
        drmModeSetCrtc(drm_fd, orig_monitor_crtc->crtc_id,
                       orig_monitor_crtc->buffer_id,
                       0, 0,  // x,y
                       &main_monitor_connector->connector_id,
                       1,  // connector_count
                       &orig_monitor_crtc->mode);
        drmModeFreeCrtc(orig_monitor_crtc);
    }
    if (mi->buffer!=NULL) {
        LOGI("drm_release munmap buffer");
        munmap(mi->buffer, mi->fb_sz);
        mi->buffer=NULL;
    }
    if (drm_fd >= 0) {
        LOGI("drm_release close fb-fd");
        close(drm_fd);
        drm_fd = -1;
    }
    libaroma_mutex_free(mi->mutex);
    free(me->internal);
}

int drm_init(LIBAROMA_FBP me) {
    if (me == NULL) {
        return 0;
    }

    LOGS("aroma initialized drm internal data");

    /* allocating internal data */
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) calloc(sizeof(LINUXFBDR_INTERNAL),1);
    if (!mi) {
        LOGE("allocating drm internal data - memory error");
        return 0;
    }

    /* set internal address */
    me->internal = (void *) mi;

    /* set release callback */
    me->release = &drm_release;

    /* init mutex & cond */
    libaroma_mutex_init(mi->mutex);

    drmModeRes *res = NULL;
    uint32_t selected_mode;
    char *dev_name;
    int width, height;
    int ret, i;

    /* Consider DRM devices in order. */
    for (i = 0; i < DRM_MAX_MINOR; i++) {
        uint64_t cap = 0;

        ret = asprintf(&dev_name, DRM_DEV_NAME, DRM_DIR_NAME, i);
        LOGV("trying %s", dev_name);
        if (ret < 0)
            continue;

        drm_fd = open(dev_name, O_RDWR, 0);
        free(dev_name);
        if (drm_fd < 0) {
            LOGW("open failed with error %s", strerror(errno));
            continue;
        }

        /* We need dumb buffers. */
        ret = drmGetCap(drm_fd, DRM_CAP_DUMB_BUFFER, &cap);
        if (ret || cap == 0) {
            close(drm_fd);
            continue;
        }

        res = drmModeGetResources(drm_fd);
        if (!res) {
            close(drm_fd);
            continue;
        }

        /* Use this device if it has at least one connected monitor. */
        if (res->count_crtcs > 0 && res->count_connectors > 0)
            if (find_first_connected_connector(drm_fd, res))
                break;

        drmModeFreeResources(res);
        close(drm_fd);
        res = NULL;
    }

    if (drm_fd < 0 || res == NULL) {
        LOGE("cannot find/open a drm device");
        goto fail;
    }

    main_monitor_connector = find_main_monitor(drm_fd,
            res, &selected_mode);

    if (!main_monitor_connector) {
        LOGE("main_monitor_connector not found");
        drmModeFreeResources(res);
        close(drm_fd);
        goto fail;
    }

    main_monitor_crtc = find_crtc_for_connector(drm_fd, res,
                                                main_monitor_connector);

    if (!main_monitor_crtc) {
        LOGE("main_monitor_crtc not found");
        drmModeFreeResources(res);
        close(drm_fd);
        goto fail;
    }

    orig_monitor_crtc = find_crtc_for_connector(drm_fd, res,
                                                main_monitor_connector);

    if (!orig_monitor_crtc) {
        LOGE("Could not backup the main crtc to restore when we're done. !");
        drmModeFreeResources(res);
        close(drm_fd);
        goto fail;
    }

    disable_non_main_crtcs(drm_fd,
                           res, main_monitor_crtc);

    LOGI("selected mode %d", selected_mode);
    main_monitor_crtc->mode = main_monitor_connector->modes[selected_mode];

    width = main_monitor_crtc->mode.hdisplay;
    height = main_monitor_crtc->mode.vdisplay;

    drmModeFreeResources(res);

    drm_surfaces[0] = drm_create_surface(width, height);
    drm_surfaces[1] = drm_create_surface(width, height);
    if (!drm_surfaces[0] || !drm_surfaces[1]) {
        drm_destroy_surface(drm_surfaces[0]);
        drm_destroy_surface(drm_surfaces[1]);
        drmModeFreeResources(res);
        close(drm_fd);
        goto fail;
    }

    draw_buf = (GRSurface *)malloc(sizeof(GRSurface));
    if (!draw_buf) {
        LOGE("failed to alloc draw_buf");
        drm_destroy_surface(drm_surfaces[0]);
        drm_destroy_surface(drm_surfaces[1]);
        drmModeFreeResources(res);
        close(drm_fd);
        goto fail;
    }

    memcpy(draw_buf, &drm_surfaces[0]->base, sizeof(GRSurface));

    /* set libaroma framebuffer instance values */
    me->w = draw_buf->width;		/* width */
    me->h = draw_buf->height;		/* height */
    me->sz = me->w * me->h;		/* width x height */

    /* set internal values */
    mi->line = draw_buf->row_bytes;	/* line memory size */
    mi->depth = draw_buf->pixel_bytes * 8;	/* color depth */
    mi->pixsz = draw_buf->pixel_bytes;	/* pixel size per byte */
    mi->fb_sz = draw_buf->height * draw_buf->row_bytes;
    mi->stride = (mi->line - (width * mi->pixsz));
    mi->buffer = draw_buf->data;

    draw_buf->data = (unsigned char *)calloc(draw_buf->height * draw_buf->row_bytes, 1);
    if (!draw_buf->data) {
        LOGE("failed to alloc draw_buf surface");
        free(draw_buf);
        drm_destroy_surface(drm_surfaces[0]);
        drm_destroy_surface(drm_surfaces[1]);
        drmModeFreeResources(res);
        close(drm_fd);
        goto fail;
    }

    drm_enable_crtc(drm_fd, main_monitor_crtc, drm_surfaces[1]);

    current_buffer = 0;

    /* dump display info */
    drm_dump(mi, main_monitor_crtc, res);

    /* set drm sync callbacks */
    me->start_post = &drm_start_post;
    me->end_post = &drm_end_post;
    me->post = &drm_post;
    me->snapshoot  = NULL;

    /* set config */
    me->setrgb = &libaroma_fb_changecolorspace;

    return 1;
fail:
    /* just use the release callback */
    LOGE("drm_init something failed, releasing!");
    drm_release(me);
    return 0;
}
