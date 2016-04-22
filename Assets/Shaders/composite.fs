#version 330

uniform sampler2D texture;
uniform sampler2D dot_crawl_texture;
uniform vec2 texture_size;
uniform vec2 input_size;
uniform vec2 output_size;
uniform float frame_count; // cycles 0, 1, 2 repeatedly

in vec2 texture_texcoord;

layout(location = 0) out vec4 output_colour;

const float tau = 6.2831853071;
const mat3 YIQ_TO_RGB = mat3(1.0,    1.0,    1.0,
                             0.956, -0.272, -1.106,
                             0.621, -0.647,  1.703);
vec3 yiq_to_rgb(vec3 yiq)
{
    return YIQ_TO_RGB * yiq;
}

vec3 dot_crawl()
{
    vec3 result;
    vec2 texcoord = texture_texcoord * texture_size;
    texcoord.y += frame_count;
    result = texture2D(dot_crawl_texture, texcoord / 3.0).xyz;
    return result;
}

void main()
{
    vec3 result = vec3(0.0);

    const float radius = 6.0;
    const float sigmas[3] = float[3](1.0, 2.0, 8.0);
    const float dot_crawl_strength = 0.7;
    const float fringing_strength = 0.5;

    float coefficient_sums[3] = float[3](0.0, 0.0, 0.0);
    vec3 incremental_gaussian[3];
    for (int i = 0; i < 3; ++i)
    {
        float sigma = sigmas[i];
        incremental_gaussian[i].x = 1.0 / (sqrt(tau) * sigma);
        float y = exp(-0.5 / (sigma * sigma));
        incremental_gaussian[i].y = y;
        incremental_gaussian[i].z = y * y;
    }

    vec3 center = texture2D(texture, texture_texcoord).xyz;
    for (int i = 0; i < 3; ++i)
    {
        result[i] += center[i] * incremental_gaussian[i].x;
        coefficient_sums[i] += incremental_gaussian[i].x;
        incremental_gaussian[i].xy *= incremental_gaussian[i].yz;
    }

    float blur_size = 1.0 / (texture_size.x * (output_size.x / input_size.x));

    vec3 adjusted_dot_crawl = dot_crawl_strength * dot_crawl();

    for (float i = 1.0; i <= radius; ++i)
    {
        vec2 offset = vec2(i * blur_size, 0.0);
        vec3 right = texture2D(texture, texture_texcoord + offset).xyz;
        vec3 left = texture2D(texture, texture_texcoord - offset).xyz;
        for (int j = 0; j < 3; ++j)
        {
            result[j] += right[j] * incremental_gaussian[j].x;
            result[j] += left[j] * incremental_gaussian[j].x;
            result[j] += adjusted_dot_crawl[j] * ((left[j] - center[j]) + (right[j] - center[j])) * incremental_gaussian[j].x;
            coefficient_sums[j] += 2.0 * incremental_gaussian[j].x;
            incremental_gaussian[j].xy *= incremental_gaussian[j].yz;
        }
    }
    result.x /= coefficient_sums[0];
    result.y /= coefficient_sums[1];
    result.z /= coefficient_sums[2];

    result = clamp(yiq_to_rgb(result), 0.0, 1.0);
    output_colour = vec4(result, 1.0);
}

