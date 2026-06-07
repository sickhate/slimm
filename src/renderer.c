#define _GNU_SOURCE
#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gbm.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

struct slim_renderer {
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface;
    struct gbm_surface *gbm_surface;

    int width, height;

    GLuint tex_prog;
    GLuint rect_prog;
    GLuint rounded_prog;
    GLuint glyph_prog;

    GLint tex_proj, tex_tex, tex_tint;
    GLint rect_proj, rect_color;
    GLint rounded_proj, rounded_rect, rounded_radius, rounded_color;
    GLint glyph_proj, glyph_atlas, glyph_color, glyph_uv;

    GLuint vao;
    GLuint vbo;
};

static const char *tex_vs =
    "#version 300 es\n"
    "in vec2 a_pos;\n"
    "in vec2 a_uv;\n"
    "uniform mat4 u_proj;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
    "  v_uv = a_uv;\n"
    "}\n";

static const char *tex_fs =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_tint;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "  vec4 c = texture(u_tex, v_uv);\n"
    "  frag = c * u_tint;\n"
    "}\n";

static const char *rect_vs =
    "#version 300 es\n"
    "in vec2 a_pos;\n"
    "uniform mat4 u_proj;\n"
    "void main() {\n"
    "  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *rect_fs =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "  frag = u_color;\n"
    "}\n";

static const char *rounded_vs =
    "#version 300 es\n"
    "in vec2 a_pos;\n"
    "uniform mat4 u_proj;\n"
    "out vec2 v_pos;\n"
    "void main() {\n"
    "  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
    "  v_pos = a_pos;\n"
    "}\n";

static const char *rounded_fs =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform vec4 u_rect;\n"
    "uniform float u_radius;\n"
    "uniform vec4 u_color;\n"
    "in vec2 v_pos;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "  vec2 h = u_rect.zw * 0.5;\n"
    "  vec2 center = u_rect.xy + h;\n"
    "  vec2 p = abs(v_pos - center) - h + u_radius;\n"
    "  float dist = length(max(p, 0.0)) - u_radius;\n"
    "  float alpha = 1.0 - smoothstep(-1.0, 1.0, dist);\n"
    "  if (alpha < 0.01) discard;\n"
    "  frag = u_color * vec4(1.0, 1.0, 1.0, alpha);\n"
    "}\n";

static const char *glyph_vs =
    "#version 300 es\n"
    "in vec2 a_pos;\n"
    "in vec2 a_uv;\n"
    "uniform mat4 u_proj;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
    "  v_uv = a_uv;\n"
    "}\n";

static const char *glyph_fs =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_atlas;\n"
    "uniform vec4 u_color;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "  float a = texture(u_atlas, v_uv).r;\n"
    "  if (a < 0.01) discard;\n"
    "  frag = vec4(u_color.rgb, u_color.a * a);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "program link error: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static struct slim_renderer *renderer_alloc(int width, int height)
{
    struct slim_renderer *r = calloc(1, sizeof(struct slim_renderer));
    r->width = width;
    r->height = height;
    return r;
}

static int renderer_init_gl(struct slim_renderer *r)
{
    GLuint vs, fs;

    vs = compile_shader(GL_VERTEX_SHADER, tex_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, tex_fs);
    r->tex_prog = link_program(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);
    if (!r->tex_prog) return -1;
    r->tex_proj = glGetUniformLocation(r->tex_prog, "u_proj");
    r->tex_tex = glGetUniformLocation(r->tex_prog, "u_tex");
    r->tex_tint = glGetUniformLocation(r->tex_prog, "u_tint");

    vs = compile_shader(GL_VERTEX_SHADER, rect_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, rect_fs);
    r->rect_prog = link_program(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);
    if (!r->rect_prog) return -1;
    r->rect_proj = glGetUniformLocation(r->rect_prog, "u_proj");
    r->rect_color = glGetUniformLocation(r->rect_prog, "u_color");

    GLuint rnd_vs = compile_shader(GL_VERTEX_SHADER, rounded_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, rounded_fs);
    r->rounded_prog = link_program(rnd_vs, fs);
    glDeleteShader(rnd_vs); glDeleteShader(fs);
    if (!r->rounded_prog) return -1;
    r->rounded_proj = glGetUniformLocation(r->rounded_prog, "u_proj");
    r->rounded_rect = glGetUniformLocation(r->rounded_prog, "u_rect");
    r->rounded_radius = glGetUniformLocation(r->rounded_prog, "u_radius");
    r->rounded_color = glGetUniformLocation(r->rounded_prog, "u_color");

    vs = compile_shader(GL_VERTEX_SHADER, glyph_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, glyph_fs);
    r->glyph_prog = link_program(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);
    if (!r->glyph_prog) return -1;
    r->glyph_proj = glGetUniformLocation(r->glyph_prog, "u_proj");
    r->glyph_atlas = glGetUniformLocation(r->glyph_prog, "u_atlas");
    r->glyph_color = glGetUniformLocation(r->glyph_prog, "u_color");
    r->glyph_uv = glGetUniformLocation(r->glyph_prog, "u_uv");

    glGenVertexArrays(1, &r->vao);
    glGenBuffers(1, &r->vbo);
    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, r->width, r->height);

    return 0;
}

struct slim_renderer *renderer_create_wl(struct wl_display *wl_disp,
                                         struct wl_egl_window *win,
                                         int width, int height)
{
    struct slim_renderer *r = renderer_alloc(width, height);
    r->gbm_surface = NULL;

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform =
        (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");

    r->display = EGL_NO_DISPLAY;
    if (get_platform)
        r->display = get_platform(EGL_PLATFORM_WAYLAND_EXT, wl_disp, NULL);
    if (r->display == EGL_NO_DISPLAY)
        r->display = eglGetDisplay((EGLNativeDisplayType)wl_disp);
    if (r->display == EGL_NO_DISPLAY) {
        fprintf(stderr, "renderer: eglGetDisplay (Wayland) failed\n");
        free(r); return NULL;
    }

    EGLint major, minor;
    if (!eglInitialize(r->display, &major, &minor)) {
        fprintf(stderr, "renderer: eglInitialize (Wayland) failed\n");
        free(r); return NULL;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "renderer: eglBindAPI failed\n");
        eglTerminate(r->display); free(r); return NULL;
    }

    EGLint attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };

    EGLint count = 0;
    if (!eglChooseConfig(r->display, attrs, &r->config, 1, &count) || !count) {
        fprintf(stderr, "renderer: eglChooseConfig failed\n");
        eglTerminate(r->display); free(r); return NULL;
    }

    EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };

    r->context = eglCreateContext(r->display, r->config,
                                  EGL_NO_CONTEXT, ctx_attrs);
    if (r->context == EGL_NO_CONTEXT) {
        fprintf(stderr, "renderer: eglCreateContext failed\n");
        eglTerminate(r->display); free(r); return NULL;
    }

    r->surface = eglCreateWindowSurface(r->display, r->config,
                                        win, NULL);
    if (r->surface == EGL_NO_SURFACE) {
        fprintf(stderr, "renderer: eglCreateWindowSurface (Wayland) failed\n");
        eglDestroyContext(r->display, r->context);
        eglTerminate(r->display); free(r); return NULL;
    }

    eglMakeCurrent(r->display, r->surface, r->surface, r->context);

    if (renderer_init_gl(r)) {
        fprintf(stderr, "renderer: shader init failed\n");
        eglDestroyContext(r->display, r->context);
        eglDestroySurface(r->display, r->surface);
        eglTerminate(r->display); free(r); return NULL;
    }

    return r;
}

struct slim_renderer *renderer_create(struct gbm_device *dev,
                                      struct gbm_surface *surface,
                                      int width, int height)
{
    struct slim_renderer *r = renderer_alloc(width, height);
    r->gbm_surface = surface;

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform =
        (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");

    r->display = EGL_NO_DISPLAY;
    if (get_platform)
        r->display = get_platform(EGL_PLATFORM_GBM_KHR, dev, NULL);
    if (r->display == EGL_NO_DISPLAY)
        r->display = eglGetDisplay((EGLNativeDisplayType)dev);
    if (r->display == EGL_NO_DISPLAY) {
        fprintf(stderr, "renderer: eglGetDisplay failed\n");
        free(r); return NULL;
    }

    EGLint major, minor;
    if (!eglInitialize(r->display, &major, &minor)) {
        fprintf(stderr, "renderer: eglInitialize failed\n");
        free(r); return NULL;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "renderer: eglBindAPI failed\n");
        eglTerminate(r->display); free(r); return NULL;
    }

    EGLint attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };

    EGLint count = 0;
    if (!eglChooseConfig(r->display, attrs, &r->config, 1, &count) || !count) {
        fprintf(stderr, "renderer: eglChooseConfig failed\n");
        eglTerminate(r->display); free(r); return NULL;
    }

    EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };

    r->context = eglCreateContext(r->display, r->config,
                                  EGL_NO_CONTEXT, ctx_attrs);
    if (r->context == EGL_NO_CONTEXT) {
        fprintf(stderr, "renderer: eglCreateContext failed\n");
        eglTerminate(r->display); free(r); return NULL;
    }

    r->surface = eglCreateWindowSurface(r->display, r->config,
                                        (EGLNativeWindowType)surface, NULL);
    if (r->surface == EGL_NO_SURFACE) {
        fprintf(stderr, "renderer: eglCreateWindowSurface failed\n");
        eglDestroyContext(r->display, r->context);
        eglTerminate(r->display); free(r); return NULL;
    }

    eglMakeCurrent(r->display, r->surface, r->surface, r->context);

    if (renderer_init_gl(r)) {
        fprintf(stderr, "renderer: shader init failed\n");
        eglDestroyContext(r->display, r->context);
        eglDestroySurface(r->display, r->surface);
        eglTerminate(r->display); free(r); return NULL;
    }

    return r;
}

void renderer_destroy(struct slim_renderer *r)
{
    if (!r) return;
    glDeleteProgram(r->tex_prog);
    glDeleteProgram(r->rect_prog);
    glDeleteProgram(r->rounded_prog);
    glDeleteProgram(r->glyph_prog);
    glDeleteBuffers(1, &r->vbo);
    glDeleteVertexArrays(1, &r->vao);
    eglMakeCurrent(r->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (r->surface != EGL_NO_SURFACE)
        eglDestroySurface(r->display, r->surface);
    if (r->context != EGL_NO_CONTEXT)
        eglDestroyContext(r->display, r->context);
    if (r->display != EGL_NO_DISPLAY)
        eglTerminate(r->display);
    free(r);
}

static void set_proj(struct slim_renderer *r, GLint loc)
{
    float l = 0.0f, r_ = (float)r->width;
    float t = 0.0f, b = (float)r->height;
    float mat[16] = {
        2.0f/(r_-l), 0, 0, 0,
        0, 2.0f/(t-b), 0, 0,
        0, 0, -1, 0,
        -(r_+l)/(r_-l), -(t+b)/(t-b), 0, 1,
    };
    glUniformMatrix4fv(loc, 1, GL_FALSE, mat);
}

void renderer_clear(struct slim_renderer *r, struct slim_color c)
{
    (void)r;
    glClearColor(c.r, c.g, c.b, c.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void renderer_draw_rect(struct slim_renderer *r,
                        float x, float y, float w, float h,
                        struct slim_color color)
{
    glUseProgram(r->rect_prog);
    set_proj(r, r->rect_proj);
    glUniform4f(r->rect_color, color.r, color.g, color.b, color.a);

    float verts[] = { x, y, x+w, y, x, y+h, x+w, y+h };
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void renderer_draw_rounded_rect(struct slim_renderer *r,
                                float x, float y, float w, float h,
                                float radius, struct slim_color color)
{
    glUseProgram(r->rounded_prog);
    set_proj(r, r->rounded_proj);
    glUniform4f(r->rounded_rect, x, y, w, h);
    glUniform1f(r->rounded_radius, radius);
    glUniform4f(r->rounded_color, color.r, color.g, color.b, color.a);

    float verts[] = { x, y, x+w, y, x, y+h, x+w, y+h };
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void renderer_draw_texture(struct slim_renderer *r,
                           float x, float y, float w, float h,
                           uint32_t tex_id, struct slim_color tint)
{
    glUseProgram(r->tex_prog);
    set_proj(r, r->tex_proj);
    glUniform1i(r->tex_tex, 0);
    glUniform4f(r->tex_tint, tint.r, tint.g, tint.b, tint.a);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_id);

    float verts[] = {
        x,   y,    0.0f, 0.0f,
        x+w, y,    1.0f, 0.0f,
        x,   y+h,  0.0f, 1.0f,
        x+w, y+h,  1.0f, 1.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), NULL);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float),
                          (void *)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void renderer_draw_texture_rt(struct slim_renderer *r,
                              float x, float y, float w, float h,
                              uint32_t tex_id, struct slim_color tint)
{
    glUseProgram(r->tex_prog);
    set_proj(r, r->tex_proj);
    glUniform1i(r->tex_tex, 0);
    glUniform4f(r->tex_tint, tint.r, tint.g, tint.b, tint.a);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_id);

    float verts[] = {
        x,   y,    0.0f, 1.0f,
        x+w, y,    1.0f, 1.0f,
        x,   y+h,  0.0f, 0.0f,
        x+w, y+h,  1.0f, 0.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), NULL);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float),
                          (void *)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void renderer_draw_glyph(struct slim_renderer *r,
                         float x, float y, float w, float h,
                         uint32_t atlas_tex, float u0, float v0,
                         float u1, float v1, struct slim_color color)
{
    glUseProgram(r->glyph_prog);
    set_proj(r, r->glyph_proj);
    glUniform1i(r->glyph_atlas, 0);
    glUniform4f(r->glyph_color, color.r, color.g, color.b, color.a);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_tex);

    float verts[] = {
        x,   y,    u0, v0,
        x+w, y,    u1, v0,
        x,   y+h,  u0, v1,
        x+w, y+h,  u1, v1,
    };
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), NULL);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float),
                          (void *)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int renderer_swap(struct slim_renderer *r)
{
    return eglSwapBuffers(r->display, r->surface);
}

struct gbm_bo *renderer_lock_front(struct slim_renderer *r)
{
    return gbm_surface_lock_front_buffer(r->gbm_surface);
}

void renderer_rebind(struct slim_renderer *r)
{
    eglMakeCurrent(r->display, r->surface, r->surface, r->context);
}

void renderer_set_viewport(struct slim_renderer *r, int width, int height)
{
    r->width = width;
    r->height = height;
    glViewport(0, 0, width, height);
}
