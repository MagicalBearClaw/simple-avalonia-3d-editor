#include "GridPipeline.h"
#include "VulkanContext.h"
#include "OffscreenTarget.h"

#include <stdexcept>
#include <fstream>
#include <vector>
#include <array>

static std::vector<uint32_t> loadSpirv(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error(std::string("Failed to open shader: ") + path);

    std::streamsize size = file.tellg();
    file.seekg(0);

    std::vector<uint32_t> code(size / 4);
    file.read(reinterpret_cast<char*>(code.data()), size);
    return code;
}

static VkShaderModule createModule(VkDevice device, const std::vector<uint32_t>& code)
{
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size() * sizeof(uint32_t);
    ci.pCode    = code.data();

    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shader module");
    return mod;
}

// ---------------------------------------------------------------------------
// GridPipeline constructor
// ---------------------------------------------------------------------------
GridPipeline::GridPipeline(VulkanContext& ctx, OffscreenTarget& target,
                            VkPipelineLayout sharedLayout)
    : m_device(ctx.device), m_pipelineLayout(sharedLayout)
{
    auto vertCode = loadSpirv("shaders/grid.vert.spv");
    auto fragCode = loadSpirv("shaders/grid.frag.spv");

    VkShaderModule vertModule = createModule(ctx.device, vertCode);
    VkShaderModule fragModule = createModule(ctx.device, fragCode);

    // No vertex input — the vertex shader generates the quad from gl_VertexIndex.
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates    = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    // No culling — the flat quad should be visible from both sides.
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth test ON (LESS_OR_EQUAL) so meshes occlude the grid; depth write OFF
    // so the grid doesn't fill the depth buffer and affect subsequent draws.
    // No stencil operations.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Standard alpha blending.
    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.blendEnable         = VK_TRUE;
    blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttach.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendAttach.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAttach;

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName  = "main";

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount          = static_cast<uint32_t>(stages.size());
    ci.pStages             = stages.data();
    ci.pVertexInputState   = &vertexInput;
    ci.pInputAssemblyState = &inputAssembly;
    ci.pViewportState      = &viewportState;
    ci.pRasterizationState = &raster;
    ci.pMultisampleState   = &msaa;
    ci.pDepthStencilState  = &depthStencil;
    ci.pColorBlendState    = &blend;
    ci.pDynamicState       = &dynamicState;
    ci.layout              = m_pipelineLayout;
    ci.renderPass          = target.renderPass;
    ci.subpass             = 0;

    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create grid pipeline");

    vkDestroyShaderModule(ctx.device, vertModule, nullptr);
    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
}

// ---------------------------------------------------------------------------
// GridPipeline destructor
// ---------------------------------------------------------------------------
GridPipeline::~GridPipeline()
{
    vkDestroyPipeline(m_device, pipeline, nullptr);
    // m_pipelineLayout is borrowed from MeshPipeline — not destroyed here
}

// ---------------------------------------------------------------------------
// BindAndSetup
// ---------------------------------------------------------------------------
void GridPipeline::BindAndSetup(VkCommandBuffer cmd, VkDescriptorSet sceneSet) const
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 1, &sceneSet, 0, nullptr);
}
