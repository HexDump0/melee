#ifndef MELEE_GX_GL_API_H
#define MELEE_GX_GL_API_H

/*
 * How the port reaches OpenGL ES 3.0 on each platform.
 *
 * On Linux the loader is the dynamic linker: the build links `libGLESv2` and
 * calls `glDrawArrays` directly.  **Windows has no such library.**  Anything
 * past OpenGL 1.1 has to be fetched at run time from the driver, so every
 * entry point becomes a function pointer -- which is what a `libGLESv2` to
 * link against would have been hiding.
 *
 * Rather than change 80 call sites, the pointers are named `melee_gl*` and a
 * macro maps the ordinary spelling onto them.  `gx_gl.c` keeps reading as GL
 * code on both platforms and there is no second code path to keep in step.
 *
 * The signatures are the Khronos header's own `PFNGL...PROC` typedefs, taken
 * from `native/third_party/khronos`, so nothing here is hand-written and
 * nothing can drift from the real prototype.  `GL_GLES_PROTOTYPES 0` asks
 * `gl3.h` for the typedefs without the prototypes, which would otherwise
 * collide with the macros below.
 *
 * Generated shape, but checked in: the list is the 80 functions the port
 * actually calls, and a new call fails to link with an obvious undefined
 * reference rather than silently doing nothing.
 */

#ifdef _WIN32
#define GL_GLES_PROTOTYPES 0
#include <GLES3/gl3.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fetches every pointer below from the current GL context.  Call once, after
 * the context is made current and before the first draw.  Returns the number
 * of entry points that could not be found: non-zero means the driver does not
 * really have ES 3.0, and the caller should say so rather than crash on the
 * first null pointer. */
int melee_gl_load(void);

extern PFNGLACTIVETEXTUREPROC melee_glActiveTexture;
extern PFNGLATTACHSHADERPROC melee_glAttachShader;
extern PFNGLBINDBUFFERPROC melee_glBindBuffer;
extern PFNGLBINDFRAMEBUFFERPROC melee_glBindFramebuffer;
extern PFNGLBINDRENDERBUFFERPROC melee_glBindRenderbuffer;
extern PFNGLBINDTEXTUREPROC melee_glBindTexture;
extern PFNGLBINDVERTEXARRAYPROC melee_glBindVertexArray;
extern PFNGLBLENDEQUATIONPROC melee_glBlendEquation;
extern PFNGLBLENDFUNCPROC melee_glBlendFunc;
extern PFNGLBLITFRAMEBUFFERPROC melee_glBlitFramebuffer;
extern PFNGLBUFFERDATAPROC melee_glBufferData;
extern PFNGLBUFFERSUBDATAPROC melee_glBufferSubData;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC melee_glCheckFramebufferStatus;
extern PFNGLCLEARPROC melee_glClear;
extern PFNGLCLEARCOLORPROC melee_glClearColor;
extern PFNGLCLEARDEPTHFPROC melee_glClearDepthf;
extern PFNGLCOLORMASKPROC melee_glColorMask;
extern PFNGLCOMPILESHADERPROC melee_glCompileShader;
extern PFNGLCOPYTEXSUBIMAGE2DPROC melee_glCopyTexSubImage2D;
extern PFNGLCREATEPROGRAMPROC melee_glCreateProgram;
extern PFNGLCREATESHADERPROC melee_glCreateShader;
extern PFNGLCULLFACEPROC melee_glCullFace;
extern PFNGLDELETEBUFFERSPROC melee_glDeleteBuffers;
extern PFNGLDELETEFRAMEBUFFERSPROC melee_glDeleteFramebuffers;
extern PFNGLDELETEPROGRAMPROC melee_glDeleteProgram;
extern PFNGLDELETESHADERPROC melee_glDeleteShader;
extern PFNGLDELETETEXTURESPROC melee_glDeleteTextures;
extern PFNGLDELETEVERTEXARRAYSPROC melee_glDeleteVertexArrays;
extern PFNGLDEPTHFUNCPROC melee_glDepthFunc;
extern PFNGLDEPTHMASKPROC melee_glDepthMask;
extern PFNGLDEPTHRANGEFPROC melee_glDepthRangef;
extern PFNGLDISABLEPROC melee_glDisable;
extern PFNGLDRAWARRAYSPROC melee_glDrawArrays;
extern PFNGLENABLEPROC melee_glEnable;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC melee_glEnableVertexAttribArray;
extern PFNGLFINISHPROC melee_glFinish;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC melee_glFramebufferRenderbuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC melee_glFramebufferTexture2D;
extern PFNGLFRONTFACEPROC melee_glFrontFace;
extern PFNGLGENBUFFERSPROC melee_glGenBuffers;
extern PFNGLGENERATEMIPMAPPROC melee_glGenerateMipmap;
extern PFNGLGENFRAMEBUFFERSPROC melee_glGenFramebuffers;
extern PFNGLGENRENDERBUFFERSPROC melee_glGenRenderbuffers;
extern PFNGLGENTEXTURESPROC melee_glGenTextures;
extern PFNGLGENVERTEXARRAYSPROC melee_glGenVertexArrays;
extern PFNGLGETERRORPROC melee_glGetError;
extern PFNGLGETPROGRAMINFOLOGPROC melee_glGetProgramInfoLog;
extern PFNGLGETPROGRAMIVPROC melee_glGetProgramiv;
extern PFNGLGETSHADERINFOLOGPROC melee_glGetShaderInfoLog;
extern PFNGLGETSHADERIVPROC melee_glGetShaderiv;
extern PFNGLGETSTRINGPROC melee_glGetString;
extern PFNGLGETUNIFORMLOCATIONPROC melee_glGetUniformLocation;
extern PFNGLISENABLEDPROC melee_glIsEnabled;
extern PFNGLLINEWIDTHPROC melee_glLineWidth;
extern PFNGLLINKPROGRAMPROC melee_glLinkProgram;
extern PFNGLPIXELSTOREIPROC melee_glPixelStorei;
extern PFNGLREADPIXELSPROC melee_glReadPixels;
extern PFNGLRENDERBUFFERSTORAGEPROC melee_glRenderbufferStorage;
extern PFNGLSCISSORPROC melee_glScissor;
extern PFNGLSHADERSOURCEPROC melee_glShaderSource;
extern PFNGLTEXIMAGE2DPROC melee_glTexImage2D;
extern PFNGLTEXPARAMETERFPROC melee_glTexParameterf;
extern PFNGLTEXPARAMETERIPROC melee_glTexParameteri;
extern PFNGLUNIFORM1FPROC melee_glUniform1f;
extern PFNGLUNIFORM1FVPROC melee_glUniform1fv;
extern PFNGLUNIFORM1IPROC melee_glUniform1i;
extern PFNGLUNIFORM1IVPROC melee_glUniform1iv;
extern PFNGLUNIFORM2FPROC melee_glUniform2f;
extern PFNGLUNIFORM2FVPROC melee_glUniform2fv;
extern PFNGLUNIFORM2IVPROC melee_glUniform2iv;
extern PFNGLUNIFORM3FPROC melee_glUniform3f;
extern PFNGLUNIFORM3FVPROC melee_glUniform3fv;
extern PFNGLUNIFORM4FPROC melee_glUniform4f;
extern PFNGLUNIFORM4FVPROC melee_glUniform4fv;
extern PFNGLUNIFORM4IVPROC melee_glUniform4iv;
extern PFNGLUNIFORMMATRIX3FVPROC melee_glUniformMatrix3fv;
extern PFNGLUNIFORMMATRIX4FVPROC melee_glUniformMatrix4fv;
extern PFNGLUSEPROGRAMPROC melee_glUseProgram;
extern PFNGLVERTEXATTRIBPOINTERPROC melee_glVertexAttribPointer;
extern PFNGLVIEWPORTPROC melee_glViewport;

#ifdef __cplusplus
}
#endif

#define glActiveTexture melee_glActiveTexture
#define glAttachShader melee_glAttachShader
#define glBindBuffer melee_glBindBuffer
#define glBindFramebuffer melee_glBindFramebuffer
#define glBindRenderbuffer melee_glBindRenderbuffer
#define glBindTexture melee_glBindTexture
#define glBindVertexArray melee_glBindVertexArray
#define glBlendEquation melee_glBlendEquation
#define glBlendFunc melee_glBlendFunc
#define glBlitFramebuffer melee_glBlitFramebuffer
#define glBufferData melee_glBufferData
#define glBufferSubData melee_glBufferSubData
#define glCheckFramebufferStatus melee_glCheckFramebufferStatus
#define glClear melee_glClear
#define glClearColor melee_glClearColor
#define glClearDepthf melee_glClearDepthf
#define glColorMask melee_glColorMask
#define glCompileShader melee_glCompileShader
#define glCopyTexSubImage2D melee_glCopyTexSubImage2D
#define glCreateProgram melee_glCreateProgram
#define glCreateShader melee_glCreateShader
#define glCullFace melee_glCullFace
#define glDeleteBuffers melee_glDeleteBuffers
#define glDeleteFramebuffers melee_glDeleteFramebuffers
#define glDeleteProgram melee_glDeleteProgram
#define glDeleteShader melee_glDeleteShader
#define glDeleteTextures melee_glDeleteTextures
#define glDeleteVertexArrays melee_glDeleteVertexArrays
#define glDepthFunc melee_glDepthFunc
#define glDepthMask melee_glDepthMask
#define glDepthRangef melee_glDepthRangef
#define glDisable melee_glDisable
#define glDrawArrays melee_glDrawArrays
#define glEnable melee_glEnable
#define glEnableVertexAttribArray melee_glEnableVertexAttribArray
#define glFinish melee_glFinish
#define glFramebufferRenderbuffer melee_glFramebufferRenderbuffer
#define glFramebufferTexture2D melee_glFramebufferTexture2D
#define glFrontFace melee_glFrontFace
#define glGenBuffers melee_glGenBuffers
#define glGenerateMipmap melee_glGenerateMipmap
#define glGenFramebuffers melee_glGenFramebuffers
#define glGenRenderbuffers melee_glGenRenderbuffers
#define glGenTextures melee_glGenTextures
#define glGenVertexArrays melee_glGenVertexArrays
#define glGetError melee_glGetError
#define glGetProgramInfoLog melee_glGetProgramInfoLog
#define glGetProgramiv melee_glGetProgramiv
#define glGetShaderInfoLog melee_glGetShaderInfoLog
#define glGetShaderiv melee_glGetShaderiv
#define glGetString melee_glGetString
#define glGetUniformLocation melee_glGetUniformLocation
#define glIsEnabled melee_glIsEnabled
#define glLineWidth melee_glLineWidth
#define glLinkProgram melee_glLinkProgram
#define glPixelStorei melee_glPixelStorei
#define glReadPixels melee_glReadPixels
#define glRenderbufferStorage melee_glRenderbufferStorage
#define glScissor melee_glScissor
#define glShaderSource melee_glShaderSource
#define glTexImage2D melee_glTexImage2D
#define glTexParameterf melee_glTexParameterf
#define glTexParameteri melee_glTexParameteri
#define glUniform1f melee_glUniform1f
#define glUniform1fv melee_glUniform1fv
#define glUniform1i melee_glUniform1i
#define glUniform1iv melee_glUniform1iv
#define glUniform2f melee_glUniform2f
#define glUniform2fv melee_glUniform2fv
#define glUniform2iv melee_glUniform2iv
#define glUniform3f melee_glUniform3f
#define glUniform3fv melee_glUniform3fv
#define glUniform4f melee_glUniform4f
#define glUniform4fv melee_glUniform4fv
#define glUniform4iv melee_glUniform4iv
#define glUniformMatrix3fv melee_glUniformMatrix3fv
#define glUniformMatrix4fv melee_glUniformMatrix4fv
#define glUseProgram melee_glUseProgram
#define glVertexAttribPointer melee_glVertexAttribPointer
#define glViewport melee_glViewport

#else /* every other target links the library directly */

#include <GLES3/gl3.h>
#define melee_gl_load() 0

#endif

#endif
