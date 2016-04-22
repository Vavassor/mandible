#include "gl_shader.h"

#include "logging.h"

#include <cstdio>
#include <cstdlib>

#define STACK_ALLOCATE(type, count) \
    static_cast<type*>(alloca(sizeof(type) * (count)))

#define HEAP_ALLOCATE(type, count) \
    static_cast<type*>(std::malloc(sizeof(type) * (count)))

#define HEAP_DEALLOCATE(memory) \
    std::free(memory)

static std::size_t string_size(const char* string) {
    const char* s;
    for (s = string; *s; ++s);
    return s - string;
}

const char* default_vertex_source = R"END(
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
)END";

const char* default_fragment_source = R"END(
#version 330

uniform sampler2D texture;

in vec2 texture_texcoord;

layout(location = 0) out vec4 output_colour;

void main()
{
    output_colour = texture2D(texture, texture_texcoord);
}
)END";

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
            GLchar* info_log = STACK_ALLOCATE(GLchar, info_log_size);
            GLsizei bytes_written = 0;
            glGetShaderInfoLog(shader, info_log_size, &bytes_written, info_log);
            LOG_ERROR("Couldn't compile the shader.\n%s", info_log);
        }

        glDeleteShader(shader);

        return 0;
    }

    return shader;
}

static char* load_text_file(const char* filename, long* size) {
    char* buffer;

    std::FILE* file = std::fopen(filename, "r");
    if (!file) {
        LOG_ERROR("Couldn't open the file.");
        return nullptr;
    }

    std::fseek(file, 0, SEEK_END);
    long char_count = std::ftell(file);
    std::rewind(file);

    buffer = HEAP_ALLOCATE(char, char_count + 1);
    if (!buffer) {
        LOG_ERROR("Failed to allocate memory for reading the file.");
        std::fclose(file);
        return nullptr;
    }
    buffer[char_count] = '\0';

    std::size_t bytes_read = std::fread(buffer, 1, char_count, file);
    if (bytes_read != char_count) {
        LOG_ERROR("Reading the file failed.");
        HEAP_DEALLOCATE(buffer);
        std::fclose(file);
        return nullptr;
    }

    std::fclose(file);

    *size = char_count;
    return buffer;
}

static GLuint load_shader_from_file(GLenum type, const char* filename) {
    GLuint shader;

    // Load the shader source code from a file.

    long char_count = 0;
    GLchar* shader_source = load_text_file(filename, &char_count);
    if (!shader_source) {
        LOG_ERROR("Couldn't load the shader source file.");
        return 0;
    }
    GLint shader_source_size = char_count;

    shader = load_shader_from_source(type, shader_source, shader_source_size);
    HEAP_DEALLOCATE(shader_source);

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
            GLchar* info_log = STACK_ALLOCATE(GLchar, info_log_size);
            GLsizei bytes_written = 0;
            glGetProgramInfoLog(program, info_log_size, &bytes_written, info_log);
            LOG_ERROR("Couldn't link the shader program (%s, %s).\n%s", vertex_file, fragment_file, info_log);
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
