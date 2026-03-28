#pragma once

#include <glm/glm.hpp>

class FpsCamera {
public:
    glm::vec3 position{0.0f, 2.0f, 8.0f};
    float     yaw   = -90.0f;   // facing -Z toward origin
    float     pitch =   0.0f;

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float fovDeg, float aspect, float nearPlane, float farPlane) const;

    // dx/dy are raw pixel deltas (e.g. from mouse move events in FPS mode).
    void ProcessMouseDelta(float dx, float dy, float sensitivity = 0.1f);

    // Move along the camera front/right vectors.  deltaTime in seconds.
    void ProcessKeyboard(bool w, bool s, bool a, bool d, float deltaTime, float speed = 5.0f);

    // Helper used internally and by Renderer for scroll-to-zoom.
    glm::vec3 GetFront() const;

private:
    static constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
};
