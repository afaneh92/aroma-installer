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
 * File: aroma_fb.c
 * Description: aroma framebuffer handler.
 *
 * This is part of libaroma, an embedded ui toolkit.
 *
 * Author(s):
 *   - Ahmad Amarullah (@amarullz) - 06/04/2015
 *   - Michael Jauregui (@MLXProjects) - 17/01/2023
 *   - Mohammad Afaneh (@afaneh92) - 09/12/2024
 *
 */

#include <stddef.h>
#include "aroma_fb.h"

static LIBAROMA_FBP _libaroma_fb=NULL;
static LIBAROMA_MUTEX _libaroma_fb_onpost_mutex;

void libaroma_runtime_activate_cores(int num_cores) {
    int i;
    FILE * fp;
    struct stat st;
    char path[256];
    for (i=0;i<num_cores;i++) {
        snprintf(path,256,"/sys/devices/system/cpu/cpu%i/online",i);
        if (stat(path,&st)<0) {
            break;
        }
        fp = fopen(path, "w+");
        if(fp) {
            fputc('1',fp);
            fclose(fp);
        }
    }
}

LIBAROMA_FBP libaroma_fb() {
    return _libaroma_fb;
}

int libaroma_dp(int dp) {
    if (_libaroma_fb == NULL) {
        LOGW("libaroma_dp framebuffer uninitialized");
        return 0;
    }
    int ret = (dp * _libaroma_fb->dpi) / 160;
    if (ret < 1 && _libaroma_fb->dpi < 160) return 1; /* must return 1 even on ldpi */
    return ret;
}

int libaroma_px(int px) {
    if (_libaroma_fb == NULL) {
        LOGW("libaroma_px framebuffer uninitialized");
        return 0;
    }
    return (px * 160 / _libaroma_fb->dpi);
}

int libaroma_width_dp() {
    if (_libaroma_fb == NULL) {
        LOGW("libaroma_width_dp framebuffer uninitialized");
        return 0;
    }
    return libaroma_px(_libaroma_fb->w);
}

int libaroma_height_dp() {
    if (_libaroma_fb == NULL) {
        LOGW("libaroma_height_dp framebuffer uninitialized");
        return 0;
    }
    return libaroma_px(_libaroma_fb->h);
}

int libaroma_fb_snapshoot() {
    if (_libaroma_fb == NULL) {
        LOGW("libaroma_fb_snapshoot framebuffer uninitialized");
        return 0;
    }
    if (_libaroma_fb->snapshoot == NULL) {
        LOGW("framebuffer driver do not support snapshoot");
        return 0;
    }
    /* get */
    return _libaroma_fb->snapshoot(_libaroma_fb, _libaroma_fb->canvas);
}

int libaroma_fb_driver_init(LIBAROMA_FBP me) {
    /* try drm driver */
    if (drm_init(me)) {
        LOGS("using drm framebuffer driver");
        return 1;
    }

    /* try qcom overlay driver */
    if (overlay_init(me)) {
        LOGS("using qcom overlay framebuffer driver");
        return 1;
    }

    /* try fbdev driver */
    if (fbdev_init(me)) {
        LOGS("using fbdev framebuffer driver");
    }

    return 1;
}

LIBAROMA_FBP libaroma_fb_init() {
    if (_libaroma_fb != NULL) {
        LOGI("libaroma_fb_init framebuffer already initialized");
        return 0;
    }

    libaroma_runtime_activate_cores(8);

    /* allocating instance memory */
    LOGI("libaroma_fb_init allocating framebuffer instance");
    _libaroma_fb = (LIBAROMA_FBP) calloc(sizeof(LIBAROMA_FB),1);
    if (!_libaroma_fb) {
        LOGE("libaroma_fb_init allocating framebuffer instance failed");
        return 0;
    }

    LOGI("Init framebuffer driver - default");
    if (libaroma_fb_driver_init(_libaroma_fb) == 0) {
        free(_libaroma_fb);
        _libaroma_fb = NULL;
        LOGE("libaroma_fb_init driver error");
        return 0;
    }

    /* check callbacks */
    if ((_libaroma_fb->release == NULL) || (_libaroma_fb->start_post == NULL) || (_libaroma_fb->end_post == NULL) || (_libaroma_fb->post == NULL)) {
        free(_libaroma_fb);
        _libaroma_fb = NULL;
        LOGE("libaroma_fb_init driver doesn't set the callbacks");
        return 0;
    }

    /* check dpi */
    if ((_libaroma_fb->dpi < 160) || (_libaroma_fb->dpi > 960)) {
        /* use phone dpi */
        _libaroma_fb->dpi = floor(min(_libaroma_fb->w, _libaroma_fb->h)/160) * 80;
        LOGW("libaroma_fb_init driver doesn't set dpi. set as : %i dpi", _libaroma_fb->dpi);
    }

    /* make sure the dpi is valid */
    if ((_libaroma_fb->dpi < 160) || (_libaroma_fb->dpi > 960)) {
        _libaroma_fb->dpi = 160;
    }

    /* check big screen */
    int dpMinWH = min(libaroma_width_dp(), libaroma_height_dp());
    _libaroma_fb->bigscreen = (dpMinWH >= 600); 

    /* create framebuffer canvas */
    if (!_libaroma_fb->canvas) {
        _libaroma_fb->canvas = (uint16_t *) malloc(_libaroma_fb->sz*2);
        memset(_libaroma_fb->canvas,0,_libaroma_fb->sz*2);
    }

    /* Show Information */
    LOGS("Framebuffer Initialized (%ix%ipx - %i dpi - %s)", _libaroma_fb->w, _libaroma_fb->h, _libaroma_fb->dpi, _libaroma_fb->double_buffer?"Double Buffer":"Single Buffer");

    /* Copy Current Framebuffer Into Display Canvas */
    if (_libaroma_fb->snapshoot != NULL) {
        LOGI("Copy framebuffer pixels into canvas");
        libaroma_fb_snapshoot();
    }

    _libaroma_fb->onpost = 0;
    libaroma_mutex_init(_libaroma_fb_onpost_mutex);

    return _libaroma_fb;
}

int libaroma_fb_start_post() {
    if (_libaroma_fb == NULL) {
        LOGW("libaroma_fb_start_post framebuffer uninitialized");
        return 0;
    }
    libaroma_mutex_lock(_libaroma_fb_onpost_mutex);
    if (_libaroma_fb->start_post(_libaroma_fb)) {
        _libaroma_fb->onpost=1;
        return 1;
    }
    libaroma_mutex_unlock(_libaroma_fb_onpost_mutex);
    return 0;
}

int libaroma_fb_end_post() {
    if (_libaroma_fb == NULL) {
        LOGW("libaroma_fb_end_post framebuffer uninitialized");
        return 0;
    }
    if (!_libaroma_fb->onpost) {
        LOGW("libaroma_fb_end_post start_post not called");
        return 0;
    }
    _libaroma_fb->end_post(_libaroma_fb);
    _libaroma_fb->onpost=0;
    libaroma_mutex_unlock(_libaroma_fb_onpost_mutex);
    return 1;
}

int libaroma_fb_post(
    uint16_t *canvas,
    int dx, int dy,
    int sx, int sy,
    int w, int h
) {
    if (_libaroma_fb == NULL) {
        LOGW("libaroma_fb_post framebuffer uninitialized");
        return 0;
    }
    if (!_libaroma_fb->onpost) {
        LOGW("libaroma_fb_post start_post not called");
        return 0;
    }

    /* check x position */
    if ((sx>_libaroma_fb->w) || (sy>_libaroma_fb->h) || (dx>_libaroma_fb->w) || (dy>_libaroma_fb->h) || (sx<0)||(sy<0)||(dx<0)||(dy<0)) {
        LOGW("libaroma_fb_post x,y position is invalid");
        return 0;
    }
    if (sx+w > _libaroma_fb->w) {
        w = _libaroma_fb->w-sx;
    }
    if (sy+h > _libaroma_fb->h) {
        h = _libaroma_fb->h-sy;
    }
    if (dx+w > _libaroma_fb->w) {
        w = _libaroma_fb->w-dx;
    }
    if (dy+h > _libaroma_fb->h) {
        h = _libaroma_fb->h-dy;
    }
    if ((w<1) || (h<1)) {
        /* no need to post */
        return 1;
    }
    return _libaroma_fb->post(_libaroma_fb, canvas, dx, dy, w, h, sx, sy, _libaroma_fb->w, _libaroma_fb->h);
}

int libaroma_fb_release() {
    if (_libaroma_fb == NULL) {
        LOGW("libaroma_fb_release framebuffer uninitialized");
        return 0;
    }

    /* Free display canvas */
    LOGI("Releasing Canvas");
    free(_libaroma_fb->canvas);

    /* Release Framebuffer Driver */
    LOGI("Releasing Framebuffer Driver");
    _libaroma_fb->release(_libaroma_fb);

    /* Show Information */
    LOGS("Framebuffer Released");

    /* cleanup post event */
    if (_libaroma_fb->onpost) {
        libaroma_fb_end_post();
    }
    _libaroma_fb->onpost=0;
    libaroma_mutex_free(_libaroma_fb_onpost_mutex);

    /* Free Framebuffer Instance */
    free(_libaroma_fb);
    
    /* Set Null */
    _libaroma_fb = NULL;

    return 1;
}

int libaroma_fb_sync() {
    if (_libaroma_fb == NULL) {
        LOGW("libaroma_fb_sync framebuffer uninitialized");
        return 0;
    }

    int ret=0;
    if (libaroma_fb_start_post()) {
        if (_libaroma_fb->post(_libaroma_fb, _libaroma_fb->canvas, 0, 0, _libaroma_fb->w, _libaroma_fb->h, 0, 0, _libaroma_fb->w, _libaroma_fb->h)) {
            ret = 1;
        }
        libaroma_fb_end_post();
    }
    return ret;
}

void libaroma_fb_setrgb(uint8_t r, uint8_t g, uint8_t b) {
    if (_libaroma_fb == NULL) {
        LOGW("libaroma_fb_setrgb framebuffer uninitialized");
        return;
    }
    if (_libaroma_fb->setrgb != NULL)
        _libaroma_fb->setrgb(_libaroma_fb, r, g, b);
}

void libaroma_fb_changecolorspace(LIBAROMA_FBP me, uint8_t r, uint8_t g, uint8_t b) {
    if (me == NULL) {
        return;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    /* save color position */
    mi->rgb_pos[0] = r;
    mi->rgb_pos[1] = g;
    mi->rgb_pos[2] = b;
    mi->rgb_pos[3] = r >> 3;
    mi->rgb_pos[4] = g >> 3;
    mi->rgb_pos[5] = b >> 3;
}
