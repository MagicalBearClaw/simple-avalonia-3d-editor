#include "MeshGenerator.h"
#include "VulkanContext.h"

#define _USE_MATH_DEFINES
#include <cmath>
#include <stdexcept>
#include <cstring>
#include <initializer_list>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// MeshAsset — GPU upload / destroy
// ---------------------------------------------------------------------------

static VkBuffer CreateStagingBuffer(VulkanContext& ctx, VkDeviceSize size,
                                    VmaAllocation& outAlloc, void*& outMapped)
{
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo info{};
    VkBuffer buf = VK_NULL_HANDLE;
    if (vmaCreateBuffer(ctx.allocator, &bci, &aci, &buf, &outAlloc, &info) != VK_SUCCESS)
        throw std::runtime_error("MeshAsset::Upload — failed to create staging buffer");

    outMapped = info.pMappedData;
    return buf;
}

static VkBuffer CreateDeviceBuffer(VulkanContext& ctx, VkDeviceSize size,
                                   VkBufferUsageFlags usage, VmaAllocation& outAlloc)
{
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkBuffer buf = VK_NULL_HANDLE;
    if (vmaCreateBuffer(ctx.allocator, &bci, &aci, &buf, &outAlloc, nullptr) != VK_SUCCESS)
        throw std::runtime_error("MeshAsset::Upload — failed to create device buffer");
    return buf;
}

void MeshAsset::Upload(VulkanContext& ctx)
{
    const VkDeviceSize vertSize = vertices.size() * sizeof(Vertex);
    const VkDeviceSize idxSize  = indices.size()  * sizeof(uint32_t);

    // Staging buffers (CPU-visible, mapped)
    VmaAllocation stagingVertAlloc, stagingIdxAlloc;
    void* stagingVertPtr = nullptr;
    void* stagingIdxPtr  = nullptr;

    VkBuffer stagingVert = CreateStagingBuffer(ctx, vertSize, stagingVertAlloc, stagingVertPtr);
    VkBuffer stagingIdx  = CreateStagingBuffer(ctx, idxSize,  stagingIdxAlloc,  stagingIdxPtr);

    std::memcpy(stagingVertPtr, vertices.data(), vertSize);
    std::memcpy(stagingIdxPtr,  indices.data(),  idxSize);

    // Device-local buffers
    vertexBuffer = CreateDeviceBuffer(ctx, vertSize,
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexAlloc);
    indexBuffer  = CreateDeviceBuffer(ctx, idxSize,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  indexAlloc);

    // One-shot transfer command buffer
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = ctx.commandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx.device, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkBufferCopy region{};
    region.size = vertSize;
    vkCmdCopyBuffer(cmd, stagingVert, vertexBuffer, 1, &region);

    region.size = idxSize;
    vkCmdCopyBuffer(cmd, stagingIdx, indexBuffer, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(ctx.graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphicsQueue);

    vkFreeCommandBuffers(ctx.device, ctx.commandPool, 1, &cmd);
    vmaDestroyBuffer(ctx.allocator, stagingVert, stagingVertAlloc);
    vmaDestroyBuffer(ctx.allocator, stagingIdx,  stagingIdxAlloc);
}

void MeshAsset::Destroy(VulkanContext& ctx)
{
    if (vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx.allocator, vertexBuffer, vertexAlloc);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx.allocator, indexBuffer, indexAlloc);
        indexBuffer = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Internal geometry helpers
// ---------------------------------------------------------------------------

static void AddTriangle(MeshAsset& m, Vertex v0, Vertex v1, Vertex v2)
{
    uint32_t base = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back(v0);
    m.vertices.push_back(v1);
    m.vertices.push_back(v2);
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2});
}

// Adds a CCW quad as two triangles (0,1,2) and (0,2,3).
static void AddQuad(MeshAsset& m, Vertex v0, Vertex v1, Vertex v2, Vertex v3)
{
    uint32_t base = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back(v0);
    m.vertices.push_back(v1);
    m.vertices.push_back(v2);
    m.vertices.push_back(v3);
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2,
                                        base, base + 2, base + 3});
}

// ---------------------------------------------------------------------------
// GenerateCube
// ---------------------------------------------------------------------------
// 24 vertices (4 per face × 6 faces), 36 indices.
// All faces use per-face normals; each face has one distinct color.
// Winding:  CCW when viewed from outside (verified via cross-product check).
// ---------------------------------------------------------------------------
MeshAsset MeshGenerator::GenerateCube()
{
    MeshAsset m;

    const glm::vec4 red     = {1.0f, 0.2f, 0.2f, 1.0f};
    const glm::vec4 green   = {0.2f, 1.0f, 0.2f, 1.0f};
    const glm::vec4 blue    = {0.2f, 0.4f, 1.0f, 1.0f};
    const glm::vec4 yellow  = {1.0f, 1.0f, 0.2f, 1.0f};
    const glm::vec4 magenta = {1.0f, 0.2f, 1.0f, 1.0f};
    const glm::vec4 cyan    = {0.2f, 1.0f, 1.0f, 1.0f};

    // +X face (red) — normal (1,0,0)
    {
        const glm::vec3 n{1, 0, 0};
        AddQuad(m,
            {{1,-1,-1}, n, red}, {{1, 1,-1}, n, red},
            {{1, 1, 1}, n, red}, {{1,-1, 1}, n, red});
    }
    // -X face (green) — normal (-1,0,0)
    {
        const glm::vec3 n{-1, 0, 0};
        AddQuad(m,
            {{-1,-1, 1}, n, green}, {{-1, 1, 1}, n, green},
            {{-1, 1,-1}, n, green}, {{-1,-1,-1}, n, green});
    }
    // +Y face (blue) — normal (0,1,0)
    {
        const glm::vec3 n{0, 1, 0};
        AddQuad(m,
            {{-1, 1,-1}, n, blue}, {{-1, 1, 1}, n, blue},
            {{ 1, 1, 1}, n, blue}, {{ 1, 1,-1}, n, blue});
    }
    // -Y face (yellow) — normal (0,-1,0)
    {
        const glm::vec3 n{0, -1, 0};
        AddQuad(m,
            {{-1,-1,-1}, n, yellow}, {{ 1,-1,-1}, n, yellow},
            {{ 1,-1, 1}, n, yellow}, {{-1,-1, 1}, n, yellow});
    }
    // +Z face (magenta) — normal (0,0,1)
    {
        const glm::vec3 n{0, 0, 1};
        AddQuad(m,
            {{-1,-1, 1}, n, magenta}, {{ 1,-1, 1}, n, magenta},
            {{ 1, 1, 1}, n, magenta}, {{-1, 1, 1}, n, magenta});
    }
    // -Z face (cyan) — normal (0,0,-1)
    {
        const glm::vec3 n{0, 0, -1};
        AddQuad(m,
            {{ 1,-1,-1}, n, cyan}, {{-1,-1,-1}, n, cyan},
            {{-1, 1,-1}, n, cyan}, {{ 1, 1,-1}, n, cyan});
    }

    return m;
}

// ---------------------------------------------------------------------------
// GenerateSphere
// ---------------------------------------------------------------------------
// (stacks+1)*(slices+1) vertices, stacks*slices*2 triangles.
// Per-vertex normals = normalised position (smooth shading).
// Checkerboard: (stack+slice)%2 selects orange or off-white.
// Winding: CCW from outside — use (v00,v11,v10) + (v00,v01,v11).
// ---------------------------------------------------------------------------
MeshAsset MeshGenerator::GenerateSphere(int stacks, int slices)
{
    MeshAsset m;

    const glm::vec4 colorA = {1.0f, 0.55f, 0.0f, 1.0f}; // orange
    const glm::vec4 colorB = {0.9f, 0.9f,  0.9f, 1.0f}; // off-white

    const float pi = static_cast<float>(M_PI);

    auto makeVert = [&](int i, int j, const glm::vec4& color) -> Vertex {
        float phi   = pi * i / stacks;
        float theta = 2.0f * pi * j / slices;
        glm::vec3 pos{
            std::sin(phi) * std::cos(theta),
            std::cos(phi),
            std::sin(phi) * std::sin(theta)
        };
        return {pos, glm::normalize(pos), color};
    };

    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            const glm::vec4& color = ((i + j) % 2 == 0) ? colorA : colorB;

            Vertex v00 = makeVert(i,   j,   color);
            Vertex v01 = makeVert(i,   j+1, color);
            Vertex v10 = makeVert(i+1, j,   color);
            Vertex v11 = makeVert(i+1, j+1, color);

            // CCW winding for outward normals
            AddTriangle(m, v00, v11, v10);
            AddTriangle(m, v00, v01, v11);
        }
    }

    return m;
}

// ---------------------------------------------------------------------------
// GeneratePyramid
// ---------------------------------------------------------------------------
// 4 triangular sides (apex duplicated per face for flat normals) + 1 quad base.
// Colors: front=red, right=green, back=blue, left=yellow, base=gray.
// ---------------------------------------------------------------------------
MeshAsset MeshGenerator::GeneratePyramid()
{
    MeshAsset m;

    const glm::vec4 colors[5] = {
        {1.0f, 0.2f, 0.2f, 1.0f}, // front  (+Z side)
        {0.2f, 1.0f, 0.2f, 1.0f}, // right  (+X side)
        {0.2f, 0.4f, 1.0f, 1.0f}, // back   (-Z side)
        {1.0f, 1.0f, 0.2f, 1.0f}, // left   (-X side)
        {0.6f, 0.6f, 0.6f, 1.0f}, // base   (-Y)
    };

    const glm::vec3 apex{0.0f, 0.5f, 0.0f};
    const glm::vec3 bl[4] = {
        {-0.5f, -0.5f, -0.5f}, // 0: back-left
        { 0.5f, -0.5f, -0.5f}, // 1: back-right
        { 0.5f, -0.5f,  0.5f}, // 2: front-right
        {-0.5f, -0.5f,  0.5f}, // 3: front-left
    };

    // Helper: add a side triangle with auto-computed face normal
    auto addSide = [&](int ci, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        AddTriangle(m, {a, n, colors[ci]}, {b, n, colors[ci]}, {c, n, colors[ci]});
    };

    // Front (+Z): apex, bl[3], bl[2]  → cross((bl[3]-apex),(bl[2]-apex)) → +Z,+Y  ✓
    addSide(0, apex, bl[3], bl[2]);
    // Right (+X): apex, bl[2], bl[1]  → cross gives +X,+Y  ✓
    addSide(1, apex, bl[2], bl[1]);
    // Back  (-Z): apex, bl[1], bl[0]  → cross gives -Z,+Y  ✓
    addSide(2, apex, bl[1], bl[0]);
    // Left  (-X): apex, bl[0], bl[3]  → cross gives -X,+Y  ✓
    addSide(3, apex, bl[0], bl[3]);

    // Base (-Y): bl[0],bl[1],bl[2],bl[3] — cross(bl[1]-bl[0], bl[2]-bl[0]) = (0,-1,0)  ✓
    {
        const glm::vec3 n{0.0f, -1.0f, 0.0f};
        AddQuad(m,
            {bl[0], n, colors[4]}, {bl[1], n, colors[4]},
            {bl[2], n, colors[4]}, {bl[3], n, colors[4]});
    }

    return m;
}

// ---------------------------------------------------------------------------
// GenerateCylinder
// ---------------------------------------------------------------------------
// Side quads + top cap fan + bottom cap fan.
// Colors: red top, green sides, blue bottom.
// Per-vertex side normals point radially outward.
// ---------------------------------------------------------------------------
MeshAsset MeshGenerator::GenerateCylinder(int segments)
{
    MeshAsset m;

    const glm::vec4 topColor  = {1.0f, 0.2f, 0.2f, 1.0f}; // red
    const glm::vec4 sideColor = {0.2f, 0.9f, 0.2f, 1.0f}; // green
    const glm::vec4 botColor  = {0.2f, 0.2f, 1.0f, 1.0f}; // blue

    const float R  = 0.5f;
    const float H  = 1.0f;
    const float dt = 2.0f * static_cast<float>(M_PI) / segments;

    for (int j = 0; j < segments; ++j) {
        const float a0 = j * dt;
        const float a1 = (j + 1) * dt;

        const glm::vec3 n0{std::cos(a0), 0.0f, std::sin(a0)};
        const glm::vec3 n1{std::cos(a1), 0.0f, std::sin(a1)};

        const glm::vec3 tl{std::cos(a0) * R,  H / 2, std::sin(a0) * R}; // top-j   (v0)
        const glm::vec3 bl{std::cos(a0) * R, -H / 2, std::sin(a0) * R}; // bot-j   (v1)
        const glm::vec3 br{std::cos(a1) * R, -H / 2, std::sin(a1) * R}; // bot-j+1 (v2)
        const glm::vec3 tr{std::cos(a1) * R,  H / 2, std::sin(a1) * R}; // top-j+1 (v3)

        // Side: CCW from outside → (tl,tr,br) then (tl,br,bl)
        AddTriangle(m, {tl, n0, sideColor}, {tr, n1, sideColor}, {br, n1, sideColor});
        AddTriangle(m, {tl, n0, sideColor}, {br, n1, sideColor}, {bl, n0, sideColor});
    }

    // Top cap (normal +Y, CCW from above → center, rim[j+1], rim[j])
    {
        const glm::vec3 topCtr{0.0f, H / 2, 0.0f};
        const glm::vec3 topN{0.0f, 1.0f, 0.0f};
        for (int j = 0; j < segments; ++j) {
            const float a0 = j * dt;
            const float a1 = (j + 1) * dt;
            const glm::vec3 r0{std::cos(a0) * R, H / 2, std::sin(a0) * R};
            const glm::vec3 r1{std::cos(a1) * R, H / 2, std::sin(a1) * R};
            AddTriangle(m, {topCtr, topN, topColor}, {r1, topN, topColor}, {r0, topN, topColor});
        }
    }

    // Bottom cap (normal -Y, CCW from below → center, rim[j], rim[j+1])
    {
        const glm::vec3 botCtr{0.0f, -H / 2, 0.0f};
        const glm::vec3 botN{0.0f, -1.0f, 0.0f};
        for (int j = 0; j < segments; ++j) {
            const float a0 = j * dt;
            const float a1 = (j + 1) * dt;
            const glm::vec3 r0{std::cos(a0) * R, -H / 2, std::sin(a0) * R};
            const glm::vec3 r1{std::cos(a1) * R, -H / 2, std::sin(a1) * R};
            AddTriangle(m, {botCtr, botN, botColor}, {r0, botN, botColor}, {r1, botN, botColor});
        }
    }

    return m;
}

// ---------------------------------------------------------------------------
// GenerateCone
// ---------------------------------------------------------------------------
// Side triangles (apex duplicated per face) + bottom cap fan.
// Colors: orange sides, blue base.
// ---------------------------------------------------------------------------
MeshAsset MeshGenerator::GenerateCone(int segments)
{
    MeshAsset m;

    const glm::vec4 sideColor = {1.0f, 0.5f, 0.0f, 1.0f}; // orange
    const glm::vec4 baseColor = {0.2f, 0.2f, 1.0f, 1.0f}; // blue

    const float R  = 0.5f;
    const float dt = 2.0f * static_cast<float>(M_PI) / segments;

    const glm::vec3 apex{0.0f, 0.5f, 0.0f};

    // Side triangles: CCW from outside → (apex, b1, b0)
    for (int j = 0; j < segments; ++j) {
        const float a0 = j * dt;
        const float a1 = (j + 1) * dt;
        const glm::vec3 b0{std::cos(a0) * R, -0.5f, std::sin(a0) * R};
        const glm::vec3 b1{std::cos(a1) * R, -0.5f, std::sin(a1) * R};

        // Face normal from cross product of (b1-apex) × (b0-apex)
        const glm::vec3 n = glm::normalize(glm::cross(b1 - apex, b0 - apex));
        AddTriangle(m, {apex, n, sideColor}, {b1, n, sideColor}, {b0, n, sideColor});
    }

    // Bottom cap (normal -Y, CCW from below → center, rim[j], rim[j+1])
    {
        const glm::vec3 baseCtr{0.0f, -0.5f, 0.0f};
        const glm::vec3 baseN{0.0f, -1.0f, 0.0f};
        for (int j = 0; j < segments; ++j) {
            const float a0 = j * dt;
            const float a1 = (j + 1) * dt;
            const glm::vec3 r0{std::cos(a0) * R, -0.5f, std::sin(a0) * R};
            const glm::vec3 r1{std::cos(a1) * R, -0.5f, std::sin(a1) * R};
            AddTriangle(m, {baseCtr, baseN, baseColor}, {r0, baseN, baseColor}, {r1, baseN, baseColor});
        }
    }

    return m;
}
