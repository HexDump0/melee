/* The Windows entry-point table for gl_api.h.  See that header for why. */
#include "gx/gl_api.h"

#ifdef _WIN32

#include <SDL3/SDL.h>

/* The macros in the header would rewrite these definitions into themselves. */
#undef glActiveTexture
#undef glAttachShader
#undef glBindBuffer
#undef glBindFramebuffer
#undef glBindRenderbuffer
#undef glBindTexture
#undef glBindVertexArray
#undef glBlendEquation
#undef glBlendFunc
#undef glBlitFramebuffer
#undef glBufferData
#undef glBufferSubData
#undef glCheckFramebufferStatus
#undef glClear
#undef glClearColor
#undef glClearDepthf
#undef glColorMask
#undef glCompileShader
#undef glCopyTexSubImage2D
#undef glCreateProgram
#undef glCreateShader
#undef glCullFace
#undef glDeleteBuffers
#undef glDeleteFramebuffers
#undef glDeleteProgram
#undef glDeleteShader
#undef glDeleteTextures
#undef glDeleteVertexArrays
#undef glDepthFunc
#undef glDepthMask
#undef glDepthRangef
#undef glDisable
#undef glDrawArrays
#undef glEnable
#undef glEnableVertexAttribArray
#undef glFinish
#undef glFramebufferRenderbuffer
#undef glFramebufferTexture2D
#undef glFrontFace
#undef glGenBuffers
#undef glGenerateMipmap
#undef glGenFramebuffers
#undef glGenRenderbuffers
#undef glGenTextures
#undef glGenVertexArrays
#undef glGetError
#undef glGetProgramInfoLog
#undef glGetProgramiv
#undef glGetShaderInfoLog
#undef glGetShaderiv
#undef glGetString
#undef glGetUniformLocation
#undef glIsEnabled
#undef glLineWidth
#undef glLinkProgram
#undef glPixelStorei
#undef glReadPixels
#undef glRenderbufferStorage
#undef glScissor
#undef glShaderSource
#undef glTexImage2D
#undef glTexParameterf
#undef glTexParameteri
#undef glUniform1f
#undef glUniform1fv
#undef glUniform1i
#undef glUniform1iv
#undef glUniform2f
#undef glUniform2fv
#undef glUniform2iv
#undef glUniform3f
#undef glUniform3fv
#undef glUniform4f
#undef glUniform4fv
#undef glUniform4iv
#undef glUniformMatrix3fv
#undef glUniformMatrix4fv
#undef glUseProgram
#undef glVertexAttribPointer
#undef glViewport

PFNGLACTIVETEXTUREPROC melee_glActiveTexture;
PFNGLATTACHSHADERPROC melee_glAttachShader;
PFNGLBINDBUFFERPROC melee_glBindBuffer;
PFNGLBINDFRAMEBUFFERPROC melee_glBindFramebuffer;
PFNGLBINDRENDERBUFFERPROC melee_glBindRenderbuffer;
PFNGLBINDTEXTUREPROC melee_glBindTexture;
PFNGLBINDVERTEXARRAYPROC melee_glBindVertexArray;
PFNGLBLENDEQUATIONPROC melee_glBlendEquation;
PFNGLBLENDFUNCPROC melee_glBlendFunc;
PFNGLBLITFRAMEBUFFERPROC melee_glBlitFramebuffer;
PFNGLBUFFERDATAPROC melee_glBufferData;
PFNGLBUFFERSUBDATAPROC melee_glBufferSubData;
PFNGLCHECKFRAMEBUFFERSTATUSPROC melee_glCheckFramebufferStatus;
PFNGLCLEARPROC melee_glClear;
PFNGLCLEARCOLORPROC melee_glClearColor;
PFNGLCLEARDEPTHFPROC melee_glClearDepthf;
PFNGLCOLORMASKPROC melee_glColorMask;
PFNGLCOMPILESHADERPROC melee_glCompileShader;
PFNGLCOPYTEXSUBIMAGE2DPROC melee_glCopyTexSubImage2D;
PFNGLCREATEPROGRAMPROC melee_glCreateProgram;
PFNGLCREATESHADERPROC melee_glCreateShader;
PFNGLCULLFACEPROC melee_glCullFace;
PFNGLDELETEBUFFERSPROC melee_glDeleteBuffers;
PFNGLDELETEFRAMEBUFFERSPROC melee_glDeleteFramebuffers;
PFNGLDELETEPROGRAMPROC melee_glDeleteProgram;
PFNGLDELETESHADERPROC melee_glDeleteShader;
PFNGLDELETETEXTURESPROC melee_glDeleteTextures;
PFNGLDELETEVERTEXARRAYSPROC melee_glDeleteVertexArrays;
PFNGLDEPTHFUNCPROC melee_glDepthFunc;
PFNGLDEPTHMASKPROC melee_glDepthMask;
PFNGLDEPTHRANGEFPROC melee_glDepthRangef;
PFNGLDISABLEPROC melee_glDisable;
PFNGLDRAWARRAYSPROC melee_glDrawArrays;
PFNGLENABLEPROC melee_glEnable;
PFNGLENABLEVERTEXATTRIBARRAYPROC melee_glEnableVertexAttribArray;
PFNGLFINISHPROC melee_glFinish;
PFNGLFRAMEBUFFERRENDERBUFFERPROC melee_glFramebufferRenderbuffer;
PFNGLFRAMEBUFFERTEXTURE2DPROC melee_glFramebufferTexture2D;
PFNGLFRONTFACEPROC melee_glFrontFace;
PFNGLGENBUFFERSPROC melee_glGenBuffers;
PFNGLGENERATEMIPMAPPROC melee_glGenerateMipmap;
PFNGLGENFRAMEBUFFERSPROC melee_glGenFramebuffers;
PFNGLGENRENDERBUFFERSPROC melee_glGenRenderbuffers;
PFNGLGENTEXTURESPROC melee_glGenTextures;
PFNGLGENVERTEXARRAYSPROC melee_glGenVertexArrays;
PFNGLGETERRORPROC melee_glGetError;
PFNGLGETPROGRAMINFOLOGPROC melee_glGetProgramInfoLog;
PFNGLGETPROGRAMIVPROC melee_glGetProgramiv;
PFNGLGETSHADERINFOLOGPROC melee_glGetShaderInfoLog;
PFNGLGETSHADERIVPROC melee_glGetShaderiv;
PFNGLGETSTRINGPROC melee_glGetString;
PFNGLGETUNIFORMLOCATIONPROC melee_glGetUniformLocation;
PFNGLISENABLEDPROC melee_glIsEnabled;
PFNGLLINEWIDTHPROC melee_glLineWidth;
PFNGLLINKPROGRAMPROC melee_glLinkProgram;
PFNGLPIXELSTOREIPROC melee_glPixelStorei;
PFNGLREADPIXELSPROC melee_glReadPixels;
PFNGLRENDERBUFFERSTORAGEPROC melee_glRenderbufferStorage;
PFNGLSCISSORPROC melee_glScissor;
PFNGLSHADERSOURCEPROC melee_glShaderSource;
PFNGLTEXIMAGE2DPROC melee_glTexImage2D;
PFNGLTEXPARAMETERFPROC melee_glTexParameterf;
PFNGLTEXPARAMETERIPROC melee_glTexParameteri;
PFNGLUNIFORM1FPROC melee_glUniform1f;
PFNGLUNIFORM1FVPROC melee_glUniform1fv;
PFNGLUNIFORM1IPROC melee_glUniform1i;
PFNGLUNIFORM1IVPROC melee_glUniform1iv;
PFNGLUNIFORM2FPROC melee_glUniform2f;
PFNGLUNIFORM2FVPROC melee_glUniform2fv;
PFNGLUNIFORM2IVPROC melee_glUniform2iv;
PFNGLUNIFORM3FPROC melee_glUniform3f;
PFNGLUNIFORM3FVPROC melee_glUniform3fv;
PFNGLUNIFORM4FPROC melee_glUniform4f;
PFNGLUNIFORM4FVPROC melee_glUniform4fv;
PFNGLUNIFORM4IVPROC melee_glUniform4iv;
PFNGLUNIFORMMATRIX3FVPROC melee_glUniformMatrix3fv;
PFNGLUNIFORMMATRIX4FVPROC melee_glUniformMatrix4fv;
PFNGLUSEPROGRAMPROC melee_glUseProgram;
PFNGLVERTEXATTRIBPOINTERPROC melee_glVertexAttribPointer;
PFNGLVIEWPORTPROC melee_glViewport;

int melee_gl_load(void)
{
    int missing = 0;

    melee_glActiveTexture = (PFNGLACTIVETEXTUREPROC) SDL_GL_GetProcAddress("glActiveTexture");
    if (melee_glActiveTexture == NULL) { missing++; }
    melee_glAttachShader = (PFNGLATTACHSHADERPROC) SDL_GL_GetProcAddress("glAttachShader");
    if (melee_glAttachShader == NULL) { missing++; }
    melee_glBindBuffer = (PFNGLBINDBUFFERPROC) SDL_GL_GetProcAddress("glBindBuffer");
    if (melee_glBindBuffer == NULL) { missing++; }
    melee_glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC) SDL_GL_GetProcAddress("glBindFramebuffer");
    if (melee_glBindFramebuffer == NULL) { missing++; }
    melee_glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC) SDL_GL_GetProcAddress("glBindRenderbuffer");
    if (melee_glBindRenderbuffer == NULL) { missing++; }
    melee_glBindTexture = (PFNGLBINDTEXTUREPROC) SDL_GL_GetProcAddress("glBindTexture");
    if (melee_glBindTexture == NULL) { missing++; }
    melee_glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC) SDL_GL_GetProcAddress("glBindVertexArray");
    if (melee_glBindVertexArray == NULL) { missing++; }
    melee_glBlendEquation = (PFNGLBLENDEQUATIONPROC) SDL_GL_GetProcAddress("glBlendEquation");
    if (melee_glBlendEquation == NULL) { missing++; }
    melee_glBlendFunc = (PFNGLBLENDFUNCPROC) SDL_GL_GetProcAddress("glBlendFunc");
    if (melee_glBlendFunc == NULL) { missing++; }
    melee_glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC) SDL_GL_GetProcAddress("glBlitFramebuffer");
    if (melee_glBlitFramebuffer == NULL) { missing++; }
    melee_glBufferData = (PFNGLBUFFERDATAPROC) SDL_GL_GetProcAddress("glBufferData");
    if (melee_glBufferData == NULL) { missing++; }
    melee_glBufferSubData = (PFNGLBUFFERSUBDATAPROC) SDL_GL_GetProcAddress("glBufferSubData");
    if (melee_glBufferSubData == NULL) { missing++; }
    melee_glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC) SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    if (melee_glCheckFramebufferStatus == NULL) { missing++; }
    melee_glClear = (PFNGLCLEARPROC) SDL_GL_GetProcAddress("glClear");
    if (melee_glClear == NULL) { missing++; }
    melee_glClearColor = (PFNGLCLEARCOLORPROC) SDL_GL_GetProcAddress("glClearColor");
    if (melee_glClearColor == NULL) { missing++; }
    melee_glClearDepthf = (PFNGLCLEARDEPTHFPROC) SDL_GL_GetProcAddress("glClearDepthf");
    if (melee_glClearDepthf == NULL) { missing++; }
    melee_glColorMask = (PFNGLCOLORMASKPROC) SDL_GL_GetProcAddress("glColorMask");
    if (melee_glColorMask == NULL) { missing++; }
    melee_glCompileShader = (PFNGLCOMPILESHADERPROC) SDL_GL_GetProcAddress("glCompileShader");
    if (melee_glCompileShader == NULL) { missing++; }
    melee_glCopyTexSubImage2D = (PFNGLCOPYTEXSUBIMAGE2DPROC) SDL_GL_GetProcAddress("glCopyTexSubImage2D");
    if (melee_glCopyTexSubImage2D == NULL) { missing++; }
    melee_glCreateProgram = (PFNGLCREATEPROGRAMPROC) SDL_GL_GetProcAddress("glCreateProgram");
    if (melee_glCreateProgram == NULL) { missing++; }
    melee_glCreateShader = (PFNGLCREATESHADERPROC) SDL_GL_GetProcAddress("glCreateShader");
    if (melee_glCreateShader == NULL) { missing++; }
    melee_glCullFace = (PFNGLCULLFACEPROC) SDL_GL_GetProcAddress("glCullFace");
    if (melee_glCullFace == NULL) { missing++; }
    melee_glDeleteBuffers = (PFNGLDELETEBUFFERSPROC) SDL_GL_GetProcAddress("glDeleteBuffers");
    if (melee_glDeleteBuffers == NULL) { missing++; }
    melee_glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC) SDL_GL_GetProcAddress("glDeleteFramebuffers");
    if (melee_glDeleteFramebuffers == NULL) { missing++; }
    melee_glDeleteProgram = (PFNGLDELETEPROGRAMPROC) SDL_GL_GetProcAddress("glDeleteProgram");
    if (melee_glDeleteProgram == NULL) { missing++; }
    melee_glDeleteShader = (PFNGLDELETESHADERPROC) SDL_GL_GetProcAddress("glDeleteShader");
    if (melee_glDeleteShader == NULL) { missing++; }
    melee_glDeleteTextures = (PFNGLDELETETEXTURESPROC) SDL_GL_GetProcAddress("glDeleteTextures");
    if (melee_glDeleteTextures == NULL) { missing++; }
    melee_glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC) SDL_GL_GetProcAddress("glDeleteVertexArrays");
    if (melee_glDeleteVertexArrays == NULL) { missing++; }
    melee_glDepthFunc = (PFNGLDEPTHFUNCPROC) SDL_GL_GetProcAddress("glDepthFunc");
    if (melee_glDepthFunc == NULL) { missing++; }
    melee_glDepthMask = (PFNGLDEPTHMASKPROC) SDL_GL_GetProcAddress("glDepthMask");
    if (melee_glDepthMask == NULL) { missing++; }
    melee_glDepthRangef = (PFNGLDEPTHRANGEFPROC) SDL_GL_GetProcAddress("glDepthRangef");
    if (melee_glDepthRangef == NULL) { missing++; }
    melee_glDisable = (PFNGLDISABLEPROC) SDL_GL_GetProcAddress("glDisable");
    if (melee_glDisable == NULL) { missing++; }
    melee_glDrawArrays = (PFNGLDRAWARRAYSPROC) SDL_GL_GetProcAddress("glDrawArrays");
    if (melee_glDrawArrays == NULL) { missing++; }
    melee_glEnable = (PFNGLENABLEPROC) SDL_GL_GetProcAddress("glEnable");
    if (melee_glEnable == NULL) { missing++; }
    melee_glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC) SDL_GL_GetProcAddress("glEnableVertexAttribArray");
    if (melee_glEnableVertexAttribArray == NULL) { missing++; }
    melee_glFinish = (PFNGLFINISHPROC) SDL_GL_GetProcAddress("glFinish");
    if (melee_glFinish == NULL) { missing++; }
    melee_glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC) SDL_GL_GetProcAddress("glFramebufferRenderbuffer");
    if (melee_glFramebufferRenderbuffer == NULL) { missing++; }
    melee_glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC) SDL_GL_GetProcAddress("glFramebufferTexture2D");
    if (melee_glFramebufferTexture2D == NULL) { missing++; }
    melee_glFrontFace = (PFNGLFRONTFACEPROC) SDL_GL_GetProcAddress("glFrontFace");
    if (melee_glFrontFace == NULL) { missing++; }
    melee_glGenBuffers = (PFNGLGENBUFFERSPROC) SDL_GL_GetProcAddress("glGenBuffers");
    if (melee_glGenBuffers == NULL) { missing++; }
    melee_glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC) SDL_GL_GetProcAddress("glGenerateMipmap");
    if (melee_glGenerateMipmap == NULL) { missing++; }
    melee_glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC) SDL_GL_GetProcAddress("glGenFramebuffers");
    if (melee_glGenFramebuffers == NULL) { missing++; }
    melee_glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC) SDL_GL_GetProcAddress("glGenRenderbuffers");
    if (melee_glGenRenderbuffers == NULL) { missing++; }
    melee_glGenTextures = (PFNGLGENTEXTURESPROC) SDL_GL_GetProcAddress("glGenTextures");
    if (melee_glGenTextures == NULL) { missing++; }
    melee_glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC) SDL_GL_GetProcAddress("glGenVertexArrays");
    if (melee_glGenVertexArrays == NULL) { missing++; }
    melee_glGetError = (PFNGLGETERRORPROC) SDL_GL_GetProcAddress("glGetError");
    if (melee_glGetError == NULL) { missing++; }
    melee_glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC) SDL_GL_GetProcAddress("glGetProgramInfoLog");
    if (melee_glGetProgramInfoLog == NULL) { missing++; }
    melee_glGetProgramiv = (PFNGLGETPROGRAMIVPROC) SDL_GL_GetProcAddress("glGetProgramiv");
    if (melee_glGetProgramiv == NULL) { missing++; }
    melee_glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC) SDL_GL_GetProcAddress("glGetShaderInfoLog");
    if (melee_glGetShaderInfoLog == NULL) { missing++; }
    melee_glGetShaderiv = (PFNGLGETSHADERIVPROC) SDL_GL_GetProcAddress("glGetShaderiv");
    if (melee_glGetShaderiv == NULL) { missing++; }
    melee_glGetString = (PFNGLGETSTRINGPROC) SDL_GL_GetProcAddress("glGetString");
    if (melee_glGetString == NULL) { missing++; }
    melee_glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC) SDL_GL_GetProcAddress("glGetUniformLocation");
    if (melee_glGetUniformLocation == NULL) { missing++; }
    melee_glIsEnabled = (PFNGLISENABLEDPROC) SDL_GL_GetProcAddress("glIsEnabled");
    if (melee_glIsEnabled == NULL) { missing++; }
    melee_glLineWidth = (PFNGLLINEWIDTHPROC) SDL_GL_GetProcAddress("glLineWidth");
    if (melee_glLineWidth == NULL) { missing++; }
    melee_glLinkProgram = (PFNGLLINKPROGRAMPROC) SDL_GL_GetProcAddress("glLinkProgram");
    if (melee_glLinkProgram == NULL) { missing++; }
    melee_glPixelStorei = (PFNGLPIXELSTOREIPROC) SDL_GL_GetProcAddress("glPixelStorei");
    if (melee_glPixelStorei == NULL) { missing++; }
    melee_glReadPixels = (PFNGLREADPIXELSPROC) SDL_GL_GetProcAddress("glReadPixels");
    if (melee_glReadPixels == NULL) { missing++; }
    melee_glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC) SDL_GL_GetProcAddress("glRenderbufferStorage");
    if (melee_glRenderbufferStorage == NULL) { missing++; }
    melee_glScissor = (PFNGLSCISSORPROC) SDL_GL_GetProcAddress("glScissor");
    if (melee_glScissor == NULL) { missing++; }
    melee_glShaderSource = (PFNGLSHADERSOURCEPROC) SDL_GL_GetProcAddress("glShaderSource");
    if (melee_glShaderSource == NULL) { missing++; }
    melee_glTexImage2D = (PFNGLTEXIMAGE2DPROC) SDL_GL_GetProcAddress("glTexImage2D");
    if (melee_glTexImage2D == NULL) { missing++; }
    melee_glTexParameterf = (PFNGLTEXPARAMETERFPROC) SDL_GL_GetProcAddress("glTexParameterf");
    if (melee_glTexParameterf == NULL) { missing++; }
    melee_glTexParameteri = (PFNGLTEXPARAMETERIPROC) SDL_GL_GetProcAddress("glTexParameteri");
    if (melee_glTexParameteri == NULL) { missing++; }
    melee_glUniform1f = (PFNGLUNIFORM1FPROC) SDL_GL_GetProcAddress("glUniform1f");
    if (melee_glUniform1f == NULL) { missing++; }
    melee_glUniform1fv = (PFNGLUNIFORM1FVPROC) SDL_GL_GetProcAddress("glUniform1fv");
    if (melee_glUniform1fv == NULL) { missing++; }
    melee_glUniform1i = (PFNGLUNIFORM1IPROC) SDL_GL_GetProcAddress("glUniform1i");
    if (melee_glUniform1i == NULL) { missing++; }
    melee_glUniform1iv = (PFNGLUNIFORM1IVPROC) SDL_GL_GetProcAddress("glUniform1iv");
    if (melee_glUniform1iv == NULL) { missing++; }
    melee_glUniform2f = (PFNGLUNIFORM2FPROC) SDL_GL_GetProcAddress("glUniform2f");
    if (melee_glUniform2f == NULL) { missing++; }
    melee_glUniform2fv = (PFNGLUNIFORM2FVPROC) SDL_GL_GetProcAddress("glUniform2fv");
    if (melee_glUniform2fv == NULL) { missing++; }
    melee_glUniform2iv = (PFNGLUNIFORM2IVPROC) SDL_GL_GetProcAddress("glUniform2iv");
    if (melee_glUniform2iv == NULL) { missing++; }
    melee_glUniform3f = (PFNGLUNIFORM3FPROC) SDL_GL_GetProcAddress("glUniform3f");
    if (melee_glUniform3f == NULL) { missing++; }
    melee_glUniform3fv = (PFNGLUNIFORM3FVPROC) SDL_GL_GetProcAddress("glUniform3fv");
    if (melee_glUniform3fv == NULL) { missing++; }
    melee_glUniform4f = (PFNGLUNIFORM4FPROC) SDL_GL_GetProcAddress("glUniform4f");
    if (melee_glUniform4f == NULL) { missing++; }
    melee_glUniform4fv = (PFNGLUNIFORM4FVPROC) SDL_GL_GetProcAddress("glUniform4fv");
    if (melee_glUniform4fv == NULL) { missing++; }
    melee_glUniform4iv = (PFNGLUNIFORM4IVPROC) SDL_GL_GetProcAddress("glUniform4iv");
    if (melee_glUniform4iv == NULL) { missing++; }
    melee_glUniformMatrix3fv = (PFNGLUNIFORMMATRIX3FVPROC) SDL_GL_GetProcAddress("glUniformMatrix3fv");
    if (melee_glUniformMatrix3fv == NULL) { missing++; }
    melee_glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC) SDL_GL_GetProcAddress("glUniformMatrix4fv");
    if (melee_glUniformMatrix4fv == NULL) { missing++; }
    melee_glUseProgram = (PFNGLUSEPROGRAMPROC) SDL_GL_GetProcAddress("glUseProgram");
    if (melee_glUseProgram == NULL) { missing++; }
    melee_glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC) SDL_GL_GetProcAddress("glVertexAttribPointer");
    if (melee_glVertexAttribPointer == NULL) { missing++; }
    melee_glViewport = (PFNGLVIEWPORTPROC) SDL_GL_GetProcAddress("glViewport");
    if (melee_glViewport == NULL) { missing++; }

    return missing;
}

#endif
