#pragma once
#include <GL/glew.h>
namespace mjgs {
// Compatibility-profile attribute stack covers depth/blend/scissor/viewport/sRGB.
// Modern bindings are not on that stack and must be saved explicitly.
class GlState {
 public:
  GlState() {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao_);
    glGetIntegerv(GL_CLIP_ORIGIN, &origin_);
    glGetIntegerv(GL_CLIP_DEPTH_MODE, &clip_);
    for (int i=0; i<5; ++i) glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &ssbo_[i]);
    glGetIntegeri_v(GL_ATOMIC_COUNTER_BUFFER_BINDING, 4, &atomic4_);
    for (int i=0; i<6; ++i) glGetIntegerv(queries_[i], &bindings_[i]);
  }
  ~GlState() {
    glUseProgram(program_);
    glBindVertexArray(vao_);
    for (int i=0; i<5; ++i) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, ssbo_[i]);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 4, atomic4_);
    for (int i=0; i<6; ++i) glBindBuffer(targets_[i], bindings_[i]);
    glClipControl(origin_, clip_);
    glPopAttrib();
  }
  GlState(const GlState&) = delete;
  GlState& operator=(const GlState&) = delete;
 private:
  GLint program_, vao_, origin_, clip_, ssbo_[5], atomic4_, bindings_[6];
  const GLenum queries_[6] = {GL_ARRAY_BUFFER_BINDING, GL_COPY_READ_BUFFER_BINDING,
    GL_COPY_WRITE_BUFFER_BINDING, GL_SHADER_STORAGE_BUFFER_BINDING,
    GL_ATOMIC_COUNTER_BUFFER_BINDING, GL_DISPATCH_INDIRECT_BUFFER_BINDING};
  const GLenum targets_[6] = {GL_ARRAY_BUFFER, GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
    GL_SHADER_STORAGE_BUFFER, GL_ATOMIC_COUNTER_BUFFER, GL_DISPATCH_INDIRECT_BUFFER};
};
}
