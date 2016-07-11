#include "gl_shader.h"

#include "logging.h"
#include "memory.h"
#include "asset_handling.h"
#include "string_utilities.h"
#include "assert.h"

const char* default_vertex_source = R"(
#version 330

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

uniform mat4x4 model_view_projection;

out vec2 texture_texcoord;

void main()
{
    texture_texcoord = texcoord;
    gl_Position = model_view_projection * vec4(position.x, position.y, 1.0, 1.0);
}
)";

const char* default_fragment_source = R"(
#version 330

uniform sampler2D texture;

in vec2 texture_texcoord;

layout(location = 0) out vec4 output_colour;

void main()
{
    output_colour = texture2D(texture, texture_texcoord);
}
)";

static GLuint load_shader_from_source(GLenum type, const char* source, GLint source_size) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, &source_size);
    glCompileShader(shader);

    // Output any errors if the compilation failed.

    GLint compile_status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
    if (compile_status == GL_FALSE) {
        GLint info_log_size = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_size);
        if (info_log_size > 0) {
            GLchar* info_log = ALLOCATE(GLchar, info_log_size);
            GLsizei bytes_written = 0;
            glGetShaderInfoLog(shader, info_log_size, &bytes_written, info_log);
            LOG_ERROR("Couldn't compile the shader.\n%s", info_log);
            DEALLOCATE(info_log);
        }

        glDeleteShader(shader);

        return 0;
    }

    return shader;
}

static GLuint load_shader_from_file(GLenum type, const char* path) {
    GLuint shader;

    // Load the shader source code from a file.

    s64 char_count;
    void* chars;
    bool loaded = load_whole_file(FileType::Asset_Shader, path, &chars, &char_count);
    if (!loaded) {
        LOG_ERROR("Couldn't load the shader source file.");
        return 0;
    }
    ASSERT(chars && char_count > 0);
    GLchar* shader_source = static_cast<GLchar*>(chars);
    GLint shader_source_size = char_count;

    shader = load_shader_from_source(type, shader_source, shader_source_size);
    DEALLOCATE(shader_source);

    return shader;
}

GLuint load_shader_program(const char* vertex_file, const char* fragment_file) {
    GLuint program;

    GLuint vertex_shader;
    if (vertex_file) {
        vertex_shader = load_shader_from_file(GL_VERTEX_SHADER, vertex_file);
    } else {
        vertex_shader = load_shader_from_source(GL_VERTEX_SHADER, default_vertex_source, string_size(default_vertex_source));
    }
    if (vertex_shader == 0) {
        LOG_ERROR("Failed to load the vertex shader %s.", vertex_file);
        return 0;
    }

    GLuint fragment_shader;
    if (fragment_file) {
        fragment_shader = load_shader_from_file(GL_FRAGMENT_SHADER, fragment_file);
    } else {
        fragment_shader = load_shader_from_source(GL_FRAGMENT_SHADER, default_fragment_source, string_size(default_fragment_source));
    }
    if (fragment_shader == 0) {
        LOG_ERROR("Failed to load the fragment shader %s.", fragment_file);
        glDeleteShader(vertex_shader);
        return 0;
    }

    // Create the program object and link the shaders to it.

    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    // Check if linking failed and output any errors.

    GLint link_status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if (link_status == GL_FALSE) {
        int info_log_size = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_size);
        if (info_log_size > 0) {
            GLchar* info_log = ALLOCATE(GLchar, info_log_size);
            GLsizei bytes_written = 0;
            glGetProgramInfoLog(program, info_log_size, &bytes_written, info_log);
            LOG_ERROR("Couldn't link the shader program (%s, %s).\n%s", vertex_file, fragment_file, info_log);
            DEALLOCATE(info_log);
        }

        glDeleteProgram(program);
        glDeleteShader(fragment_shader);
        glDeleteShader(vertex_shader);

        return 0;
    }

    // Shaders are no longer needed after the program object is linked.
    glDetachShader(program, vertex_shader);
    glDetachShader(program, fragment_shader);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return program;
}
