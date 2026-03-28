#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConst {
    mat4  model;
    int   selected;
    float _pad[3];
} push;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    gl_Position = ubo.proj * ubo.view * push.model * vec4(inPos, 1.0);
    fragColor   = inColor;
}
