#include "decomp/render/hud.h"

#include "gx/gl_api.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extras/font.h"

#define HUD_MAX_VERTS 65536

static GLuint hud_program;
static GLuint hud_vao;
static GLuint hud_vbo;
static GLsizeiptr hud_vbo_size;
static GLint hud_size_loc;
static GLint hud_color_loc;
static float* hud_verts;
static size_t hud_vert_count;
static float hud_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static float hud_width = 640.0f;
static float hud_height = 480.0f;

static const char* HUD_VS =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location=0) in vec2 a_pos;\n"
    "uniform vec2 u_size;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos.x / u_size.x * 2.0 - 1.0,\n"
    "                       1.0 - a_pos.y / u_size.y * 2.0, 0.0, 1.0);\n"
    "}\n";

static const char* HUD_FS =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform vec4 u_color;\n"
    "out vec4 frag_color;\n"
    "void main() { frag_color = u_color; }\n";

static GLuint hud_compile(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "hud: compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

int hud_init(char* error, size_t error_size)
{
    GLuint vs = hud_compile(GL_VERTEX_SHADER, HUD_VS);
    GLuint fs = hud_compile(GL_FRAGMENT_SHADER, HUD_FS);
    GLint ok = 0;

    if (vs == 0 || fs == 0) {
        snprintf(error, error_size, "hud shader compile failed");
        return 0;
    }
    hud_program = glCreateProgram();
    glAttachShader(hud_program, vs);
    glAttachShader(hud_program, fs);
    glLinkProgram(hud_program);
    glGetProgramiv(hud_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(hud_program, sizeof(log), NULL, log);
        snprintf(error, error_size, "hud link failed: %.200s", log);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    hud_size_loc = glGetUniformLocation(hud_program, "u_size");
    hud_color_loc = glGetUniformLocation(hud_program, "u_color");

    hud_verts = (float*) calloc((size_t) HUD_MAX_VERTS * 2, sizeof(float));
    if (hud_verts == NULL) {
        snprintf(error, error_size, "hud out of memory");
        return 0;
    }
    glGenVertexArrays(1, &hud_vao);
    glBindVertexArray(hud_vao);
    glGenBuffers(1, &hud_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
    glBindVertexArray(0);
    return 1;
}

void hud_shutdown(void)
{
    free(hud_verts);
    hud_verts = NULL;
}

void hud_begin(int width, int height)
{
    hud_vert_count = 0;
    hud_width = (float) (width > 0 ? width : 1);
    hud_height = (float) (height > 0 ? height : 1);
}

static void hud_flush(void);

/*
 * Changing colour ends the current batch.
 *
 * The colour is one uniform for the whole draw, so without this every
 * hud_set_color after the first was silently discarded and everything came
 * out in the *last* colour set -- which for a full-screen backdrop meant a
 * flat rectangle over the entire frame.  The viewer's own overlay has been
 * quietly single-coloured for the same reason.
 */
void hud_set_color(float r, float g, float b, float a)
{
    if (hud_color[0] != r || hud_color[1] != g || hud_color[2] != b ||
        hud_color[3] != a)
    {
        hud_flush();
    }
    hud_color[0] = r;
    hud_color[1] = g;
    hud_color[2] = b;
    hud_color[3] = a;
}

static void hud_quad(float x, float y, float w, float h)
{
    float* v;
    if (hud_verts == NULL || hud_vert_count + 6 > HUD_MAX_VERTS) {
        return;
    }
    v = &hud_verts[hud_vert_count * 2];
    v[0] = x;
    v[1] = y;
    v[2] = x + w;
    v[3] = y;
    v[4] = x;
    v[5] = y + h;
    v[6] = x + w;
    v[7] = y;
    v[8] = x + w;
    v[9] = y + h;
    v[10] = x;
    v[11] = y + h;
    hud_vert_count += 6;
}

/* font.h calls this for every lit glyph pixel. */
void font_rect(float x, float y, float w, float h)
{
    hud_quad(x, y, w, h);
}

void hud_rect(float x, float y, float w, float h)
{
    hud_quad(x, y, w, h);
}

void hud_text(float x, float y, float scale, const char* text)
{
    font_draw(x, y, scale, text);
}

void hud_printf(float x, float y, float scale, const char* fmt, ...)
{
    char line[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    hud_text(x, y, scale, line);
}

static void hud_flush(void)
{
    GLsizeiptr bytes;
    if (hud_verts == NULL || hud_vert_count == 0) {
        return;
    }
    bytes = (GLsizeiptr) (hud_vert_count * 2 * sizeof(float));
    glUseProgram(hud_program);
    glUniform2f(hud_size_loc, hud_width, hud_height);
    glUniform4f(hud_color_loc, hud_color[0], hud_color[1], hud_color[2],
                hud_color[3]);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(hud_vao);
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo);
    if (bytes > hud_vbo_size) {
        glBufferData(GL_ARRAY_BUFFER, bytes, hud_verts, GL_STREAM_DRAW);
        hud_vbo_size = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, hud_verts);
    }
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei) hud_vert_count);
    glBindVertexArray(0);
    hud_vert_count = 0;
}

void hud_end(void) { hud_flush(); }
