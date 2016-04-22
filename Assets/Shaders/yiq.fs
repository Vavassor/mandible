#version 330

uniform sampler2D texture;

in vec2 texture_texcoord;

layout(location = 0) out vec4 output_colour;

const mat3 RGB_TO_YIQ = mat3(0.299,  0.596,  0.211,
                             0.587, -0.274, -0.523,
                             0.114, -0.322,  0.312);
vec3 rgb_to_yiq(vec3 rgb)
{
    return RGB_TO_YIQ * rgb;
}

void main()
{
    vec4 result = texture2D(texture, texture_texcoord);
    result.rgb = rgb_to_yiq(result.rgb);
    output_colour = result;
}

