#pragma once

#include "gl_core_3_3.h"
#include "memory.h"

GLuint load_shader_program(const char* vertex_file, const char* fragment_file, Stack* stack);
