#include "Scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

int Scene::AddMesh(MeshType type)
{
    MeshInstance inst{};
    inst.id        = m_nextId++;
    inst.type      = type;
    inst.transform = glm::translate(glm::mat4(1.0f),
                         glm::vec3(static_cast<float>(m_offsetCounter++) * 2.5f, 0.0f, 0.0f));
    m_instances.push_back(inst);
    return inst.id;
}

void Scene::RemoveMesh(int id)
{
    m_instances.erase(
        std::remove_if(m_instances.begin(), m_instances.end(),
                       [id](const MeshInstance& i) { return i.id == id; }),
        m_instances.end());

    if (m_highlightedId == id)
        m_highlightedId = -1;
}

void Scene::SetHighlightedMesh(int id)
{
    m_highlightedId = id;
}

int Scene::GetHighlightedMeshId() const
{
    return m_highlightedId;
}

const std::vector<MeshInstance>& Scene::GetInstances() const
{
    return m_instances;
}

MeshInstance* Scene::GetMutableInstance(int id)
{
    for (auto& inst : m_instances)
        if (inst.id == id) return &inst;
    return nullptr;
}
