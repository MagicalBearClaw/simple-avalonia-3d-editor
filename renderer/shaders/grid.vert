#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec2 fragWorldXZ;

void main()
{
    // Generate a ±500 unit XZ quad (y=0) from gl_VertexIndex — no vertex buffer needed.
    // Two CCW triangles: indices 0-2 and 3-5.
    const vec2 corners[4] = vec2[4](
        vec2(-500.0, -500.0),
        vec2( 500.0, -500.0),
        vec2( 500.0,  500.0),
        vec2(-500.0,  500.0)
    );
    const int indices[6] = int[6](0, 1, 2, 0, 2, 3);

    vec2 xz = corners[indices[gl_VertexIndex]];
    fragWorldXZ = xz;

    gl_Position = ubo.proj * ubo.view * vec4(xz.x, 0.0, xz.y, 1.0);
}
