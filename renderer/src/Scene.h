#pragma once

#include "Mesh.h"

#include <glm/glm.hpp>
#include <vector>

// Plain data — no selection or rendering knowledge.
struct MeshInstance {
    int       id;
    MeshType  type;
    glm::mat4 transform;
};

class Scene {
public:
    // Create a new instance of the given primitive type, placed at
    // (offsetCounter * 2.5, 0, 0).  Returns the new instance ID.
    int  AddMesh(MeshType type);

    // Remove instance by ID.  Auto-clears highlight if that mesh was highlighted.
    void RemoveMesh(int id);

    // Tell the renderer which mesh should render with the highlight push-constant.
    // Pass -1 to clear.
    void SetHighlightedMesh(int id);
    int  GetHighlightedMeshId() const;

    const std::vector<MeshInstance>& GetInstances() const;

private:
    std::vector<MeshInstance> m_instances;
    int m_nextId        = 0;
    int m_offsetCounter = 0;
    int m_highlightedId = -1;
};
