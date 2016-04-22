#version 330

uniform sampler2D texture;
uniform vec2 texture_size;
uniform vec2 input_size;
uniform vec2 output_size;

in vec2 texture_texcoord;

layout(location = 0) out vec4 output_colour;

void main()
{
    vec3 result;

    const float fringing_strength = 1.0;

    vec3 center = texture2D(texture, texture_texcoord).xyz;

    vec2 offset = 1.0 / (texture_size * (output_size / input_size));
    float i_sample = texture2D(texture, texture_texcoord + vec2(offset.x, offset.y)).y;
    float i = mix(center.y, i_sample, fringing_strength);
    float q_sample = texture2D(texture, texture_texcoord - vec2(offset.x, offset.y)).z;
    float q = mix(center.z, q_sample, fringing_strength);
    result = vec3(center.x, i, q);
    
    output_colour = vec4(result, 1.0);
}
