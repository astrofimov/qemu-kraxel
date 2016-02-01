/*
 * QEMU graphical console -- opengl helper bits
 *
 * Copyright (c) 2014 Red Hat
 *
 * Authors:
 *    Gerd Hoffmann <kraxel@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "ui/console.h"
#include "ui/shader.h"

#include "shader/texture-blit-vert.h"
#include "shader/texture-blit-frag.h"

struct ConsoleGLState {
    GLint texture_blit_prog;
    GLint texture_blit_vao;
};

/* ---------------------------------------------------------------------- */

ConsoleGLState *console_gl_init_context(void)
{
    ConsoleGLState *gls = g_new0(ConsoleGLState, 1);

    gls->texture_blit_prog = qemu_gl_create_compile_link_program
        (texture_blit_vert_src, texture_blit_frag_src);
    if (!gls->texture_blit_prog) {
        exit(1);
    }

    gls->texture_blit_vao =
        qemu_gl_init_texture_blit(gls->texture_blit_prog);

    return gls;
}

void console_gl_fini_context(ConsoleGLState *gls)
{
    if (!gls) {
        return;
    }
    g_free(gls);
}

bool console_gl_check_format(DisplayChangeListener *dcl,
                             pixman_format_code_t format)
{
    switch (format) {
    case PIXMAN_BE_b8g8r8x8:
    case PIXMAN_BE_b8g8r8a8:
    case PIXMAN_r5g6b5:
        return true;
    default:
        return false;
    }
}

void surface_gl_create_texture(ConsoleGLState *gls,
                               DisplaySurface *surface)
{
    assert(gls);
    assert(surface_stride(surface) % surface_bytes_per_pixel(surface) == 0);

    switch (surface->format) {
    case PIXMAN_BE_b8g8r8x8:
    case PIXMAN_BE_b8g8r8a8:
        surface->glformat = GL_BGRA_EXT;
        surface->gltype = GL_UNSIGNED_BYTE;
        break;
    case PIXMAN_BE_x8r8g8b8:
    case PIXMAN_BE_a8r8g8b8:
        surface->glformat = GL_RGBA;
        surface->gltype = GL_UNSIGNED_BYTE;
        break;
    case PIXMAN_r5g6b5:
        surface->glformat = GL_RGB;
        surface->gltype = GL_UNSIGNED_SHORT_5_6_5;
        break;
    default:
        g_assert_not_reached();
    }

    glGenTextures(1, &surface->texture);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, surface->texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT,
                  surface_stride(surface) / surface_bytes_per_pixel(surface));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                 surface_width(surface),
                 surface_height(surface),
                 0, surface->glformat, surface->gltype,
                 surface_data(surface));

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
}

void surface_gl_update_texture(ConsoleGLState *gls,
                               DisplaySurface *surface,
                               int x, int y, int w, int h)
{
    uint8_t *data = (void *)surface_data(surface);

    assert(gls);

    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT,
                  surface_stride(surface) / surface_bytes_per_pixel(surface));
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    x, y, w, h,
                    surface->glformat, surface->gltype,
                    data + surface_stride(surface) * y
                    + surface_bytes_per_pixel(surface) * x);
}

void surface_gl_render_texture(ConsoleGLState *gls,
                               DisplaySurface *surface)
{
    assert(gls);

    glClearColor(0.1f, 0.1f, 0.1f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    qemu_gl_run_texture_blit(gls->texture_blit_prog,
                             gls->texture_blit_vao);
}

void surface_gl_destroy_texture(ConsoleGLState *gls,
                                DisplaySurface *surface)
{
    if (!surface || !surface->texture) {
        return;
    }
    glDeleteTextures(1, &surface->texture);
    surface->texture = 0;
}

void surface_gl_setup_viewport(ConsoleGLState *gls,
                               DisplaySurface *surface,
                               int ww, int wh)
{
    int gw, gh, stripe;
    float sw, sh;

    assert(gls);

    gw = surface_width(surface);
    gh = surface_height(surface);

    sw = (float)ww/gw;
    sh = (float)wh/gh;
    if (sw < sh) {
        stripe = wh - wh*sw/sh;
        glViewport(0, stripe / 2, ww, wh - stripe);
    } else {
        stripe = ww - ww*sh/sw;
        glViewport(stripe / 2, 0, ww - stripe, wh);
    }
}

void glo_readpixels(GLenum gl_format, GLenum gl_type,
                    unsigned int bytes_per_pixel, unsigned int stride,
                    unsigned int width, unsigned int height, void *data)
{
    /* TODO: weird strides */
    assert(stride % bytes_per_pixel == 0);

    /* Save guest processes GL state before we ReadPixels() */
    int rl, pa;
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rl);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pa);
    glPixelStorei(GL_PACK_ROW_LENGTH, stride / bytes_per_pixel);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

#ifdef GETCONTENTS_INDIVIDUAL
    GLubyte *b = (GLubyte *) data;
    int irow;

    for (irow = height - 1; irow >= 0; irow--) {
        glReadPixels(0, irow, width, 1, gl_format, gl_type, b);
        b += stride;
    }
#else
    /* Faster buffer flip */
    GLubyte *b = (GLubyte *) data;
    GLubyte *c = &((GLubyte *) data)[stride * (height - 1)];
    GLubyte *tmp = (GLubyte *) g_malloc(width * bytes_per_pixel);
    int irow;

    glReadPixels(0, 0, width, height, gl_format, gl_type, data);

    for (irow = 0; irow < height / 2; irow++) {
        memcpy(tmp, b, width * bytes_per_pixel);
        memcpy(b, c, width * bytes_per_pixel);
        memcpy(c, tmp, width * bytes_per_pixel);
        b += stride;
        c -= stride;
    }
    g_free(tmp);
#endif

    /* Restore GL state */
    glPixelStorei(GL_PACK_ROW_LENGTH, rl);
    glPixelStorei(GL_PACK_ALIGNMENT, pa);
}
