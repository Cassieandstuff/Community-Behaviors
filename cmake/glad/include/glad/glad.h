/* Redirect glad1-style includes to our gl.h.
   ImGui detects glad/glad.h before glad/gl.h, so it uses GLAD (v1) path
   which just calls the macros from the included header — no GladGLContext needed. */
#include <glad/gl.h>
