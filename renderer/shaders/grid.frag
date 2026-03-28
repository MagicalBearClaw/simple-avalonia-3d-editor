#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec2 fragWorldXZ;

layout(location = 0) out vec4 outColor;

// Returns an anti-aliased line coverage value [0,1] for a world-space grid at
// the given spacing. Uses fwidth() for screen-space derivatives.
float gridLine(vec2 xz, float spacing)
{
    vec2 coord = xz / spacing;
    vec2 grid  = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    float line = min(grid.x, grid.y);
    return 1.0 - min(line, 1.0);
}

void main()
{
    // Camera world position extracted from the inverse view matrix (column 3).
    vec3 camPos = vec3(inverse(ubo.view)[3]);

    // Two-scale grid: 1-unit minor lines and 10-unit major lines.
    float minor = gridLine(fragWorldXZ, 1.0);
    float major = gridLine(fragWorldXZ, 10.0);

    // Base grid colour — minor lines are dimmer, major lines brighter.
    vec3  lineColor = vec3(0.5) * minor * 0.4 + vec3(0.7) * major * 0.8;
    float lineAlpha = max(minor * 0.4, major * 0.8);

    // Axis highlights (higher priority — overwrite base colour).
    // X-axis (where worldZ == 0): red
    if (abs(fragWorldXZ.y) < fwidth(fragWorldXZ.y) * 1.5) {
        lineColor = vec3(1.0, 0.2, 0.2);
        lineAlpha = max(lineAlpha, 0.8);
    }
    // Z-axis (where worldX == 0): blue
    if (abs(fragWorldXZ.x) < fwidth(fragWorldXZ.x) * 1.5) {
        lineColor = vec3(0.2, 0.2, 1.0);
        lineAlpha = max(lineAlpha, 0.8);
    }

    // Distance fade: full opacity up to 50 units; fades to transparent at 100 units.
    float dist = length(camPos.xz - fragWorldXZ);
    float fade = 1.0 - smoothstep(50.0, 100.0, dist);

    float finalAlpha = lineAlpha * fade;
    if (finalAlpha < 0.001)
        discard;

    outColor = vec4(lineColor, finalAlpha);
}
