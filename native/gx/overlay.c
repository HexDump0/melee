#include "gx/overlay.h"
#include "gx/gl.h"
#include "gx/shader.h"
#include "extras/font.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const char *OVERLAY_VS =
    "layout(location=0) in vec3 a_position;\n"
    "layout(location=2) in vec4 a_color;\n"
    "uniform mat4 u_mvp;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
    "    v_color = a_color;\n"
    "}\n";

static const char *OVERLAY_FS =
    "in vec4 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() { frag_color = v_color; }\n";

/* -------------------------------------------------------------------------
 * Immediate-style overlay geometry.  Every glBegin/glVertex pair in the old
 * fixed-function path becomes ov_begin/ov_vertex/ov_end here; quads and fans
 * are converted to triangles on the CPU and the rest draw with the same
 * primitive mode.  One stream buffer is reused for all overlay draws.
 */
typedef struct OverlayVertex {
    float position[3];
    float color[4];
} OverlayVertex;

#define OVERLAY_INITIAL_VERTICES 4096

static OverlayVertex *g_ov_vertices;
static size_t g_ov_count;
static size_t g_ov_capacity;
static GLenum g_ov_mode;
static OverlayVertex g_ov_pending[4];
static size_t g_ov_pending_count;
static float g_ov_color[4]={1,1,1,1};
static GLuint g_ov_vao,g_ov_vbo;
static GLuint g_ov_program;
static GLint g_ov_mvp;

static int ov_reserve(size_t extra)
{
    size_t cap;
    OverlayVertex *p;
    if(g_ov_count+extra<=g_ov_capacity)return 1;
    cap=g_ov_capacity?g_ov_capacity*2:OVERLAY_INITIAL_VERTICES;
    while(cap<g_ov_count+extra)cap*=2;
    p=realloc(g_ov_vertices,cap*sizeof(*p));
    if(!p)return 0;
    g_ov_vertices=p;
    g_ov_capacity=cap;
    return 1;
}

static void ov_emit(const OverlayVertex *v)
{
    if(!ov_reserve(1))return;
    g_ov_vertices[g_ov_count++]=*v;
}

void ov_vertex3f(float x,float y,float z)
{
    OverlayVertex v;
    v.position[0]=x;v.position[1]=y;v.position[2]=z;
    memcpy(v.color,g_ov_color,sizeof(v.color));
    switch(g_ov_mode) {
    case GL_QUADS:
        g_ov_pending[g_ov_pending_count++]=v;
        if(g_ov_pending_count==4) {
            /* Mesa decomposes GL_QUADS as (0,1,2),(0,2,3). */
            ov_emit(&g_ov_pending[0]);ov_emit(&g_ov_pending[1]);ov_emit(&g_ov_pending[2]);
            ov_emit(&g_ov_pending[0]);ov_emit(&g_ov_pending[2]);ov_emit(&g_ov_pending[3]);
            g_ov_pending_count=0;
        }
        break;
    case GL_TRIANGLES:
        g_ov_pending[g_ov_pending_count++]=v;
        if(g_ov_pending_count==3) {
            ov_emit(&g_ov_pending[0]);ov_emit(&g_ov_pending[1]);ov_emit(&g_ov_pending[2]);
            g_ov_pending_count=0;
        }
        break;
    case GL_TRIANGLE_FAN:
        if(g_ov_pending_count==0) {
            g_ov_pending[0]=v;
            g_ov_pending_count=1;
        } else if(g_ov_pending_count==1) {
            g_ov_pending[1]=v;
            g_ov_pending_count=2;
        } else {
            ov_emit(&g_ov_pending[0]);ov_emit(&g_ov_pending[1]);ov_emit(&v);
            g_ov_pending[1]=v;
        }
        break;
    default:
        /* GL_LINES, GL_LINE_STRIP and GL_LINE_LOOP pass through. */
        ov_emit(&v);
        break;
    }
}

void ov_vertex2f(float x,float y)
{
    ov_vertex3f(x,y,0.0f);
}

void ov_begin(GLenum mode)
{
    g_ov_mode=mode;
    g_ov_pending_count=0;
}

void ov_end(void)
{
    GLenum draw_mode;
    size_t i;
    for(i=0;i<g_ov_pending_count;++i)ov_emit(&g_ov_pending[i]);
    g_ov_pending_count=0;
    if(!g_ov_count){g_ov_mode=0;return;}
    if(g_ov_mode==GL_QUADS||g_ov_mode==GL_TRIANGLE_FAN)g_ov_mode=GL_TRIANGLES;
    draw_mode=g_ov_mode;
    glBindVertexArray(g_ov_vao);
    glBindBuffer(GL_ARRAY_BUFFER,g_ov_vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(g_ov_count*sizeof(OverlayVertex)),
                 g_ov_vertices,GL_STREAM_DRAW);
    glDrawArrays(draw_mode,0,(GLsizei)g_ov_count);
    g_ov_count=0;
    g_ov_mode=0;
}

void ov_color3f(float r,float g,float b)
{
    g_ov_color[0]=r;g_ov_color[1]=g;g_ov_color[2]=b;g_ov_color[3]=1.0f;
}

void ov_color4f(float r,float g,float b,float a)
{
    g_ov_color[0]=r;g_ov_color[1]=g;g_ov_color[2]=b;g_ov_color[3]=a;
}

void ov_set_mvp(const Mat4 m)
{
    glUseProgram(g_ov_program);
    glUniformMatrix4fv(g_ov_mvp,1,GL_FALSE,m);
}

void rect(float x,float y,float w,float h)
{
    ov_begin(GL_QUADS);
    ov_vertex2f(x,y);ov_vertex2f(x+w,y);
    ov_vertex2f(x+w,y+h);ov_vertex2f(x,y+h);
    ov_end();
}

void circle(float x,float y,float z,float radius,int filled)
{
    int i;
    ov_begin(filled?GL_TRIANGLE_FAN:GL_LINE_LOOP);
    if(filled)ov_vertex3f(x,y,z);
    for(i=0;i<=48;++i) {
        float a=i*2*PI/48;
        ov_vertex3f(x+cosf(a)*radius,y+sinf(a)*radius,z);
    }
    ov_end();
}

/* font_draw.h calls this for every lit font pixel. */
void font_rect(float x,float y,float w,float h)
{
    ov_vertex2f(x,y);ov_vertex2f(x+w,y);
    ov_vertex2f(x+w,y+h);ov_vertex2f(x,y+h);
}

void draw_text(float x,float y,float size,const char *s)
{
    ov_begin(GL_QUADS);
    font_draw(x,y,size,s);
    ov_end();
}


int overlay_init(void)
{
    GLuint vs,fs;
    vs=gx_compile_shader(GL_VERTEX_SHADER,OVERLAY_VS,NULL);
    fs=gx_compile_shader(GL_FRAGMENT_SHADER,OVERLAY_FS,NULL);
    if(!vs||!fs)return 0;
    g_ov_program=gx_link_program(vs,fs,"overlay");
    if(!g_ov_program)return 0;
    g_ov_mvp=glGetUniformLocation(g_ov_program,"u_mvp");
    glGenVertexArrays(1,&g_ov_vao);
    glGenBuffers(1,&g_ov_vbo);
    glBindVertexArray(g_ov_vao);
    glBindBuffer(GL_ARRAY_BUFFER,g_ov_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(OverlayVertex),
                          (const void*)(uintptr_t)offsetof(OverlayVertex,position));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(OverlayVertex),
                          (const void*)(uintptr_t)offsetof(OverlayVertex,color));
    glBindVertexArray(0);
    return 1;
}

void overlay_shutdown(void)
{
    free(g_ov_vertices);
    g_ov_vertices = NULL;
    g_ov_capacity = 0;
    g_ov_count = 0;
}
