#version 330

uniform sampler2D texture;
uniform vec2 texture_size;
uniform float sigma; // standard deviation
uniform float radius;

in vec2 texture_texcoord;

layout(location = 0) out vec4 output_colour;

const float tau = 6.2831853071;

void main()
{
    vec3 result;

    vec3 incremental_gaussian;
    incremental_gaussian.x = 1.0 / (sqrt(tau) * sigma);
    incremental_gaussian.y = exp(-0.5 / (sigma * sigma));
    incremental_gaussian.z = incremental_gaussian.y * incremental_gaussian.y;

    float blur_size = 1.0 / texture_size.x;

    float coefficient_sum = 0.0;
    for (float i = 0; i < radius; ++i)
    {
        vec2 offset = vec2(i * blur_size, 0.0);
        result += texture2D(texture, texture_texcoord + offset).xyz * incremental_gaussian.x;
        result += texture2D(texture, texture_texcoord - offset).xyz * incremental_gaussian.x;
        coefficient_sum += 2.0 * incremental_gaussian.x;
        incremental_gaussian.xy *= incremental_gaussian.yz;
    }
    result /= coefficient_sum;

    output_colour = vec4(result, 1.0);
}

