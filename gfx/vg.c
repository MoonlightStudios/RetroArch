/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2012 - Hans-Kristian Arntzen
 *  Copyright (C) 2012 - Michael Lelli
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include <VG/openvg.h>
#include <VG/vgu.h>
#include <EGL/egl.h>
#include "gfx_context.h"
#include "../libretro.h"
#include "../general.h"
#include "../driver.h"

#ifdef HAVE_FREETYPE
#include "fonts/fonts.h"
#include "../file.h"
#endif

typedef struct
{
   uint32_t mScreenWidth;
   uint32_t mScreenHeight;
   float mScreenAspect;
   bool mKeepAspect;
   unsigned mTextureWidth;
   unsigned mTextureHeight;
   unsigned mRenderWidth;
   unsigned mRenderHeight;
   unsigned x1, y1, x2, y2;
   unsigned frame_count;
   VGImageFormat mTexType;
   VGImage mImage;
   VGfloat mTransformMatrix[9];
   VGint scissor[4];

#ifdef HAVE_FREETYPE
   char *mLastMsg;
   uint32_t mFontHeight;
   VGFont mFont;
   font_renderer_t *mFontRenderer;
   bool mFontsOn;
   VGuint mMsgLength;
   VGuint mGlyphIndices[1024];
   VGPaint mPaintFg;
   VGPaint mPaintBg;
#endif
} vg_t;

static void vg_set_nonblock_state(void *data, bool state)
{
   (void)data;
   gfx_ctx_set_swap_interval(state ? 0 : 1, true);
}

static void *vg_init(const video_info_t *video, const input_driver_t **input, void **input_data)
{
   vg_t *vg = (vg_t*)calloc(1, sizeof(vg_t));
   if (!vg)
      return NULL;

   if (!eglBindAPI(EGL_OPENVG_API))
      return NULL;

   if (!gfx_ctx_init())
   {
      free(vg);
      return NULL;
   }

   gfx_ctx_get_video_size(&vg->mScreenWidth, &vg->mScreenHeight);
   RARCH_LOG("Detecting screen resolution %ux%u.\n", vg->mScreenWidth, vg->mScreenHeight);

   gfx_ctx_set_swap_interval(video->vsync ? 1 : 0, false);

   vg->mTexType = video->rgb32 ? VG_sABGR_8888 : VG_sARGB_1555;
   vg->mKeepAspect = video->force_aspect;

   // check for SD televisions: they should always be 4:3
   if ((vg->mScreenWidth == 640 || vg->mScreenWidth == 720) && (vg->mScreenHeight == 480 || vg->mScreenHeight == 576))
      vg->mScreenAspect = 4.0f / 3.0f;
   else
      vg->mScreenAspect = (float)vg->mScreenWidth / vg->mScreenHeight;

   VGfloat clearColor[4] = {0, 0, 0, 1};
   vgSetfv(VG_CLEAR_COLOR, 4, clearColor);

   vg->mTextureWidth = vg->mTextureHeight = video->input_scale * RARCH_SCALE_BASE;
   // We can't use the native format because there's no sXRGB_1555 type and
   // emulation cores can send 0 in the top bit. We lose some speed on
   // conversion but I doubt it has any real affect, since we are only drawing
   // one image at the end of the day. Still keep the alpha channel for ABGR.
   vg->mImage = vgCreateImage(video->rgb32 ? VG_sABGR_8888 : VG_sXBGR_8888,
         vg->mTextureWidth, vg->mTextureHeight,
         video->smooth ? VG_IMAGE_QUALITY_BETTER : VG_IMAGE_QUALITY_NONANTIALIASED);
   vg_set_nonblock_state(vg, !video->vsync);

   gfx_ctx_input_driver(input, input_data);

#ifdef HAVE_FREETYPE
   if (g_settings.video.font_enable)
   {
      vg->mFont = vgCreateFont(0);
      vg->mFontHeight = g_settings.video.font_size * (g_settings.video.font_scale ? (float) vg->mScreenWidth / 1280.0f : 1.0f);

      const char *path = g_settings.video.font_path;
      if (!*path || !path_file_exists(path))
         path = font_renderer_get_default_font();

      vg->mFontRenderer = font_renderer_new(path, vg->mFontHeight);

      if (vg->mFont != VG_INVALID_HANDLE && vg->mFontRenderer)
      {
         vg->mFontsOn = true;

         vg->mPaintFg = vgCreatePaint();
         vg->mPaintBg = vgCreatePaint();
         VGfloat paintFg[] = { g_settings.video.msg_color_r, g_settings.video.msg_color_g, g_settings.video.msg_color_b, 1.0f };
         VGfloat paintBg[] = { g_settings.video.msg_color_r / 2.0f, g_settings.video.msg_color_g / 2.0f, g_settings.video.msg_color_b / 2.0f, 0.5f };

         vgSetParameteri(vg->mPaintFg, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
         vgSetParameterfv(vg->mPaintFg, VG_PAINT_COLOR, 4, paintFg);

         vgSetParameteri(vg->mPaintBg, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
         vgSetParameterfv(vg->mPaintBg, VG_PAINT_COLOR, 4, paintBg);
      }
   }
#endif

   return vg;
}

static void vg_free(void *data)
{
   vg_t *vg = (vg_t*)data;

   vgDestroyImage(vg->mImage);

#ifdef HAVE_FREETYPE
   if (vg->mFontsOn)
   {
      vgDestroyFont(vg->mFont);
      font_renderer_free(vg->mFontRenderer);
      vgDestroyPaint(vg->mPaintFg);
      vgDestroyPaint(vg->mPaintBg);
   }
#endif

   gfx_ctx_destroy();

   free(vg);
}

#ifdef HAVE_FREETYPE

static void vg_render_message(vg_t *vg, const char *msg)
{
   free(vg->mLastMsg);
   vg->mLastMsg = strdup(msg);

   if (vg->mMsgLength)
   {
      while (--vg->mMsgLength)
         vgClearGlyph(vg->mFont, vg->mMsgLength);

      vgClearGlyph(vg->mFont, 0);
   }

   struct font_output_list out;
   font_renderer_msg(vg->mFontRenderer, msg, &out);
   struct font_output *head = out.head;

   while (head)
   {
      if (vg->mMsgLength >= 1024)
         break;

      VGfloat origin[2], escapement[2];
      VGImage img;

      escapement[0] = head->advance_x;
      escapement[1] = head->advance_y;
      origin[0] = -head->char_off_x;
      origin[1] = -head->char_off_y;

      img = vgCreateImage(VG_A_8, head->width, head->height, VG_IMAGE_QUALITY_NONANTIALIASED);

      // flip it
      for (unsigned i = 0; i < head->height; i++)
         vgImageSubData(img, head->output + head->pitch * i, head->pitch, VG_A_8, 0, head->height - i - 1, head->width, 1);

      vgSetGlyphToImage(vg->mFont, vg->mMsgLength, img, origin, escapement);
      vgDestroyImage(img);

      vg->mMsgLength++;
      head = head->next;
   }

   font_renderer_free_output(&out);

   for (unsigned i = 0; i < vg->mMsgLength; i++)
      vg->mGlyphIndices[i] = i;
}

static void vg_draw_message(vg_t *vg, const char *msg)
{
   if (!vg->mLastMsg || strcmp(vg->mLastMsg, msg))
      vg_render_message(vg, msg);

   vgSeti(VG_SCISSORING, VG_FALSE);
   vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_STENCIL);

   VGfloat origins[] = {
      vg->mScreenWidth * g_settings.video.msg_pos_x - 2.0f,
      vg->mScreenHeight * g_settings.video.msg_pos_y - 2.0f,
   };

   vgSetfv(VG_GLYPH_ORIGIN, 2, origins);
   vgSetPaint(vg->mPaintBg, VG_FILL_PATH);
   vgDrawGlyphs(vg->mFont, vg->mMsgLength, vg->mGlyphIndices, NULL, NULL, VG_FILL_PATH, VG_TRUE);
   origins[0] += 2.0f;
   origins[1] += 2.0f;
   vgSetfv(VG_GLYPH_ORIGIN, 2, origins);
   vgSetPaint(vg->mPaintFg, VG_FILL_PATH);
   vgDrawGlyphs(vg->mFont, vg->mMsgLength, vg->mGlyphIndices, NULL, NULL, VG_FILL_PATH, VG_TRUE);

   vgSeti(VG_SCISSORING, VG_TRUE);
   vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
}

#endif

static void vg_calculate_quad(vg_t *vg)
{
   // set viewport for aspect ratio, taken from the OpenGL driver
   if (vg->mKeepAspect)
   {
      float desired_aspect = g_settings.video.aspect_ratio;

      // If the aspect ratios of screen and desired aspect ratio are sufficiently equal (floating point stuff),
      // assume they are actually equal.
      if (fabs(vg->mScreenAspect - desired_aspect) < 0.0001)
      {
         vg->x1 = 0;
         vg->y1 = 0;
         vg->x2 = vg->mScreenWidth;
         vg->y2 = vg->mScreenHeight;
      }
      else if (vg->mScreenAspect > desired_aspect)
      {
         float delta = (desired_aspect / vg->mScreenAspect - 1.0) / 2.0 + 0.5;
         vg->x1 = vg->mScreenWidth * (0.5 - delta);
         vg->y1 = 0;
         vg->x2 = 2.0 * vg->mScreenWidth * delta + vg->x1;
         vg->y2 = vg->mScreenHeight + vg->y1;
      }
      else
      {
         float delta = (vg->mScreenAspect / desired_aspect - 1.0) / 2.0 + 0.5;
         vg->x1 = 0;
         vg->y1 = vg->mScreenHeight * (0.5 - delta);
         vg->x2 = vg->mScreenWidth + vg->x1;
         vg->y2 = 2.0 * vg->mScreenHeight * delta + vg->y1;
      }
   }
   else
   {
      vg->x1 = 0;
      vg->y1 = 0;
      vg->x2 = vg->mScreenWidth;
      vg->y2 = vg->mScreenHeight;
   }

   vg->scissor[0] = vg->x1;
   vg->scissor[1] = vg->y1;
   vg->scissor[2] = vg->x2 - vg->x1;
   vg->scissor[3] = vg->y2 - vg->y1;

   vgSetiv(VG_SCISSOR_RECTS, 4, vg->scissor);
}

static bool vg_frame(void *data, const void *frame, unsigned width, unsigned height, unsigned pitch, const char *msg)
{
   vg_t *vg = (vg_t*)data;
   vg->frame_count++;

   if (width != vg->mRenderWidth || height != vg->mRenderHeight)
   {
      vg->mRenderWidth = width;
      vg->mRenderHeight = height;
      vg_calculate_quad(vg);
      vguComputeWarpQuadToQuad(
         vg->x1, vg->y1, vg->x2, vg->y1, vg->x2, vg->y2, vg->x1, vg->y2,
         // needs to be flipped, Khronos loves their bottom-left origin
         0, height, width, height, width, 0, 0, 0,
         vg->mTransformMatrix);
      vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
      vgLoadMatrix(vg->mTransformMatrix);
   }
   vgSeti(VG_SCISSORING, VG_FALSE);
   vgClear(0, 0, vg->mScreenWidth, vg->mScreenHeight);
   vgSeti(VG_SCISSORING, VG_TRUE);

   vgImageSubData(vg->mImage, frame, pitch, vg->mTexType, 0, 0, width, height);
   vgDrawImage(vg->mImage);

#ifdef HAVE_FREETYPE
   if (msg && vg->mFontsOn)
      vg_draw_message(vg, msg);
#else
   (void)msg;
#endif

   gfx_ctx_swap_buffers();

   return true;
}

static bool vg_alive(void *data)
{
   vg_t *vg = (vg_t*)data;
   bool quit, resize;

   gfx_ctx_check_window(&quit,
         &resize, &vg->mScreenWidth, &vg->mScreenHeight,
         vg->frame_count);
   return !quit;
}

static bool vg_focus(void *data)
{
   (void)data;
   return gfx_ctx_window_has_focus();
}

const video_driver_t video_vg = {
   vg_init,
   vg_frame,
   vg_set_nonblock_state,
   vg_alive,
   vg_focus,
   NULL,
   vg_free,
   "vg"
};