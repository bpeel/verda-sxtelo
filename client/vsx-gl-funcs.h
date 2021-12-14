/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2014, 2021  Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This header is included multiple times with different definitions
 * of the VSX_GL_FUNC macro
 */

/* The VSX_GL_BEGIN_GROUP macro looks like this:
 * VSX_GL_BEGIN_GROUP(minimum GL version,
 *                   extension,
 *                   extension function suffix)
 * If the GL version if at least the minimum then the function is
 * assumed to be available with no suffix. Otherwise it will check for
 * the extension and append the function suffix to all function names.
 *
 * The GL version is given as the real GL version multiplied by 10 and
 * converted to an integer. Eg, 2.1 would be 21.
 */

/* Core functions that we can't do without */
VSX_GL_BEGIN_GROUP(00,
                   NULL,
                   NULL)
VSX_GL_FUNC(void,
            glAttachShader, (GLuint program, GLuint shader))
VSX_GL_FUNC(void,
            glBindAttribLocation, (GLuint program, GLuint index,
                                   const GLchar *name))
VSX_GL_FUNC(void,
            glBindBuffer, (GLenum target, GLuint buffer))
VSX_GL_FUNC(void,
            glBindTexture, (GLenum target, GLuint texture))
VSX_GL_FUNC(void,
            glBlendFunc, (GLenum sfactor, GLenum dfactor))
VSX_GL_FUNC(void,
            glBufferData, (GLenum target, GLsizeiptr size,
                           const void *data, GLenum usage))
VSX_GL_FUNC(void,
            glBufferSubData, (GLenum target, GLintptr offset,
                              GLsizeiptr size, const void *data))
VSX_GL_FUNC(void,
            glClear, (GLbitfield mask))
VSX_GL_FUNC(void,
            glCompileShader, (GLuint shader))
VSX_GL_FUNC(GLuint,
            glCreateProgram, (void))
VSX_GL_FUNC(GLuint,
            glCreateShader, (GLenum type))
VSX_GL_FUNC(void,
            glDeleteBuffers, (GLsizei n, const GLuint *buffers))
VSX_GL_FUNC(void,
            glDeleteProgram, (GLuint program))
VSX_GL_FUNC(void,
            glDeleteShader, (GLuint shader))
VSX_GL_FUNC(void,
            glDeleteTextures, (GLsizei n, const GLuint *textures))
VSX_GL_FUNC(void,
            glDisable, (GLenum cap))
VSX_GL_FUNC(void,
            glDisableVertexAttribArray, (GLuint index))
VSX_GL_FUNC(void,
            glDrawArrays, (GLenum mode, GLint first, GLsizei count))
VSX_GL_FUNC(void,
            glDrawElements, (GLenum mode, GLsizei count, GLenum type,
                             const GLvoid *indices))
VSX_GL_FUNC(void,
            glEnable, (GLenum cap))
VSX_GL_FUNC(void,
            glEnableVertexAttribArray, (GLuint index))
VSX_GL_FUNC(void,
            glGenBuffers, (GLsizei n, GLuint *buffers))
VSX_GL_FUNC(GLint,
            glGetAttribLocation, (GLuint program, const GLchar *name))
VSX_GL_FUNC(void,
            glGetIntegerv, (GLenum pname, GLint *params))
VSX_GL_FUNC(void,
            glGenTextures, (GLsizei n, GLuint *textures))
VSX_GL_FUNC(void,
            glGetProgramInfoLog, (GLuint program, GLsizei bufSize,
                                  GLsizei *length, GLchar *infoLog))
VSX_GL_FUNC(void,
            glGetProgramiv, (GLuint program, GLenum pname, GLint *params))
VSX_GL_FUNC(void,
            glGetShaderInfoLog, (GLuint shader, GLsizei bufSize,
                                 GLsizei *length, GLchar *infoLog))
VSX_GL_FUNC(void,
            glGetShaderiv, (GLuint shader, GLenum pname, GLint *params))
VSX_GL_FUNC(const GLubyte *,
            glGetString, (GLenum name))
VSX_GL_FUNC(GLint,
            glGetUniformLocation, (GLuint program, const GLchar *name))
VSX_GL_FUNC(void,
            glLinkProgram, (GLuint program))
VSX_GL_FUNC(void,
            glShaderSource, (GLuint shader, GLsizei count,
                             const GLchar *const*string, const GLint *length))
VSX_GL_FUNC(void,
            glTexImage2D, (GLenum target, GLint level,
                           GLint internalFormat,
                           GLsizei width, GLsizei height,
                           GLint border, GLenum format, GLenum type,
                           const GLvoid *pixels))
VSX_GL_FUNC(void,
            glTexSubImage2D, (GLenum target, GLint level,
                              GLint xoffset, GLint yoffset,
                              GLsizei width, GLsizei height,
                              GLenum format, GLenum type,
                              const GLvoid *pixel))
VSX_GL_FUNC(void,
            glTexParameteri, (GLenum target, GLenum pname, GLint param))
VSX_GL_FUNC(void,
            glUniform1i, (GLint location, GLint v0))
VSX_GL_FUNC(void,
            glUniform1f, (GLint location, GLfloat v0))
VSX_GL_FUNC(void,
            glUniform2f, (GLint location, GLfloat v0, GLfloat v1))
VSX_GL_FUNC(void,
            glUniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2))
VSX_GL_FUNC(void,
            glUniformMatrix4fv, (GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat *value))
VSX_GL_FUNC(void,
            glUniformMatrix3fv, (GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat *value))
VSX_GL_FUNC(void,
            glUseProgram, (GLuint program))
VSX_GL_FUNC(void,
            glVertexAttribPointer, (GLuint index, GLint size,
                                    GLenum type, GLboolean normalized,
                                    GLsizei stride, const void *pointer))
VSX_GL_FUNC(void,
            glViewport, (GLint x, GLint y,
                         GLsizei width, GLsizei height))
VSX_GL_FUNC(GLboolean,
            glIsBuffer, (GLuint buffer))
VSX_GL_FUNC(GLboolean,
            glIsTexture, (GLuint buffer))
VSX_GL_FUNC(GLboolean,
            glIsShader, (GLuint buffer))
VSX_GL_FUNC(GLboolean,
            glIsProgram, (GLuint buffer))
VSX_GL_END_GROUP()

/* Map buffer range */
VSX_GL_BEGIN_GROUP(30,
                   "GL_EXT_map_buffer_range",
                   "EXT")
VSX_GL_FUNC(void,
           glFlushMappedBufferRange, (GLenum target, GLintptr offset,
                                      GLsizei length))
VSX_GL_FUNC(void *,
           glMapBufferRange, (GLenum target, GLintptr offset,
                              GLsizeiptr length, GLbitfield access))
VSX_GL_FUNC(GLboolean,
           glUnmapBuffer, (GLenum target))
VSX_GL_END_GROUP()

/* Vertex array objects */
VSX_GL_BEGIN_GROUP(30,
                   "GL_OES_vertex_array_object",
                   "OES")
VSX_GL_FUNC(void,
            glBindVertexArray, (GLuint array))
VSX_GL_FUNC(void,
            glDeleteVertexArrays, (GLsizei n, const GLuint *arrays))
VSX_GL_FUNC(void,
            glGenVertexArrays, (GLsizei n, GLuint *arrays))
VSX_GL_FUNC(GLboolean,
            glIsVertexArray, (GLuint buffer))
VSX_GL_END_GROUP()

/* Instanced arrays */
VSX_GL_BEGIN_GROUP(30,
                   "GL_ANGLE_instanced_arrays",
                   "ANGLE")
VSX_GL_FUNC(void,
            glDrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type,
                                      const void *indices,
                                      GLsizei instancecount))
VSX_GL_FUNC(void,
            glVertexAttribDivisor, (GLuint index, GLuint divisor))
VSX_GL_END_GROUP()

/* FBOs. This is only used for generating mipmaps */
VSX_GL_BEGIN_GROUP(0, NULL, NULL)
VSX_GL_FUNC(void,
            glGenerateMipmap, (GLenum target))
VSX_GL_END_GROUP()

/* 3D textures (used for 2D array textures) */
VSX_GL_BEGIN_GROUP(30,
                   "GL_OES_texture_3D",
                   "OES")
VSX_GL_FUNC(void,
            glTexImage3D, (GLenum target, GLint level,
                           GLint internalFormat,
                           GLsizei width, GLsizei height,
                           GLsizei depth, GLint border,
                           GLenum format, GLenum type,
                           const GLvoid *pixels ))
VSX_GL_FUNC(void,
            glTexSubImage3D, (GLenum target, GLint level,
                              GLint xoffset, GLint yoffset,
                              GLint zoffset, GLsizei width,
                              GLsizei height, GLsizei depth,
                              GLenum format,
                              GLenum type, const GLvoid *pixels))
VSX_GL_END_GROUP()

/* Draw range elements is not available in GLES 2 */
VSX_GL_BEGIN_GROUP(30,
                   NULL,
                   NULL)
VSX_GL_FUNC(void,
            glDrawRangeElements, (GLenum mode, GLuint start,
                                  GLuint end, GLsizei count, GLenum type,
                                  const GLvoid *indices))
VSX_GL_END_GROUP()
