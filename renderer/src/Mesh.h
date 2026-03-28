#pragma once

#include <glm/glm.hpp>

// 40 bytes: vec3 pos (12) + vec3 normal (12) + vec4 color (16)
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec4 color;
};
