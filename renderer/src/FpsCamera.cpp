#include "FpsCamera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

static float toRad(float deg) { return glm::radians(deg); }

glm::vec3 FpsCamera::GetFront() const
{
    return glm::normalize(glm::vec3{
        std::cos(toRad(yaw)) * std::cos(toRad(pitch)),
        std::sin(toRad(pitch)),
        std::sin(toRad(yaw)) * std::cos(toRad(pitch))
    });
}

glm::mat4 FpsCamera::GetViewMatrix() const
{
    return glm::lookAt(position, position + GetFront(), kWorldUp);
}

glm::mat4 FpsCamera::GetProjectionMatrix(float fovDeg, float aspect,
                                         float nearPlane, float farPlane) const
{
    glm::mat4 proj = glm::perspective(glm::radians(fovDeg), aspect, nearPlane, farPlane);
    proj[1][1] *= -1.0f; // Vulkan NDC: flip Y
    return proj;
}

void FpsCamera::ProcessMouseDelta(float dx, float dy, float sensitivity)
{
    yaw   += dx * sensitivity;
    pitch -= dy * sensitivity;
    pitch  = std::clamp(pitch, -89.0f, 89.0f);
}

void FpsCamera::ProcessKeyboard(bool w, bool s, bool a, bool d,
                                float deltaTime, float speed)
{
    const glm::vec3 front = GetFront();
    const glm::vec3 right = glm::normalize(glm::cross(front, kWorldUp));
    const float     vel   = speed * deltaTime;

    if (w) position += front * vel;
    if (s) position -= front * vel;
    if (a) position -= right * vel;
    if (d) position += right * vel;
}
