#include "gx/shader.h"
#include "gx/gl.h"
#include <stdio.h>
GLuint gx_compile_shader(GLenum type,const char *body,const char *body2)
{
    const char *sources[3]={GLSL_HEADER,body,body2};
    char log[2048];
    GLint ok=0;
    GLuint s=glCreateShader(type);
    glShaderSource(s,body2?3:2,sources,NULL);
    glCompileShader(s);
    glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok) {
        glGetShaderInfoLog(s,sizeof(log),NULL,log);
        fprintf(stderr,"Shader compile failed: %s\n",log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint gx_link_program(GLuint vs,GLuint fs,const char *name)
{
    char log[2048];
    GLint ok=0;
    GLuint p=glCreateProgram();
    glAttachShader(p,vs);
    glAttachShader(p,fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) {
        glGetProgramInfoLog(p,sizeof(log),NULL,log);
        fprintf(stderr,"%s link failed: %s\n",name,log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

