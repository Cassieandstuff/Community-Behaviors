#ifndef GLAD_GL_H_
#define GLAD_GL_H_

/* Prevent system GL headers from being included after this */
#define __gl_h_
#define __gl2_h_
#define __gl3_h_
#define __glext_h_
#define __gl3ext_h_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <KHR/khrplatform.h>

/* ── GL base types ──────────────────────────────────────────────────────── */
typedef void             GLvoid;
typedef unsigned char    GLboolean;
typedef unsigned char    GLubyte;
typedef signed char      GLbyte;
typedef unsigned short   GLushort;
typedef short            GLshort;
typedef unsigned int     GLuint;
typedef int              GLint;
typedef unsigned int     GLenum;
typedef unsigned int     GLbitfield;
typedef float            GLfloat;
typedef float            GLclampf;
typedef double           GLdouble;
typedef double           GLclampd;
typedef int              GLsizei;
typedef khronos_intptr_t GLintptr;
typedef khronos_intptr_t GLsizeiptr;
typedef char             GLchar;
typedef khronos_int64_t  GLint64;
typedef khronos_uint64_t GLuint64;
typedef struct __GLsync *GLsync;

/* Windows calling convention */
#ifndef APIENTRY
#  ifdef _WIN32
#    define APIENTRY __stdcall
#  else
#    define APIENTRY
#  endif
#endif
#ifndef APIENTRYP
#  define APIENTRYP APIENTRY *
#endif

/* ── Constants ──────────────────────────────────────────────────────────── */
#define GL_FALSE                          0
#define GL_TRUE                           1
#define GL_NONE                           0

/* clear bits */
#define GL_DEPTH_BUFFER_BIT               0x00000100
#define GL_COLOR_BUFFER_BIT               0x00004000

/* primitives */
#define GL_POINTS                         0x0000
#define GL_LINES                          0x0001
#define GL_TRIANGLES                      0x0004
#define GL_TRIANGLE_STRIP                 0x0005

/* errors */
#define GL_NO_ERROR                       0
#define GL_INVALID_ENUM                   0x0500
#define GL_INVALID_VALUE                  0x0501
#define GL_INVALID_OPERATION              0x0502
#define GL_OUT_OF_MEMORY                  0x0505

/* polygon mode */
#define GL_FRONT_AND_BACK                 0x0408
#define GL_LINE                           0x1B01
#define GL_FILL                           0x1B02

/* depth/blend */
#define GL_DEPTH_TEST                     0x0B71
#define GL_LEQUAL                         0x0203
#define GL_LESS                           0x0201
#define GL_BLEND                          0x0BE2
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_ONE                            0x0001

/* face culling */
#define GL_CULL_FACE                      0x0B44
#define GL_BACK                           0x0405
#define GL_FRONT                          0x0404

/* data types */
#define GL_UNSIGNED_SHORT                 0x1403

/* texture */
#define GL_TEXTURE_2D                     0x0DE1
#define GL_RGBA                           0x1908
#define GL_RGBA8                          0x8058
#define GL_RGBA16F                        0x881A
#define GL_RGB                            0x1907
#define GL_BGRA                           0x80E1
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_FLOAT                          0x1406
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803
#define GL_LINEAR                         0x2601
#define GL_NEAREST                        0x2600
#define GL_LINEAR_MIPMAP_LINEAR           0x2703
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_REPEAT                         0x2901
#define GL_TEXTURE_MAX_LEVEL              0x813D
#define GL_TEXTURE0                       0x84C0
#define GL_TEXTURE1                       0x84C1
#define GL_TEXTURE2                       0x84C2
#define GL_TEXTURE3                       0x84C3
#define GL_TEXTURE4                       0x84C4
#define GL_TEXTURE5                       0x84C5
#define GL_TEXTURE6                       0x84C6
#define GL_TEXTURE7                       0x84C7
#define GL_TEXTURE8                       0x84C8
#define GL_TEXTURE9                       0x84C9
#define GL_TEXTURE10                      0x84CA
#define GL_RED                            0x1903
#define GL_R32F                           0x8228

/* framebuffer */
#define GL_FRAMEBUFFER                    0x8D40
#define GL_DRAW_FRAMEBUFFER               0x8CA9
#define GL_READ_FRAMEBUFFER               0x8CA8
#define GL_COLOR_ATTACHMENT0              0x8CE0
#define GL_DEPTH_ATTACHMENT               0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT       0x821A
#define GL_RENDERBUFFER                   0x8D41
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
#define GL_DEPTH_COMPONENT24              0x81A6
#define GL_DEPTH24_STENCIL8               0x88F0
#define GL_DRAW_FRAMEBUFFER_BINDING       0x8CA6

/* shader */
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84

/* buffer */
#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893
#define GL_SHADER_STORAGE_BUFFER          0x90D2
#define GL_STATIC_DRAW                    0x88B4
#define GL_DYNAMIC_DRAW                   0x88E8
#define GL_STREAM_DRAW                    0x88E0

/* point rendering */
#define GL_PROGRAM_POINT_SIZE             0x8642

/* get */
#define GL_VIEWPORT                       0x0BA2

/* ── Function pointer types ─────────────────────────────────────────────── */
typedef void   (APIENTRYP PFNGLCLEARCOLORPROC)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
typedef void   (APIENTRYP PFNGLCLEARPROC)(GLbitfield mask);
typedef void   (APIENTRYP PFNGLENABLEPROC)(GLenum cap);
typedef void   (APIENTRYP PFNGLDISABLEPROC)(GLenum cap);
typedef void   (APIENTRYP PFNGLDEPTHFUNCPROC)(GLenum func);
typedef void   (APIENTRYP PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei w, GLsizei h);
typedef void   (APIENTRYP PFNGLGENERATEMIPMAPPROC)(GLenum target);
typedef GLenum (APIENTRYP PFNGLGETERRORPROC)(void);
typedef void   (APIENTRYP PFNGLGETINTEGERVPROC)(GLenum pname, GLint* data);
typedef void   (APIENTRYP PFNGLLINEWIDTHPROC)(GLfloat width);
typedef void   (APIENTRYP PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);
typedef void   (APIENTRYP PFNGLBLENDFUNCPROC)(GLenum sfactor, GLenum dfactor);

/* textures */
typedef void   (APIENTRYP PFNGLGENTEXTURESPROC)(GLsizei n, GLuint* textures);
typedef void   (APIENTRYP PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint* textures);
typedef void   (APIENTRYP PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void   (APIENTRYP PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* pixels);
typedef void   (APIENTRYP PFNGLCOMPRESSEDTEXIMAGE2DPROC)(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid* data);
typedef void   (APIENTRYP PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void   (APIENTRYP PFNGLACTIVETEXTUREPROC)(GLenum texture);

/* VBO / VAO */
typedef void   (APIENTRYP PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
typedef void   (APIENTRYP PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);
typedef void   (APIENTRYP PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void   (APIENTRYP PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage);
typedef void   (APIENTRYP PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void   (APIENTRYP PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint* arrays);
typedef void   (APIENTRYP PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void   (APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void   (APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* pointer);

/* index / element drawing */
typedef void   (APIENTRYP PFNGLDRAWELEMENTSPROC)(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);

/* integer vertex attrib */
typedef void   (APIENTRYP PFNGLVERTEXATTRIBIPOINTERPROC)(GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid* pointer);

/* face culling */
typedef void   (APIENTRYP PFNGLCULLFACEPROC)(GLenum mode);

/* polygon rasterization */
typedef void   (APIENTRYP PFNGLPOLYGONMODEPROC)(GLenum face, GLenum mode);

/* SSBO / buffer base */
typedef void   (APIENTRYP PFNGLBINDBUFFERBASEPROC)(GLenum target, GLuint index, GLuint buffer);

/* shaders */
typedef GLuint (APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void   (APIENTRYP PFNGLDELETESHADERPROC)(GLuint shader);
typedef void   (APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void   (APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void   (APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void   (APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef GLuint (APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
typedef void   (APIENTRYP PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void   (APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void   (APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void   (APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void   (APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void   (APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef GLint  (APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void   (APIENTRYP PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void   (APIENTRYP PFNGLUNIFORM4FVPROC)(GLint location, GLsizei count, const GLfloat* value);
typedef void   (APIENTRYP PFNGLUNIFORM3FVPROC)(GLint location, GLsizei count, const GLfloat* value);
typedef void   (APIENTRYP PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void   (APIENTRYP PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void   (APIENTRYP PFNGLUNIFORM1IVPROC)(GLint location, GLsizei count, const GLint* value);
typedef void   (APIENTRYP PFNGLUNIFORM1FVPROC)(GLint location, GLsizei count, const GLfloat* value);
typedef void   (APIENTRYP PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
typedef void   (APIENTRYP PFNGLDEPTHMASKPROC)(GLboolean flag);
typedef GLboolean (APIENTRYP PFNGLISENABLED)(GLenum cap);

/* framebuffer */
typedef void   (APIENTRYP PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void   (APIENTRYP PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* framebuffers);
typedef void   (APIENTRYP PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void   (APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void   (APIENTRYP PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef GLenum (APIENTRYP PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void   (APIENTRYP PFNGLGENRENDERBUFFERSPROC)(GLsizei n, GLuint* renderbuffers);
typedef void   (APIENTRYP PFNGLDELETERENDERBUFFERSPROC)(GLsizei n, const GLuint* renderbuffers);
typedef void   (APIENTRYP PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint renderbuffer);
typedef void   (APIENTRYP PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);

/* ── Extern declarations ─────────────────────────────────────────────────── */
extern PFNGLCLEARCOLORPROC              glad_glClearColor;
extern PFNGLCLEARPROC                   glad_glClear;
extern PFNGLENABLEPROC                  glad_glEnable;
extern PFNGLDISABLEPROC                 glad_glDisable;
extern PFNGLDEPTHFUNCPROC               glad_glDepthFunc;
extern PFNGLVIEWPORTPROC                glad_glViewport;
extern PFNGLGETERRORPROC                glad_glGetError;
extern PFNGLGETINTEGERVPROC             glad_glGetIntegerv;
extern PFNGLLINEWIDTHPROC               glad_glLineWidth;
extern PFNGLDRAWARRAYSPROC              glad_glDrawArrays;
extern PFNGLBLENDFUNCPROC               glad_glBlendFunc;
extern PFNGLGENTEXTURESPROC             glad_glGenTextures;
extern PFNGLDELETETEXTURESPROC          glad_glDeleteTextures;
extern PFNGLBINDTEXTUREPROC             glad_glBindTexture;
extern PFNGLTEXIMAGE2DPROC              glad_glTexImage2D;
extern PFNGLCOMPRESSEDTEXIMAGE2DPROC    glad_glCompressedTexImage2D;
extern PFNGLTEXPARAMETERIPROC           glad_glTexParameteri;
extern PFNGLGENERATEMIPMAPPROC          glad_glGenerateMipmap;
extern PFNGLACTIVETEXTUREPROC           glad_glActiveTexture;
extern PFNGLGENBUFFERSPROC              glad_glGenBuffers;
extern PFNGLDELETEBUFFERSPROC           glad_glDeleteBuffers;
extern PFNGLBINDBUFFERPROC              glad_glBindBuffer;
extern PFNGLBUFFERDATAPROC              glad_glBufferData;
extern PFNGLGENVERTEXARRAYSPROC         glad_glGenVertexArrays;
extern PFNGLDELETEVERTEXARRAYSPROC      glad_glDeleteVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC         glad_glBindVertexArray;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC     glad_glVertexAttribPointer;
extern PFNGLVERTEXATTRIBIPOINTERPROC    glad_glVertexAttribIPointer;
extern PFNGLDRAWELEMENTSPROC            glad_glDrawElements;
extern PFNGLCULLFACEPROC                glad_glCullFace;
extern PFNGLPOLYGONMODEPROC             glad_glPolygonMode;
extern PFNGLBINDBUFFERBASEPROC          glad_glBindBufferBase;
extern PFNGLCREATESHADERPROC            glad_glCreateShader;
extern PFNGLDELETESHADERPROC            glad_glDeleteShader;
extern PFNGLSHADERSOURCEPROC            glad_glShaderSource;
extern PFNGLCOMPILESHADERPROC           glad_glCompileShader;
extern PFNGLGETSHADERIVPROC             glad_glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC        glad_glGetShaderInfoLog;
extern PFNGLCREATEPROGRAMPROC           glad_glCreateProgram;
extern PFNGLDELETEPROGRAMPROC           glad_glDeleteProgram;
extern PFNGLATTACHSHADERPROC            glad_glAttachShader;
extern PFNGLLINKPROGRAMPROC             glad_glLinkProgram;
extern PFNGLUSEPROGRAMPROC              glad_glUseProgram;
extern PFNGLGETPROGRAMIVPROC            glad_glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC       glad_glGetProgramInfoLog;
extern PFNGLGETUNIFORMLOCATIONPROC      glad_glGetUniformLocation;
extern PFNGLUNIFORM1IPROC               glad_glUniform1i;
extern PFNGLUNIFORM1FPROC               glad_glUniform1f;
extern PFNGLUNIFORM1IVPROC              glad_glUniform1iv;
extern PFNGLUNIFORM1FVPROC              glad_glUniform1fv;
extern PFNGLUNIFORM4FPROC               glad_glUniform4f;
extern PFNGLUNIFORM4FVPROC              glad_glUniform4fv;
extern PFNGLUNIFORM3FVPROC              glad_glUniform3fv;
extern PFNGLUNIFORMMATRIX4FVPROC        glad_glUniformMatrix4fv;
extern PFNGLDEPTHMASKPROC               glad_glDepthMask;
extern PFNGLISENABLED                   glad_glIsEnabled;
extern PFNGLGENFRAMEBUFFERSPROC         glad_glGenFramebuffers;
extern PFNGLDELETEFRAMEBUFFERSPROC      glad_glDeleteFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC         glad_glBindFramebuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC    glad_glFramebufferTexture2D;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC  glad_glCheckFramebufferStatus;
extern PFNGLGENRENDERBUFFERSPROC        glad_glGenRenderbuffers;
extern PFNGLDELETERENDERBUFFERSPROC     glad_glDeleteRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC        glad_glBindRenderbuffer;
extern PFNGLRENDERBUFFERSTORAGEPROC     glad_glRenderbufferStorage;

/* ── Macro wrappers ──────────────────────────────────────────────────────── */
#define glClearColor              glad_glClearColor
#define glClear                   glad_glClear
#define glEnable                  glad_glEnable
#define glDisable                 glad_glDisable
#define glDepthFunc               glad_glDepthFunc
#define glViewport                glad_glViewport
#define glGetError                glad_glGetError
#define glGetIntegerv             glad_glGetIntegerv
#define glLineWidth               glad_glLineWidth
#define glDrawArrays              glad_glDrawArrays
#define glBlendFunc               glad_glBlendFunc
#define glGenTextures             glad_glGenTextures
#define glDeleteTextures          glad_glDeleteTextures
#define glBindTexture             glad_glBindTexture
#define glTexImage2D              glad_glTexImage2D
#define glCompressedTexImage2D    glad_glCompressedTexImage2D
#define glTexParameteri           glad_glTexParameteri
#define glGenerateMipmap          glad_glGenerateMipmap
#define glActiveTexture           glad_glActiveTexture
#define glGenBuffers              glad_glGenBuffers
#define glDeleteBuffers           glad_glDeleteBuffers
#define glBindBuffer              glad_glBindBuffer
#define glBufferData              glad_glBufferData
#define glGenVertexArrays         glad_glGenVertexArrays
#define glDeleteVertexArrays      glad_glDeleteVertexArrays
#define glBindVertexArray         glad_glBindVertexArray
#define glEnableVertexAttribArray glad_glEnableVertexAttribArray
#define glVertexAttribPointer     glad_glVertexAttribPointer
#define glVertexAttribIPointer    glad_glVertexAttribIPointer
#define glDrawElements            glad_glDrawElements
#define glCullFace                glad_glCullFace
#define glPolygonMode             glad_glPolygonMode
#define glBindBufferBase          glad_glBindBufferBase
#define glCreateShader            glad_glCreateShader
#define glDeleteShader            glad_glDeleteShader
#define glShaderSource            glad_glShaderSource
#define glCompileShader           glad_glCompileShader
#define glGetShaderiv             glad_glGetShaderiv
#define glGetShaderInfoLog        glad_glGetShaderInfoLog
#define glCreateProgram           glad_glCreateProgram
#define glDeleteProgram           glad_glDeleteProgram
#define glAttachShader            glad_glAttachShader
#define glLinkProgram             glad_glLinkProgram
#define glUseProgram              glad_glUseProgram
#define glGetProgramiv            glad_glGetProgramiv
#define glGetProgramInfoLog       glad_glGetProgramInfoLog
#define glGetUniformLocation      glad_glGetUniformLocation
#define glUniform1i               glad_glUniform1i
#define glUniform1f               glad_glUniform1f
#define glUniform1iv              glad_glUniform1iv
#define glUniform1fv              glad_glUniform1fv
#define glUniform4f               glad_glUniform4f
#define glUniform4fv              glad_glUniform4fv
#define glUniform3fv              glad_glUniform3fv
#define glUniformMatrix4fv        glad_glUniformMatrix4fv
#define glDepthMask               glad_glDepthMask
#define glIsEnabled               glad_glIsEnabled
#define glGenFramebuffers         glad_glGenFramebuffers
#define glDeleteFramebuffers      glad_glDeleteFramebuffers
#define glBindFramebuffer         glad_glBindFramebuffer
#define glFramebufferTexture2D    glad_glFramebufferTexture2D
#define glFramebufferRenderbuffer glad_glFramebufferRenderbuffer
#define glCheckFramebufferStatus  glad_glCheckFramebufferStatus
#define glGenRenderbuffers        glad_glGenRenderbuffers
#define glDeleteRenderbuffers     glad_glDeleteRenderbuffers
#define glBindRenderbuffer        glad_glBindRenderbuffer
#define glRenderbufferStorage     glad_glRenderbufferStorage

/* ── Loader ──────────────────────────────────────────────────────────────── */
typedef void* (*GLADloadfunc)(const char* name);
int gladLoadGL(GLADloadfunc load);

#ifdef __cplusplus
}
#endif

#endif /* GLAD_GL_H_ */
