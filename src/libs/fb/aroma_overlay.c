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
 * File: aroma_overlay.c
 * Description: qcom overlay framebuffer driver.
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
#include <errno.h>
#include <sys/mman.h>
#include <linux/msm_mdp.h>
#include <linux/msm_ion.h>

#include "aroma_fb.h"

#define MDP_V4_0 400
#define MAX_DISPLAY_DIM  2048

static GRSurface gr_framebuffer;
static GRSurface* gr_draw = NULL;

static struct fb_var_screeninfo vi;
static struct fb_fix_screeninfo fi;
static int fb_fd = -1;
static bool isMDP5 = false;
static int leftSplit = 0;
static int rightSplit = 0;
#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))

static size_t frame_size = 0;

typedef struct {
    unsigned char *mem_buf;
    int size;
    int ion_fd;
    int mem_fd;
    struct ion_handle_data handle_data;
} memInfo;

//Left and right overlay id
static int overlayL_id = MSMFB_NEW_REQUEST;
static int overlayR_id = MSMFB_NEW_REQUEST;

static memInfo mem_info;

static int map_mdp_pixel_format()
{
    if (gr_framebuffer.format == GGL_PIXEL_FORMAT_RGB_565)
        return MDP_RGB_565;
    else if (gr_framebuffer.format == GGL_PIXEL_FORMAT_BGRA_8888)
        return MDP_BGRA_8888;
    else if (gr_framebuffer.format == GGL_PIXEL_FORMAT_RGBA_8888)
        return MDP_RGBA_8888;
    else if (gr_framebuffer.format == GGL_PIXEL_FORMAT_RGBX_8888)
        return MDP_RGBA_8888;
    LOGI("No known pixel format for map_mdp_pixel_format, defaulting to MDP_RGB_565.");
    return MDP_RGB_565;
}

bool target_has_overlay(char *version)
{
    int mdp_version;
    bool overlay_supported = false;

    if (strlen(version) >= 8) {
        if(!strncmp(version, "msmfb", strlen("msmfb"))) {
            char str_ver[4];
            memcpy(str_ver, version + strlen("msmfb"), 3);
            str_ver[3] = '\0';
            mdp_version = atoi(str_ver);
            if (mdp_version >= MDP_V4_0) {
                overlay_supported = true;
            }
        } else if (!strncmp(version, "mdssfb", strlen("mdssfb"))) {
            overlay_supported = true;
            isMDP5 = true;
        }
    }

    return overlay_supported;
}

void setDisplaySplit(void) {
    char split[64] = {0};
    if (!isMDP5)
        return;
    FILE* fp = fopen("/sys/class/graphics/fb0/msm_fb_split", "r");
    if (fp) {
        //Format "left right" space as delimiter
        if(fread(split, sizeof(char), 64, fp)) {
            leftSplit = atoi(split);
            LOGI("Left Split=%d",leftSplit);
            char *rght = strpbrk(split, " ");
            if (rght)
                rightSplit = atoi(rght + 1);
            LOGI("Right Split=%d", rightSplit);
        }
    } else {
        LOGI("Failed to open mdss_fb_split node");
    }
    if (fp)
        fclose(fp);
}

int getLeftSplit(void) {
   //Default even split for all displays with high res
   int lSplit = vi.xres / 2;

   //Override if split published by driver
   if (leftSplit)
       lSplit = leftSplit;

   return lSplit;
}

int getRightSplit(void) {
   return rightSplit;
}

int free_ion_mem(void) {
    int ret = 0;

    if (mem_info.mem_buf)
        munmap(mem_info.mem_buf, mem_info.size);

    if (mem_info.ion_fd >= 0) {
        ret = ioctl(mem_info.ion_fd, ION_IOC_FREE, &mem_info.handle_data);
        if (ret < 0)
            LOGE("free_mem failed");
    }

    if (mem_info.mem_fd >= 0)
        close(mem_info.mem_fd);
    if (mem_info.ion_fd >= 0)
        close(mem_info.ion_fd);

    memset(&mem_info, 0, sizeof(mem_info));
    mem_info.mem_fd = -1;
    mem_info.ion_fd = -1;
    return 0;
}

int alloc_ion_mem(unsigned int size)
{
    int result;
    struct ion_fd_data fd_data;
    struct ion_allocation_data ionAllocData;

    mem_info.ion_fd = open("/dev/ion", O_RDWR|O_DSYNC);
    if (mem_info.ion_fd < 0) {
        LOGE("ERROR: Can't open ion");
        return -errno;
    }

    ionAllocData.flags = 0;
    ionAllocData.len = size;
    ionAllocData.align = sysconf(_SC_PAGESIZE);
// are you kidding me -.-
#if (PLATFORM_SDK_VERSION >= 21)
    ionAllocData.heap_id_mask =
#else
    ionAllocData.heap_mask =
#endif
            ION_HEAP(ION_IOMMU_HEAP_ID) |
            ION_HEAP(ION_SYSTEM_CONTIG_HEAP_ID);

    result = ioctl(mem_info.ion_fd, ION_IOC_ALLOC,  &ionAllocData);
    if(result){
        LOGE("ION_IOC_ALLOC Failed");
        close(mem_info.ion_fd);
        return result;
    }

    fd_data.handle = ionAllocData.handle;
    mem_info.handle_data.handle = ionAllocData.handle;
    result = ioctl(mem_info.ion_fd, ION_IOC_MAP, &fd_data);
    if (result) {
        LOGE("ION_IOC_MAP Failed");
        free_ion_mem();
        return result;
    }
    mem_info.mem_buf = (unsigned char *)mmap(NULL, size, PROT_READ |
                PROT_WRITE, MAP_SHARED, fd_data.fd, 0);
    mem_info.mem_fd = fd_data.fd;

    if (!mem_info.mem_buf) {
        LOGE("ERROR: mem_buf MAP_FAILED");
        free_ion_mem();
        return -ENOMEM;
    }

    return 0;
}

bool isDisplaySplit(void) {
    if (vi.xres > MAX_DISPLAY_DIM)
        return true;
    //check if right split is set by driver
    if (getRightSplit())
        return true;

    return false;
}

int allocate_overlay(int fd, GRSurface gr_fb)
{
    int ret = 0;

    if (!isDisplaySplit()) {
        // Check if overlay is already allocated
        if (MSMFB_NEW_REQUEST == overlayL_id) {
            struct mdp_overlay overlayL;

            memset(&overlayL, 0 , sizeof (struct mdp_overlay));

            /* Fill Overlay Data */
            overlayL.src.width  = ALIGN(gr_fb.width, 32);
            overlayL.src.height = gr_fb.height;
            overlayL.src.format = map_mdp_pixel_format();
            overlayL.src_rect.w = gr_fb.width;
            overlayL.src_rect.h = gr_fb.height;
            overlayL.dst_rect.w = gr_fb.width;
            overlayL.dst_rect.h = gr_fb.height;
            overlayL.alpha = 0xFF;
            // If this worked, life would have been so much easier
            //switch (gr_rotation) {
                //case   0:  overlayL.flags = MDP_ROT_NOP; break;
                //case  90:  overlayL.flags = MDP_ROT_90;  break;
                //case 180:  overlayL.flags = MDP_ROT_180; break;
                //case 270:  overlayL.flags = MDP_ROT_270; break;
            //}
            overlayL.transp_mask = MDP_TRANSP_NOP;
            overlayL.id = MSMFB_NEW_REQUEST;
            ret = ioctl(fd, MSMFB_OVERLAY_SET, &overlayL);
            if (ret < 0) {
                LOGE("Overlay Set Failed");
                return ret;
            }
            overlayL_id = overlayL.id;
        }
    } else {
        float xres = vi.xres;
        int lSplit = getLeftSplit();
        float lSplitRatio = lSplit / xres;
        float lCropWidth = gr_fb.width * lSplitRatio;
        int lWidth = lSplit;
        int rWidth = gr_fb.width - lSplit;
        int height = gr_fb.height;

        if (MSMFB_NEW_REQUEST == overlayL_id) {

            struct mdp_overlay overlayL;

            memset(&overlayL, 0 , sizeof (struct mdp_overlay));

            /* Fill OverlayL Data */
            overlayL.src.width  = ALIGN(gr_fb.width, 32);
            overlayL.src.height = gr_fb.height;
            overlayL.src.format = map_mdp_pixel_format();
            overlayL.src_rect.x = 0;
            overlayL.src_rect.y = 0;
            overlayL.src_rect.w = lCropWidth;
            overlayL.src_rect.h = gr_fb.height;
            overlayL.dst_rect.x = 0;
            overlayL.dst_rect.y = 0;
            overlayL.dst_rect.w = lWidth;
            overlayL.dst_rect.h = height;
            overlayL.alpha = 0xFF;
            // If this worked, life would have been so much easier
            //switch (gr_rotation) {
                //case   0:  overlayL.flags = MDP_ROT_NOP; break;
                //case  90:  overlayL.flags = MDP_ROT_90;  break;
                //case 180:  overlayL.flags = MDP_ROT_180; break;
                //case 270:  overlayL.flags = MDP_ROT_270; break;
            //}
            overlayL.transp_mask = MDP_TRANSP_NOP;
            overlayL.id = MSMFB_NEW_REQUEST;
            ret = ioctl(fd, MSMFB_OVERLAY_SET, &overlayL);
            if (ret < 0) {
                LOGE("OverlayL Set Failed");
                return ret;
            }
            overlayL_id = overlayL.id;
        }
        if (MSMFB_NEW_REQUEST == overlayR_id) {
            struct mdp_overlay overlayR;

            memset(&overlayR, 0 , sizeof (struct mdp_overlay));

            /* Fill OverlayR Data */
            overlayR.src.width  = ALIGN(gr_fb.width, 32);
            overlayR.src.height = gr_fb.height;
            overlayR.src.format = map_mdp_pixel_format();
            overlayR.src_rect.x = lCropWidth;
            overlayR.src_rect.y = 0;
            overlayR.src_rect.w = gr_fb.width - lCropWidth;
            overlayR.src_rect.h = gr_fb.height;
            overlayR.dst_rect.x = 0;
            overlayR.dst_rect.y = 0;
            overlayR.dst_rect.w = rWidth;
            overlayR.dst_rect.h = height;
            overlayR.alpha = 0xFF;
            overlayR.flags = MDSS_MDP_RIGHT_MIXER;
            // If this worked, life would have been so much easier
            //switch (gr_rotation) {
                //case   0:  overlayR.flags |= MDP_ROT_NOP; break;
                //case  90:  overlayR.flags |= MDP_ROT_90;  break;
                //case 180:  overlayR.flags |= MDP_ROT_180; break;
                //case 270:  overlayR.flags |= MDP_ROT_270; break;
            //}
            overlayR.transp_mask = MDP_TRANSP_NOP;
            overlayR.id = MSMFB_NEW_REQUEST;
            ret = ioctl(fd, MSMFB_OVERLAY_SET, &overlayR);
            if (ret < 0) {
                LOGE("OverlayR Set Failed");
                return ret;
            }
            overlayR_id = overlayR.id;
        }

    }
    return 0;
}

int overlay_display_frame(int fd, void* data, size_t size)
{
    int ret = 0;
    struct msmfb_overlay_data ovdataL, ovdataR;
    struct mdp_display_commit ext_commit;

    if (!isDisplaySplit()) {
        if (overlayL_id == MSMFB_NEW_REQUEST) {
            LOGE("display_frame failed, no overlay");
            return -EINVAL;
        }

        memcpy(mem_info.mem_buf, data, size);

        memset(&ovdataL, 0, sizeof(struct msmfb_overlay_data));

        ovdataL.id = overlayL_id;
        ovdataL.data.flags = 0;
        ovdataL.data.offset = 0;
        ovdataL.data.memory_id = mem_info.mem_fd;
        ret = ioctl(fd, MSMFB_OVERLAY_PLAY, &ovdataL);
        if (ret < 0) {
            LOGE("overlay_display_frame failed, overlay play Failed");
            LOGE("%i, %i, %i, %i", ret, fb_fd, fd, errno);
            return ret;
        }
    } else {

        if (overlayL_id == MSMFB_NEW_REQUEST) {
            LOGE("display_frame failed, no overlayL");
            return -EINVAL;
        }

        memcpy(mem_info.mem_buf, data, size);

        memset(&ovdataL, 0, sizeof(struct msmfb_overlay_data));

        ovdataL.id = overlayL_id;
        ovdataL.data.flags = 0;
        ovdataL.data.offset = 0;
        ovdataL.data.memory_id = mem_info.mem_fd;
        ret = ioctl(fd, MSMFB_OVERLAY_PLAY, &ovdataL);
        if (ret < 0) {
            LOGE("overlay_display_frame failed, overlayL play Failed");
            return ret;
        }

        if (overlayR_id == MSMFB_NEW_REQUEST) {
            LOGE("display_frame failed, no overlayR");
            return -EINVAL;
        }
        memset(&ovdataR, 0, sizeof(struct msmfb_overlay_data));

        ovdataR.id = overlayR_id;
        ovdataR.data.flags = 0;
        ovdataR.data.offset = 0;
        ovdataR.data.memory_id = mem_info.mem_fd;
        ret = ioctl(fd, MSMFB_OVERLAY_PLAY, &ovdataR);
        if (ret < 0) {
            LOGE("overlay_display_frame failed, overlayR play Failed");
            return ret;
        }
    }
    memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
    ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
    ext_commit.wait_for_finish = 1;
    ret = ioctl(fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
    if (ret < 0) {
        LOGE("overlay_display_frame failed, overlay commit Failed!");
    }

    return ret;
}

int free_overlay(int fd)
{
    int ret = 0;
    struct mdp_display_commit ext_commit;

    if (!isDisplaySplit()) {
        if (overlayL_id != MSMFB_NEW_REQUEST) {
            ret = ioctl(fd, MSMFB_OVERLAY_UNSET, &overlayL_id);
            if (ret) {
                LOGE("Overlay Unset Failed");
                overlayL_id = MSMFB_NEW_REQUEST;
                return ret;
            }
        }
    } else {

        if (overlayL_id != MSMFB_NEW_REQUEST) {
            ret = ioctl(fd, MSMFB_OVERLAY_UNSET, &overlayL_id);
            if (ret) {
                LOGE("OverlayL Unset Failed");
            }
        }

        if (overlayR_id != MSMFB_NEW_REQUEST) {
            ret = ioctl(fd, MSMFB_OVERLAY_UNSET, &overlayR_id);
            if (ret) {
                LOGE("OverlayR Unset Failed");
                overlayR_id = MSMFB_NEW_REQUEST;
                return ret;
            }
        }
    }
    memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
    ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
    ext_commit.wait_for_finish = 1;
    ret = ioctl(fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
    if (ret < 0) {
        LOGE("ERROR: Clear MSMFB_DISPLAY_COMMIT failed!");
        overlayL_id = MSMFB_NEW_REQUEST;
        overlayR_id = MSMFB_NEW_REQUEST;
        return ret;
    }
    overlayL_id = MSMFB_NEW_REQUEST;
    overlayR_id = MSMFB_NEW_REQUEST;

    return 0;
}

int overlay_flush(LIBAROMA_FBP me) {
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
    // Copy from the in-memory surface to the framebuffer.
    overlay_display_frame(fb_fd, gr_draw->data, frame_size);

    mi->buffer = gr_draw->data;

    return 1;
}

int overlay_start_post(LIBAROMA_FBP me) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    libaroma_mutex_lock(mi->mutex);
    return 1;
}

int overlay_end_post(LIBAROMA_FBP me) {
    if (me == NULL) {
        return 0;
    }
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;
    overlay_flush(me);
    libaroma_mutex_unlock(mi->mutex);
    return 1;
}

int overlay_post_32bit(
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

void overlay_init_32bit(LIBAROMA_FBP me) {
    if (me == NULL) {
        return;
    }
    LOGI("qcom overlay init 32bit colorspace");
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;

    /* calculate stride size */
    mi->stride = mi->line - (me->w * mi->pixsz);

    /* gralloc framebuffer subpixel position style */
    if (vi.transp.offset) {
        libaroma_fb_changecolorspace(me,16,8,0);
    } else {
        libaroma_fb_changecolorspace(me,0,8,16);
    }

    /* set qcom overlay sync callbacks */
    me->start_post = &overlay_start_post;
    me->end_post = &overlay_end_post;
    me->post = &overlay_post_32bit;
    me->snapshoot = NULL;
}

int overlay_post_16bit(
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

void overlay_init_16bit(LIBAROMA_FBP me) {
    if (me == NULL) {
        return;
    }
    LOGI("qcom overlay init 16bit colorspace");
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;

    /* fix not standard 16bit framebuffer */
    if (mi->line / 4 == me->w) {
        mi->line = mi->line / 2;
    }

    /* calculate stride size */
    mi->stride = mi->line - (me->w * 2);

    /* set qcom overlay sync callbacks */
    me->start_post = &overlay_start_post;
    me->end_post = &overlay_end_post;
    me->post = &overlay_post_16bit;
    me->snapshoot = NULL;
}

void overlay_dump(LINUXFBDR_INTERNALP mi) {
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

void overlay_release(LIBAROMA_FBP me) {
    if (me == NULL) {
        return;
    }

    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) me->internal;

    if (mi == NULL) {
        return;
    }

    free_overlay(fb_fd);
    free_ion_mem();

    close(fb_fd);
    fb_fd = -1;

    gr_draw = NULL;
    mi->buffer = NULL;

    /* destroy mutex & cond */
    libaroma_mutex_free(mi->mutex);

    /* free overlay internal data */
    LOGI("overlay free internal data");
    free(me->internal);
}

int overlay_init(LIBAROMA_FBP me) {
    if (me == NULL) {
        return 0;
    }

    LOGS("aroma initialized qcom overlay internal data");

    /* allocating internal data */
    LINUXFBDR_INTERNALP mi = (LINUXFBDR_INTERNALP) calloc(sizeof(LINUXFBDR_INTERNAL),1);
    if (!mi) {
        LOGE("allocating qcom overlay internal data - memory error");
        return 0;
    }

    /* set internal address */
    me->internal = (void *) mi;

    /* set release callback */
    me->release = &overlay_release;
  
    /* init mutex & cond */
    libaroma_mutex_init(mi->mutex);

    int fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd == -1) {
        LOGE("cannot open fb0");
        goto error;
    }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        LOGE("failed to get fb0 info");
        close(fd);
        goto error;
    }

    if (!target_has_overlay(fi.id)) {
        close(fd);
        goto error;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        LOGE("failed to get fb0 info");
        close(fd);
        goto error;
    }

    /* dump display info */
    overlay_dump(mi);

    /* set libaroma framebuffer instance values */
    me->w = vi.xres;			/* width */
    me->h = vi.yres;			/* height */
    me->sz = me->w * me->h;		/* width x height */

    /* set internal values */
    mi->line = fi.line_length;		/* line memory size */
    mi->depth = vi.bits_per_pixel;	/* color depth */
    mi->pixsz = mi->depth >> 3;		/* pixel size per byte */
    mi->fb_sz = (vi.xres_virtual * vi.yres_virtual * mi->pixsz);

    gr_framebuffer.width = vi.xres;
    gr_framebuffer.height = vi.yres;
    gr_framebuffer.row_bytes = fi.line_length;
    gr_framebuffer.pixel_bytes = vi.bits_per_pixel / 8;
    //gr_framebuffer.data = reinterpret_cast<uint8_t*>(bits);
    if (vi.bits_per_pixel == 16) {
        LOGI("setting GGL_PIXEL_FORMAT_RGB_565");
        gr_framebuffer.format = GGL_PIXEL_FORMAT_RGB_565;
    } else if (vi.red.offset == 8 || vi.red.offset == 16) {
        LOGI("setting GGL_PIXEL_FORMAT_BGRA_8888");
        gr_framebuffer.format = GGL_PIXEL_FORMAT_BGRA_8888;
    } else if (vi.red.offset == 0) {
        LOGI("setting GGL_PIXEL_FORMAT_RGBA_8888");
        gr_framebuffer.format = GGL_PIXEL_FORMAT_RGBA_8888;
    } else if (vi.red.offset == 24) {
        LOGI("setting GGL_PIXEL_FORMAT_RGBX_8888");
        gr_framebuffer.format = GGL_PIXEL_FORMAT_RGBX_8888;
    } else {
        if (vi.red.length == 8) {
            LOGI("No valid pixel format detected, trying GGL_PIXEL_FORMAT_RGBX_8888");
            gr_framebuffer.format = GGL_PIXEL_FORMAT_RGBX_8888;
        } else {
            LOGI("No valid pixel format detected, trying GGL_PIXEL_FORMAT_RGB_565");
            gr_framebuffer.format = GGL_PIXEL_FORMAT_RGB_565;
        }
    }

    frame_size = fi.line_length * vi.yres;

    gr_framebuffer.data = (uint8_t*)calloc(frame_size, 1);
    if (gr_framebuffer.data == NULL) {
        LOGE("failed to calloc framebuffer");
        close(fd);
        goto error;
    }

    gr_draw = &gr_framebuffer;
    fb_fd = fd;

    LOGI("framebuffer: %d (%d x %d)", fb_fd, gr_draw->width, gr_draw->height);

    if (!alloc_ion_mem(frame_size))
        allocate_overlay(fb_fd, gr_framebuffer);

    /* swap buffer now */
    overlay_flush(me);

    if (mi->pixsz == 2) {
        /* init colorspace */
        overlay_init_16bit(me);
    } else {
        /* init colorspace */
        overlay_init_32bit(me);
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
