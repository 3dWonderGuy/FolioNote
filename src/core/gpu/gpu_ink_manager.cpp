/**
 * =========================================================================================
 * @file core/gpu/gpu_ink_manager.cpp
 * @brief Implementation of SDL_GPU Hardware-Accelerated Triangle Mesh Inking Manager
 * =========================================================================================
 */

#include "core/gpu/gpu_ink_manager.hpp"
#include "core/gpu/gpu_ink_shaders.hpp"

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#endif

#include <algorithm>
#include <cstring>

namespace Folio {

GpuInkManager& GpuInkManager::Instance() {
    static GpuInkManager instance;
    return instance;
}

GpuInkManager::~GpuInkManager() {
    Shutdown();
}

bool GpuInkManager::Init() {
    if (m_initialized && (m_device != nullptr || m_glesInitialized)) {
        return true;
    }
    if (m_initAttempted) {
        return false;
    }
    m_initAttempted = true;

    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
        SDL_InitSubSystem(SDL_INIT_VIDEO);
    }

    // 1. Try pure SDL_GPU (Vulkan on Android / Linux / Windows, Metal on Apple)
    // Relax optional desktop-only features that mobile GPUs reject
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXBC_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);

    m_device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);

    if (m_device) {
        if (!CreatePipelines()) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager] Failed to create SDL_GPU graphics pipelines");
            Shutdown();
            return false;
        }

        m_stagedVertices.reserve(65536);
        m_stagedIndices.reserve(196608);
        m_drawCommands.reserve(512);

        m_initialized = true;
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager] Initialized pure SDL_GPU device successfully using driver: %s", GetDriverName());
        return true;
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager] SDL_CreateGPUDevice unavailable (%s), trying GLES fallback...", SDL_GetError());

    // 2. Direct OpenGL ES fallback if SDL_GPU device could not be acquired
    if (InitGlesFallback()) {
        m_stagedVertices.reserve(65536);
        m_stagedIndices.reserve(196608);
        m_drawCommands.reserve(512);

        m_initialized = true;
        return true;
    }

    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager] Neither SDL_GPU nor OpenGL ES could be initialized");
    return false;
}

void GpuInkManager::Shutdown() {
    ShutdownGlesFallback();

    if (!m_device) {
        m_initialized = false;
        m_initAttempted = false;
        return;
    }

    SDL_WaitForGPUIdle(m_device);

    if (m_imageDownloadTransfer) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_imageDownloadTransfer);
        m_imageDownloadTransfer = nullptr;
    }
    if (m_imageUploadTransfer) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_imageUploadTransfer);
        m_imageUploadTransfer = nullptr;
    }
    m_imageTransferCapacity = 0;

    if (m_indexTransfer) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_indexTransfer);
        m_indexTransfer = nullptr;
    }
    if (m_indexBuffer) {
        SDL_ReleaseGPUBuffer(m_device, m_indexBuffer);
        m_indexBuffer = nullptr;
    }
    m_indexCapacity = 0;

    if (m_vertexTransfer) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_vertexTransfer);
        m_vertexTransfer = nullptr;
    }
    if (m_vertexBuffer) {
        SDL_ReleaseGPUBuffer(m_device, m_vertexBuffer);
        m_vertexBuffer = nullptr;
    }
    m_vertexCapacity = 0;

    if (m_canvasTexture) {
        SDL_ReleaseGPUTexture(m_device, m_canvasTexture);
        m_canvasTexture = nullptr;
    }
    if (m_depthStencilTexture) {
        SDL_ReleaseGPUTexture(m_device, m_depthStencilTexture);
        m_depthStencilTexture = nullptr;
    }
    m_textureWidth = 0;
    m_textureHeight = 0;

    if (m_pipelineAdditive) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipelineAdditive);
        m_pipelineAdditive = nullptr;
    }
    if (m_pipelineMultiplySkirt) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipelineMultiplySkirt);
        m_pipelineMultiplySkirt = nullptr;
    }
    if (m_pipelineMultiplyCore) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipelineMultiplyCore);
        m_pipelineMultiplyCore = nullptr;
    }
    if (m_pipelineNormalSkirt) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipelineNormalSkirt);
        m_pipelineNormalSkirt = nullptr;
    }
    if (m_pipelineNormalCore) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipelineNormalCore);
        m_pipelineNormalCore = nullptr;
    }
    if (m_pipelineNormal) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipelineNormal);
        m_pipelineNormal = nullptr;
    }
    if (m_fragmentShader) {
        SDL_ReleaseGPUShader(m_device, m_fragmentShader);
        m_fragmentShader = nullptr;
    }
    if (m_vertexShader) {
        SDL_ReleaseGPUShader(m_device, m_vertexShader);
        m_vertexShader = nullptr;
    }

    SDL_DestroyGPUDevice(m_device);
    m_device = nullptr;
    m_initialized = false;
    m_initAttempted = false;
}

const char* GpuInkManager::GetDriverName() const noexcept {
    if (m_device) {
        const char* drv = SDL_GetGPUDeviceDriver(m_device);
        return drv ? drv : "unknown";
    }
    if (m_glesInitialized) {
        return "opengles";
    }
    return "none";
}

bool GpuInkManager::CreatePipelines() {
    SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(m_device);
    if (!(supported & SDL_GPU_SHADERFORMAT_SPIRV)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager] Device does not support SPIR-V shader format");
        return false;
    }

    SDL_GPUShaderCreateInfo vertInfo = {};
    vertInfo.entrypoint           = "main";
    vertInfo.stage                = SDL_GPU_SHADERSTAGE_VERTEX;
    vertInfo.format               = SDL_GPU_SHADERFORMAT_SPIRV;
    vertInfo.code                 = GpuShaders::kInkStrokeVertSpirv;
    vertInfo.code_size            = sizeof(GpuShaders::kInkStrokeVertSpirv);
    vertInfo.num_uniform_buffers  = 1;
    vertInfo.num_storage_buffers  = 0;
    vertInfo.num_storage_textures = 0;
    vertInfo.num_samplers         = 0;

    SDL_GPUShaderCreateInfo fragInfo = {};
    fragInfo.entrypoint           = "main";
    fragInfo.stage                = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragInfo.format               = SDL_GPU_SHADERFORMAT_SPIRV;
    fragInfo.code                 = GpuShaders::kInkStrokeFragSpirv;
    fragInfo.code_size            = sizeof(GpuShaders::kInkStrokeFragSpirv);
    fragInfo.num_uniform_buffers  = 0;
    fragInfo.num_storage_buffers  = 0;
    fragInfo.num_storage_textures = 0;
    fragInfo.num_samplers         = 0;

    m_vertexShader   = SDL_CreateGPUShader(m_device, &vertInfo);
    m_fragmentShader = SDL_CreateGPUShader(m_device, &fragInfo);
    if (!m_vertexShader || !m_fragmentShader) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager] SDL_CreateGPUShader failed: %s", SDL_GetError());
        return false;
    }

    // Vertex buffer layout matching Folio::InkVertex (20 bytes)
    SDL_GPUVertexBufferDescription vbufDesc[1] = {};
    vbufDesc[0].slot               = 0;
    vbufDesc[0].input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbufDesc[0].instance_step_rate = 0;
    vbufDesc[0].pitch              = sizeof(InkVertex);

    SDL_GPUVertexAttribute vattrs[3] = {};
    // location 0: vec2(x, y)
    vattrs[0].buffer_slot = 0;
    vattrs[0].location    = 0;
    vattrs[0].format      = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vattrs[0].offset      = offsetof(InkVertex, x);

    // location 1: vec2(edgeDist, radius)
    vattrs[1].buffer_slot = 0;
    vattrs[1].location    = 1;
    vattrs[1].format      = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vattrs[1].offset      = offsetof(InkVertex, edgeDist);

    // location 2: vec4(color) unpacked from R8G8B8A8
    vattrs[2].buffer_slot = 0;
    vattrs[2].location    = 2;
    vattrs[2].format      = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    vattrs[2].offset      = offsetof(InkVertex, color);

    SDL_GPUVertexInputState vertexInput = {};
    vertexInput.num_vertex_buffers         = 1;
    vertexInput.vertex_buffer_descriptions = vbufDesc;
    vertexInput.num_vertex_attributes      = 3;
    vertexInput.vertex_attributes          = vattrs;

    m_depthStencilFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    if (SDL_GPUTextureSupportsFormat(m_device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                                     SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        m_depthStencilFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    } else if (SDL_GPUTextureSupportsFormat(m_device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                                            SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        m_depthStencilFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    }

    SDL_GPURasterizerState rasterizer = {};
    rasterizer.fill_mode         = SDL_GPU_FILLMODE_FILL;
    rasterizer.cull_mode         = SDL_GPU_CULLMODE_NONE;
    rasterizer.front_face        = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    rasterizer.enable_depth_bias = false;
    rasterizer.enable_depth_clip = false;

    SDL_GPUMultisampleState multisample = {};
    multisample.sample_count = SDL_GPU_SAMPLECOUNT_1;
    multisample.enable_mask  = false;

    SDL_GPUDepthStencilState dsDisabled = {};
    dsDisabled.enable_depth_test   = false;
    dsDisabled.enable_depth_write  = false;
    dsDisabled.enable_stencil_test = false;

    SDL_GPUDepthStencilState dsReplace = dsDisabled;
    SDL_GPUDepthStencilState dsKeep    = dsDisabled;

    if (m_depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID) {
        // Pass 1: Solid Core Ribbon stamps stencil with strokeId
        dsReplace.enable_stencil_test = true;
        dsReplace.compare_mask        = 0xFF;
        dsReplace.write_mask          = 0xFF;
        dsReplace.front_stencil_state.compare_op    = SDL_GPU_COMPAREOP_NOT_EQUAL;
        dsReplace.front_stencil_state.pass_op       = SDL_GPU_STENCILOP_REPLACE;
        dsReplace.front_stencil_state.fail_op       = SDL_GPU_STENCILOP_KEEP;
        dsReplace.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
        dsReplace.back_stencil_state                = dsReplace.front_stencil_state;

        // Pass 2: Exterior AA Skirt tests against stencil but NEVER writes (pass_op = KEEP, write_mask = 0x00),
        // preventing skirts from blocking subsequent crossing core ribbons (zero white cracks).
        dsKeep.enable_stencil_test = true;
        dsKeep.compare_mask        = 0xFF;
        dsKeep.write_mask          = 0x00;
        dsKeep.front_stencil_state.compare_op    = SDL_GPU_COMPAREOP_NOT_EQUAL;
        dsKeep.front_stencil_state.pass_op       = SDL_GPU_STENCILOP_KEEP;
        dsKeep.front_stencil_state.fail_op       = SDL_GPU_STENCILOP_KEEP;
        dsKeep.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
        dsKeep.back_stencil_state                = dsKeep.front_stencil_state;
    }

    auto makePipeline = [&](const SDL_GPUColorTargetBlendState& blendState,
                            const SDL_GPUDepthStencilState& dsState) -> SDL_GPUGraphicsPipeline* {
        SDL_GPUColorTargetDescription colorDesc[1] = {};
        colorDesc[0].format      = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        colorDesc[0].blend_state = blendState;

        SDL_GPUGraphicsPipelineTargetInfo targetInfo = {};
        targetInfo.num_color_targets         = 1;
        targetInfo.color_target_descriptions = colorDesc;
        if (m_depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID) {
            targetInfo.has_depth_stencil_target = true;
            targetInfo.depth_stencil_format     = m_depthStencilFormat;
        } else {
            targetInfo.has_depth_stencil_target = false;
        }

        SDL_GPUGraphicsPipelineCreateInfo pipeInfo = {};
        pipeInfo.vertex_shader       = m_vertexShader;
        pipeInfo.fragment_shader     = m_fragmentShader;
        pipeInfo.vertex_input_state  = vertexInput;
        pipeInfo.primitive_type      = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipeInfo.rasterizer_state    = rasterizer;
        pipeInfo.multisample_state   = multisample;
        pipeInfo.depth_stencil_state = dsState;
        pipeInfo.target_info         = targetInfo;

        return SDL_CreateGPUGraphicsPipeline(m_device, &pipeInfo);
    };

    // 1. Normal (Premultiplied Alpha SrcOver: out = src + (1 - srcA) * dst)
    SDL_GPUColorTargetBlendState blendNormal = {};
    blendNormal.enable_blend          = true;
    blendNormal.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blendNormal.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blendNormal.color_blend_op        = SDL_GPU_BLENDOP_ADD;
    blendNormal.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blendNormal.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blendNormal.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;
    blendNormal.color_write_mask      = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                                        SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

    // 2. Multiply (Highlighter: out.rgb = dst.rgb * src.rgb + (1 - srcA) * dst.rgb)
    SDL_GPUColorTargetBlendState blendMultiply = blendNormal;
    blendMultiply.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
    blendMultiply.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

    // 3. Additive (Neon/Glow: out.rgb = src.rgb + dst.rgb)
    SDL_GPUColorTargetBlendState blendAdditive = blendNormal;
    blendAdditive.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blendAdditive.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;

    m_pipelineNormal        = makePipeline(blendNormal,   dsDisabled);
    m_pipelineNormalCore    = makePipeline(blendNormal,   dsReplace);
    m_pipelineNormalSkirt   = makePipeline(blendNormal,   dsKeep);
    m_pipelineMultiplyCore  = makePipeline(blendMultiply, dsReplace);
    m_pipelineMultiplySkirt = makePipeline(blendMultiply, dsKeep);
    m_pipelineAdditive      = makePipeline(blendAdditive, dsDisabled);

    return m_pipelineNormal != nullptr &&
           m_pipelineNormalCore != nullptr &&
           m_pipelineNormalSkirt != nullptr &&
           m_pipelineMultiplyCore != nullptr &&
           m_pipelineMultiplySkirt != nullptr &&
           m_pipelineAdditive != nullptr;
}

bool GpuInkManager::EnsureCanvasTexture(uint32_t width, uint32_t height) {
    if (m_canvasTexture && m_textureWidth >= width && m_textureHeight >= height) {
        return true;
    }

    uint32_t newW = std::max({width, m_textureWidth, 1920u});
    uint32_t newH = std::max({height, m_textureHeight, 1080u});

    if (m_canvasTexture) {
        SDL_ReleaseGPUTexture(m_device, m_canvasTexture);
        m_canvasTexture = nullptr;
    }
    if (m_depthStencilTexture) {
        SDL_ReleaseGPUTexture(m_device, m_depthStencilTexture);
        m_depthStencilTexture = nullptr;
    }

    SDL_GPUTextureCreateInfo texInfo = {};
    texInfo.type                 = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format               = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    texInfo.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texInfo.width                = newW;
    texInfo.height               = newH;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels           = 1;
    texInfo.sample_count         = SDL_GPU_SAMPLECOUNT_1;

    m_canvasTexture = SDL_CreateGPUTexture(m_device, &texInfo);
    if (!m_canvasTexture) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager] SDL_CreateGPUTexture failed: %s", SDL_GetError());
        m_textureWidth = 0;
        m_textureHeight = 0;
        return false;
    }

    if (m_depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID) {
        SDL_GPUTextureCreateInfo dsInfo = {};
        dsInfo.type                 = SDL_GPU_TEXTURETYPE_2D;
        dsInfo.format               = m_depthStencilFormat;
        dsInfo.usage                = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        dsInfo.width                = newW;
        dsInfo.height               = newH;
        dsInfo.layer_count_or_depth = 1;
        dsInfo.num_levels           = 1;
        dsInfo.sample_count         = SDL_GPU_SAMPLECOUNT_1;

        m_depthStencilTexture = SDL_CreateGPUTexture(m_device, &dsInfo);
        if (!m_depthStencilTexture) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                        "[GpuInkManager] SDL_CreateGPUTexture for depth-stencil failed: %s",
                        SDL_GetError());
        }
    }

    m_textureWidth  = newW;
    m_textureHeight = newH;
    return true;
}

bool GpuInkManager::EnsureGeometryBuffers(uint32_t vertexBytes, uint32_t indexBytes) {
    if (vertexBytes > m_vertexCapacity || !m_vertexBuffer || !m_vertexTransfer) {
        uint32_t newCap = std::max({vertexBytes, m_vertexCapacity * 2u, 65536u * static_cast<uint32_t>(sizeof(InkVertex))});
        if (m_vertexBuffer) SDL_ReleaseGPUBuffer(m_device, m_vertexBuffer);
        if (m_vertexTransfer) SDL_ReleaseGPUTransferBuffer(m_device, m_vertexTransfer);

        SDL_GPUBufferCreateInfo bufInfo = {};
        bufInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bufInfo.size  = newCap;
        m_vertexBuffer = SDL_CreateGPUBuffer(m_device, &bufInfo);

        SDL_GPUTransferBufferCreateInfo tInfo = {};
        tInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tInfo.size  = newCap;
        m_vertexTransfer = SDL_CreateGPUTransferBuffer(m_device, &tInfo);

        if (!m_vertexBuffer || !m_vertexTransfer) {
            m_vertexCapacity = 0;
            return false;
        }
        m_vertexCapacity = newCap;
    }

    if (indexBytes > m_indexCapacity || !m_indexBuffer || !m_indexTransfer) {
        uint32_t newCap = std::max({indexBytes, m_indexCapacity * 2u, 196608u * static_cast<uint32_t>(sizeof(uint32_t))});
        if (m_indexBuffer) SDL_ReleaseGPUBuffer(m_device, m_indexBuffer);
        if (m_indexTransfer) SDL_ReleaseGPUTransferBuffer(m_device, m_indexTransfer);

        SDL_GPUBufferCreateInfo bufInfo = {};
        bufInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        bufInfo.size  = newCap;
        m_indexBuffer = SDL_CreateGPUBuffer(m_device, &bufInfo);

        SDL_GPUTransferBufferCreateInfo tInfo = {};
        tInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tInfo.size  = newCap;
        m_indexTransfer = SDL_CreateGPUTransferBuffer(m_device, &tInfo);

        if (!m_indexBuffer || !m_indexTransfer) {
            m_indexCapacity = 0;
            return false;
        }
        m_indexCapacity = newCap;
    }

    return true;
}

bool GpuInkManager::EnsureImageTransferBuffers(uint32_t byteSize) {
    if (byteSize <= m_imageTransferCapacity && m_imageUploadTransfer && m_imageDownloadTransfer) {
        return true;
    }

    uint32_t newCap = std::max({byteSize, m_imageTransferCapacity * 2u, 1920u * 1080u * 4u});
    if (m_imageUploadTransfer) SDL_ReleaseGPUTransferBuffer(m_device, m_imageUploadTransfer);
    if (m_imageDownloadTransfer) SDL_ReleaseGPUTransferBuffer(m_device, m_imageDownloadTransfer);

    SDL_GPUTransferBufferCreateInfo upInfo = {};
    upInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    upInfo.size  = newCap;
    m_imageUploadTransfer = SDL_CreateGPUTransferBuffer(m_device, &upInfo);

    SDL_GPUTransferBufferCreateInfo downInfo = {};
    downInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    downInfo.size  = newCap;
    m_imageDownloadTransfer = SDL_CreateGPUTransferBuffer(m_device, &downInfo);

    if (!m_imageUploadTransfer || !m_imageDownloadTransfer) {
        m_imageTransferCapacity = 0;
        return false;
    }

    m_imageTransferCapacity = newCap;
    return true;
}

void GpuInkManager::BeginBatch() noexcept {
    m_stagedVertices.clear();
    m_stagedIndices.clear();
    m_drawCommands.clear();
    m_nextAutoStrokeId = 1;
}

void GpuInkManager::SubmitMesh(const InkMesh& mesh,
                               const BLMatrix2D& worldToScreen,
                               float scalePxPerMm,
                               float opacity,
                               BlendMode blendMode,
                               uint32_t strokeId) {
    if (mesh.empty()) return;

    if (!m_initialized && !m_initAttempted) {
        Init();
    }
    if (!IsEnabled()) return;

    const uint32_t baseVertex = static_cast<uint32_t>(m_stagedVertices.size());
    const uint32_t baseIndex  = static_cast<uint32_t>(m_stagedIndices.size());
    const uint32_t sid        = (strokeId != 0) ? strokeId : m_nextAutoStrokeId++;

    GpuStrokeUniforms uniforms;
    uniforms.transform0[0] = static_cast<float>(worldToScreen.m00);
    uniforms.transform0[1] = static_cast<float>(worldToScreen.m01);
    uniforms.transform0[2] = static_cast<float>(worldToScreen.m10);
    uniforms.transform0[3] = static_cast<float>(worldToScreen.m11);
    uniforms.transform1[0] = static_cast<float>(worldToScreen.m20);
    uniforms.transform1[1] = static_cast<float>(worldToScreen.m21);
    uniforms.params[0]     = std::max(0.01f, scalePxPerMm);
    uniforms.params[1]     = std::clamp(opacity, 0.0f, 1.0f);
    uniforms.params[2]     = 0.5f;
    uniforms.params[3]     = 0.0f;

    const uint32_t totalIndices = static_cast<uint32_t>(mesh.indices.size());
    const uint32_t coreIndices  = mesh.coreIndexCount;

    // Check whether the stroke is semi-transparent either via uniform opacity or vertex color alpha
    bool hasVertexAlpha = false;
    if (!mesh.vertices.empty()) {
        uint8_t vAlpha = static_cast<uint8_t>(mesh.vertices[0].color >> 24);
        if (vAlpha < 254) {
            hasVertexAlpha = true;
        }
    }

    // Semi-transparent strokes (Multiply highlighter, or semi-transparent brush/pencil)
    // require stencil isolation to prevent intra-stroke self-intersection darkening.
    const bool isSemiTransparent = (blendMode == BlendMode::Multiply) ||
                                   (blendMode == BlendMode::Normal && (opacity < 0.999f || hasVertexAlpha));

    if (blendMode == BlendMode::Additive) {
        DrawCommand cmd;
        cmd.vertexOffset = static_cast<int32_t>(baseVertex);
        cmd.indexOffset  = baseIndex;
        cmd.indexCount   = totalIndices;
        cmd.pipeline     = PipelineType::Additive;
        cmd.strokeId     = sid;
        cmd.uniforms     = uniforms;
        m_drawCommands.push_back(cmd);
    } else if (!isSemiTransparent || coreIndices == 0 || coreIndices >= totalIndices) {
        // Opaque stroke (100% opacity) or mesh with no skirt partition: single draw call
        DrawCommand cmd;
        cmd.vertexOffset = static_cast<int32_t>(baseVertex);
        cmd.indexOffset  = baseIndex;
        cmd.indexCount   = totalIndices;
        if (blendMode == BlendMode::Multiply) {
            cmd.pipeline = PipelineType::MultiplyCore;
        } else if (opacity < 0.999f) {
            cmd.pipeline = PipelineType::NormalCore;
        } else {
            cmd.pipeline = PipelineType::NormalOpaque;
        }
        cmd.strokeId     = sid;
        cmd.uniforms     = uniforms;
        m_drawCommands.push_back(cmd);
    } else {
        // Two-Pass Stencil for Semi-Transparent Stroke:
        // Pass 1: Solid Core Ribbon (dsReplace)
        DrawCommand cmdCore;
        cmdCore.vertexOffset = static_cast<int32_t>(baseVertex);
        cmdCore.indexOffset  = baseIndex;
        cmdCore.indexCount   = coreIndices;
        cmdCore.pipeline     = (blendMode == BlendMode::Multiply) ? PipelineType::MultiplyCore : PipelineType::NormalCore;
        cmdCore.strokeId     = sid;
        cmdCore.uniforms     = uniforms;
        m_drawCommands.push_back(cmdCore);

        // Pass 2: Anti-Aliasing Skirt (dsKeep, never stamps stencil)
        DrawCommand cmdSkirt;
        cmdSkirt.vertexOffset = static_cast<int32_t>(baseVertex);
        cmdSkirt.indexOffset  = baseIndex + coreIndices;
        cmdSkirt.indexCount   = totalIndices - coreIndices;
        cmdSkirt.pipeline     = (blendMode == BlendMode::Multiply) ? PipelineType::MultiplySkirt : PipelineType::NormalSkirt;
        cmdSkirt.strokeId     = sid;
        cmdSkirt.uniforms     = uniforms;
        m_drawCommands.push_back(cmdSkirt);
    }

    m_stagedVertices.insert(m_stagedVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    m_stagedIndices.insert(m_stagedIndices.end(), mesh.indices.begin(), mesh.indices.end());
}

bool GpuInkManager::FlushToSurface(BLImage& targetSurface,
                                   int clipX,
                                   int clipY,
                                   int clipW,
                                   int clipH) {
    if (m_drawCommands.empty() || m_stagedVertices.empty() || m_stagedIndices.empty()) {
        BeginBatch();
        return false;
    }

    if (!m_initialized && !m_initAttempted) {
        Init();
    }
    if (!IsEnabled() || targetSurface.is_empty()) {
        BeginBatch();
        return false;
    }

    BLImageData imgData = {};
    if (targetSurface.make_mutable(&imgData) != BL_SUCCESS || !imgData.pixel_data) {
        BeginBatch();
        return false;
    }

    const int surfW = imgData.size.w;
    const int surfH = imgData.size.h;
    if (surfW <= 0 || surfH <= 0) {
        BeginBatch();
        return false;
    }

    if (clipW <= 0 || clipH <= 0) {
        if (m_activeClipW > 0 && m_activeClipH > 0) {
            clipX = m_activeClipX;
            clipY = m_activeClipY;
            clipW = m_activeClipW;
            clipH = m_activeClipH;
        } else if (!m_stagedVertices.empty()) {
            // Compute tight screen-space bounding rect of staged vertices
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            for (const auto& dc : m_drawCommands) {
                float m00 = dc.uniforms.transform0[0];
                float m01 = dc.uniforms.transform0[1];
                float m10 = dc.uniforms.transform0[2];
                float m11 = dc.uniforms.transform0[3];
                float m20 = dc.uniforms.transform1[0];
                float m21 = dc.uniforms.transform1[1];
                for (uint32_t i = 0; i < dc.indexCount; ++i) {
                    uint32_t idx = m_stagedIndices[dc.indexOffset + i];
                    const auto& v = m_stagedVertices[dc.vertexOffset + idx];
                    float sx = m00 * v.x + m10 * v.y + m20;
                    float sy = m01 * v.x + m11 * v.y + m21;
                    minX = std::min(minX, sx);
                    minY = std::min(minY, sy);
                    maxX = std::max(maxX, sx);
                    maxY = std::max(maxY, sy);
                }
            }
            if (minX <= maxX && minY <= maxY) {
                clipX = static_cast<int>(std::floor(minX)) - 4;
                clipY = static_cast<int>(std::floor(minY)) - 4;
                clipW = static_cast<int>(std::ceil(maxX)) - clipX + 8;
                clipH = static_cast<int>(std::ceil(maxY)) - clipY + 8;
            }
        }
    }

    int rx = 0;
    int ry = 0;
    int rw = surfW;
    int rh = surfH;

    if (clipW > 0 && clipH > 0) {
        int x0 = std::clamp(clipX, 0, surfW);
        int y0 = std::clamp(clipY, 0, surfH);
        int x1 = std::clamp(clipX + clipW, 0, surfW);
        int y1 = std::clamp(clipY + clipH, 0, surfH);
        if (x1 <= x0 || y1 <= y0) {
            // The dirty rect is completely outside the surface bounds
            BeginBatch();
            return true;
        }
        rx = x0;
        ry = y0;
        rw = x1 - x0;
        rh = y1 - y0;
    }

    if (!m_device && m_glesInitialized) {
        return FlushToSurfaceGLES(imgData, surfW, surfH, rx, ry, rw, rh);
    }

    const uint32_t vtxBytes = static_cast<uint32_t>(m_stagedVertices.size() * sizeof(InkVertex));
    const uint32_t idxBytes = static_cast<uint32_t>(m_stagedIndices.size() * sizeof(uint32_t));
    const uint32_t regionRowBytes = static_cast<uint32_t>(rw) * 4u;
    const uint32_t regionTotalBytes = regionRowBytes * static_cast<uint32_t>(rh);

    if (!EnsureCanvasTexture(static_cast<uint32_t>(surfW), static_cast<uint32_t>(surfH)) ||
        !EnsureGeometryBuffers(vtxBytes, idxBytes) ||
        !EnsureImageTransferBuffers(regionTotalBytes)) {
        BeginBatch();
        return false;
    }

    // 1. Map & copy vertex, index, and backdrop sub-rect into GPU upload transfer buffers
    void* vtxPtr = SDL_MapGPUTransferBuffer(m_device, m_vertexTransfer, true);
    if (!vtxPtr) {
        BeginBatch();
        return false;
    }
    std::memcpy(vtxPtr, m_stagedVertices.data(), vtxBytes);
    SDL_UnmapGPUTransferBuffer(m_device, m_vertexTransfer);

    void* idxPtr = SDL_MapGPUTransferBuffer(m_device, m_indexTransfer, true);
    if (!idxPtr) {
        BeginBatch();
        return false;
    }
    std::memcpy(idxPtr, m_stagedIndices.data(), idxBytes);
    SDL_UnmapGPUTransferBuffer(m_device, m_indexTransfer);

    void* imgUpPtr = SDL_MapGPUTransferBuffer(m_device, m_imageUploadTransfer, true);
    if (!imgUpPtr) {
        BeginBatch();
        return false;
    }
    const uint8_t* srcPixels = static_cast<const uint8_t*>(imgData.pixel_data);
    const intptr_t stride = imgData.stride;
    for (int row = 0; row < rh; ++row) {
        const uint8_t* srcRow = srcPixels + (ry + row) * stride + rx * 4;
        uint8_t* dstRow = static_cast<uint8_t*>(imgUpPtr) + static_cast<size_t>(row) * regionRowBytes;
        std::memcpy(dstRow, srcRow, regionRowBytes);
    }
    SDL_UnmapGPUTransferBuffer(m_device, m_imageUploadTransfer);

    // 2. Acquire command buffer and upload geometry + backdrop patch
    SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(m_device);
    if (!cmdBuf) {
        BeginBatch();
        return false;
    }

    SDL_GPUCopyPass* copyIn = SDL_BeginGPUCopyPass(cmdBuf);

    SDL_GPUTransferBufferLocation vtxLoc = { m_vertexTransfer, 0 };
    SDL_GPUBufferRegion vtxReg = { m_vertexBuffer, 0, vtxBytes };
    SDL_UploadToGPUBuffer(copyIn, &vtxLoc, &vtxReg, true);

    SDL_GPUTransferBufferLocation idxLoc = { m_indexTransfer, 0 };
    SDL_GPUBufferRegion idxReg = { m_indexBuffer, 0, idxBytes };
    SDL_UploadToGPUBuffer(copyIn, &idxLoc, &idxReg, true);

    SDL_GPUTextureTransferInfo texUpInfo = {};
    texUpInfo.transfer_buffer = m_imageUploadTransfer;
    texUpInfo.offset          = 0;
    texUpInfo.pixels_per_row  = static_cast<uint32_t>(rw);
    texUpInfo.rows_per_layer  = static_cast<uint32_t>(rh);

    SDL_GPUTextureRegion texRegion = {};
    texRegion.texture = m_canvasTexture;
    texRegion.x       = static_cast<uint32_t>(rx);
    texRegion.y       = static_cast<uint32_t>(ry);
    texRegion.w       = static_cast<uint32_t>(rw);
    texRegion.h       = static_cast<uint32_t>(rh);
    texRegion.d       = 1;

    SDL_UploadToGPUTexture(copyIn, &texUpInfo, &texRegion, false);
    SDL_EndGPUCopyPass(copyIn);

    // 3. Execute hardware rasterization pass for all staged InkMesh batches
    SDL_GPUColorTargetInfo colorTarget = {};
    colorTarget.texture     = m_canvasTexture;
    colorTarget.load_op     = SDL_GPU_LOADOP_LOAD;
    colorTarget.store_op    = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo dsTarget = {};
    const SDL_GPUDepthStencilTargetInfo* pDsTarget = nullptr;
    if (m_depthStencilTexture) {
        dsTarget.texture          = m_depthStencilTexture;
        dsTarget.clear_depth      = 0.0f;
        dsTarget.load_op          = SDL_GPU_LOADOP_DONT_CARE;
        dsTarget.store_op         = SDL_GPU_STOREOP_DONT_CARE;
        dsTarget.clear_stencil    = 0;
        dsTarget.stencil_load_op  = SDL_GPU_LOADOP_CLEAR;
        dsTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        dsTarget.cycle            = false;
        pDsTarget = &dsTarget;
    }

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(cmdBuf, &colorTarget, 1, pDsTarget);

    SDL_GPUViewport gpuVp = {};
    gpuVp.x         = 0.0f;
    gpuVp.y         = 0.0f;
    gpuVp.w         = static_cast<float>(surfW);
    gpuVp.h         = static_cast<float>(surfH);
    gpuVp.min_depth = 0.0f;
    gpuVp.max_depth = 1.0f;
    SDL_SetGPUViewport(renderPass, &gpuVp);

    SDL_Rect scissor = { rx, ry, rw, rh };
    SDL_SetGPUScissor(renderPass, &scissor);

    SDL_GPUBufferBinding vtxBind = { m_vertexBuffer, 0 };
    SDL_GPUBufferBinding idxBind = { m_indexBuffer, 0 };
    SDL_BindGPUVertexBuffers(renderPass, 0, &vtxBind, 1);
    SDL_BindGPUIndexBuffer(renderPass, &idxBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    uint8_t currentStencilRef = 0;
    uint32_t lastStrokeId = 0;
    bool hasLastStrokeId = false;
    SDL_GPUGraphicsPipeline* currentPipe = nullptr;

    for (auto& dc : m_drawCommands) {
        dc.uniforms.transform1[2] = static_cast<float>(surfW);
        dc.uniforms.transform1[3] = static_cast<float>(surfH);

        SDL_GPUGraphicsPipeline* wantPipe = m_pipelineNormal;
        bool usesStencil = false;

        switch (dc.pipeline) {
            case PipelineType::NormalOpaque:
                wantPipe = m_pipelineNormal;
                break;
            case PipelineType::NormalCore:
                wantPipe = m_pipelineNormalCore;
                usesStencil = true;
                break;
            case PipelineType::NormalSkirt:
                wantPipe = m_pipelineNormalSkirt;
                usesStencil = true;
                break;
            case PipelineType::MultiplyCore:
                wantPipe = m_pipelineMultiplyCore;
                usesStencil = true;
                break;
            case PipelineType::MultiplySkirt:
                wantPipe = m_pipelineMultiplySkirt;
                usesStencil = true;
                break;
            case PipelineType::Additive:
                wantPipe = m_pipelineAdditive;
                break;
        }

        if (wantPipe != currentPipe) {
            SDL_BindGPUGraphicsPipeline(renderPass, wantPipe);
            currentPipe = wantPipe;
        }

        if (usesStencil && m_depthStencilTexture) {
            if (!hasLastStrokeId || dc.strokeId != lastStrokeId) {
                currentStencilRef++;
                if (currentStencilRef == 255) {
                    // Stencil reference saturated: restart render pass to clear stencil buffer
                    SDL_EndGPURenderPass(renderPass);
                    renderPass = SDL_BeginGPURenderPass(cmdBuf, &colorTarget, 1, pDsTarget);
                    SDL_SetGPUViewport(renderPass, &gpuVp);
                    SDL_SetGPUScissor(renderPass, &scissor);
                    SDL_BindGPUVertexBuffers(renderPass, 0, &vtxBind, 1);
                    SDL_BindGPUIndexBuffer(renderPass, &idxBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    currentPipe = nullptr;
                    SDL_BindGPUGraphicsPipeline(renderPass, wantPipe);
                    currentPipe = wantPipe;
                    currentStencilRef = 1;
                }
                lastStrokeId = dc.strokeId;
                hasLastStrokeId = true;
            }
            SDL_SetGPUStencilReference(renderPass, currentStencilRef);
        }

        SDL_PushGPUVertexUniformData(cmdBuf, 0, &dc.uniforms, sizeof(GpuStrokeUniforms));
        SDL_DrawGPUIndexedPrimitives(renderPass, dc.indexCount, 1, dc.indexOffset, dc.vertexOffset, 0);
    }

    SDL_EndGPURenderPass(renderPass);

    // 4. Download the composited sub-rect back into targetSurface
    SDL_GPUCopyPass* copyOut = SDL_BeginGPUCopyPass(cmdBuf);

    SDL_GPUTextureTransferInfo texDownInfo = {};
    texDownInfo.transfer_buffer = m_imageDownloadTransfer;
    texDownInfo.offset          = 0;
    texDownInfo.pixels_per_row  = static_cast<uint32_t>(rw);
    texDownInfo.rows_per_layer  = static_cast<uint32_t>(rh);

    SDL_DownloadFromGPUTexture(copyOut, &texRegion, &texDownInfo);
    SDL_EndGPUCopyPass(copyOut);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);
    if (fence) {
        SDL_WaitForGPUFences(m_device, true, &fence, 1);
        SDL_ReleaseGPUFence(m_device, fence);
    }

    void* imgDownPtr = SDL_MapGPUTransferBuffer(m_device, m_imageDownloadTransfer, false);
    if (imgDownPtr) {
        uint8_t* dstPixels = static_cast<uint8_t*>(imgData.pixel_data);
        for (int row = 0; row < rh; ++row) {
            const uint8_t* srcRow = static_cast<const uint8_t*>(imgDownPtr) + static_cast<size_t>(row) * regionRowBytes;
            uint8_t* dstRow = dstPixels + (ry + row) * stride + rx * 4;
            std::memcpy(dstRow, srcRow, regionRowBytes);
        }
        SDL_UnmapGPUTransferBuffer(m_device, m_imageDownloadTransfer);
    }

    m_lastVertexCount   = static_cast<uint32_t>(m_stagedVertices.size());
    m_lastTriangleCount = static_cast<uint32_t>(m_stagedIndices.size() / 3);
    m_lastDrawCallCount = static_cast<uint32_t>(m_drawCommands.size());

    BeginBatch();
    return true;
}

bool GpuInkManager::InitGlesFallback() {
    if (m_glesInitialized) {
        return true;
    }

    const char* vShaderSrc =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aEdgeDist_radius;\n"
        "layout(location = 2) in vec4 aColor;\n"
        "uniform vec4 uTransform0;\n"
        "uniform vec4 uTransform1;\n"
        "uniform vec4 uParams;\n"
        "out vec4 vColor;\n"
        "out vec2 vEdgeParams;\n"
        "void main() {\n"
        "    float m00 = uTransform0.x;\n"
        "    float m01 = uTransform0.y;\n"
        "    float m10 = uTransform0.z;\n"
        "    float m11 = uTransform0.w;\n"
        "    float m20 = uTransform1.x;\n"
        "    float m21 = uTransform1.y;\n"
        "    vec2 vpSize = uTransform1.zw;\n"
        "    vec2 screenPos = vec2(\n"
        "        m00 * aPos.x + m10 * aPos.y + m20,\n"
        "        m01 * aPos.x + m11 * aPos.y + m21\n"
        "    );\n"
        "    vec2 ndc = vec2(\n"
        "        (screenPos.x / vpSize.x) * 2.0 - 1.0,\n"
        "        1.0 - (screenPos.y / vpSize.y) * 2.0\n"
        "    );\n"
        "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
        "    vColor = aColor;\n"
        "    vColor.a *= uParams.y;\n"
        "    vEdgeParams = vec2(aEdgeDist_radius.x, aEdgeDist_radius.y * uParams.x);\n"
        "}\n";

    const char* fShaderSrc =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec4 vColor;\n"
        "in vec2 vEdgeParams;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    float u = abs(vEdgeParams.x);\n"
        "    float rPx = max(vEdgeParams.y, 0.5);\n"
        "    float dPx = (1.0 - u) * rPx;\n"
        "    float alpha = clamp(dPx, 0.0, 1.0);\n"
        "    fragColor = vec4(vColor.rgb * (vColor.a * alpha), vColor.a * alpha);\n"
        "}\n";

    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vShaderSrc, nullptr);
    glCompileShader(vert);

    GLint compiled = 0;
    glGetShaderiv(vert, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char infoLog[512];
        glGetShaderInfoLog(vert, sizeof(infoLog), nullptr, infoLog);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager GLES] Vertex shader failed: %s", infoLog);
        glDeleteShader(vert);
        return false;
    }

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fShaderSrc, nullptr);
    glCompileShader(frag);

    glGetShaderiv(frag, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char infoLog[512];
        glGetShaderInfoLog(frag, sizeof(infoLog), nullptr, infoLog);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager GLES] Fragment shader failed: %s", infoLog);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    m_glesProgram = glCreateProgram();
    glAttachShader(m_glesProgram, vert);
    glAttachShader(m_glesProgram, frag);
    glLinkProgram(m_glesProgram);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint linked = 0;
    glGetProgramiv(m_glesProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        char infoLog[512];
        glGetProgramInfoLog(m_glesProgram, sizeof(infoLog), nullptr, infoLog);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager GLES] Program link failed: %s", infoLog);
        glDeleteProgram(m_glesProgram);
        m_glesProgram = 0;
        return false;
    }

    m_glesLocTransform0 = glGetUniformLocation(m_glesProgram, "uTransform0");
    m_glesLocTransform1 = glGetUniformLocation(m_glesProgram, "uTransform1");
    m_glesLocParams     = glGetUniformLocation(m_glesProgram, "uParams");

    // Generate dynamic VAO, VBO, IBO
    glGenVertexArrays(1, &m_glesVao);
    glGenBuffers(1, &m_glesVbo);
    glGenBuffers(1, &m_glesIbo);

    glBindVertexArray(m_glesVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_glesVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_glesIbo);

    // Attribute 0: vec2 pos (offset 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(InkVertex), reinterpret_cast<const void*>(offsetof(InkVertex, x)));

    // Attribute 1: vec2 edgeDist_radius (offset 8)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(InkVertex), reinterpret_cast<const void*>(offsetof(InkVertex, edgeDist)));

    // Attribute 2: vec4 color normalized from UBYTE4_NORM (offset 16)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(InkVertex), reinterpret_cast<const void*>(offsetof(InkVertex, color)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Framebuffer setup for offscreen canvas backing
    glGenFramebuffers(1, &m_glesFbo);
    glGenTextures(1, &m_glesCanvasTex);
    glGenRenderbuffers(1, &m_glesRboDepthStencil);

    m_glesInitialized = true;
    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager GLES] Initialized OpenGL ES 3.2 fallback pipeline successfully");
    return true;
}

void GpuInkManager::ShutdownGlesFallback() {
    if (!m_glesInitialized) return;

    if (m_glesRboDepthStencil) {
        glDeleteRenderbuffers(1, &m_glesRboDepthStencil);
        m_glesRboDepthStencil = 0;
    }
    if (m_glesCanvasTex) {
        glDeleteTextures(1, &m_glesCanvasTex);
        m_glesCanvasTex = 0;
    }
    if (m_glesFbo) {
        glDeleteFramebuffers(1, &m_glesFbo);
        m_glesFbo = 0;
    }
    if (m_glesIbo) {
        glDeleteBuffers(1, &m_glesIbo);
        m_glesIbo = 0;
    }
    if (m_glesVbo) {
        glDeleteBuffers(1, &m_glesVbo);
        m_glesVbo = 0;
    }
    if (m_glesVao) {
        glDeleteVertexArrays(1, &m_glesVao);
        m_glesVao = 0;
    }
    if (m_glesProgram) {
        glDeleteProgram(m_glesProgram);
        m_glesProgram = 0;
    }

    m_glesTexWidth = 0;
    m_glesTexHeight = 0;
    m_glesInitialized = false;
}

bool GpuInkManager::FlushToSurfaceGLES(BLImageData& imgData, int surfW, int surfH, int rx, int ry, int rw, int rh) {
    if (!m_glesInitialized || m_drawCommands.empty()) {
        BeginBatch();
        return false;
    }

    // Save previous OpenGL state to avoid interfering with ImGui / Canvas presentation
    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    GLint prevElementBuffer = 0;
    GLint prevFbo = 0;
    GLint prevTexture = 0;
    GLint prevViewport[4];
    GLint prevScissorBox[4];
    GLboolean prevScissorTest = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencilTest = glIsEnabled(GL_STENCIL_TEST);
    GLint prevBlendSrcRgb = 0, prevBlendDstRgb = 0, prevBlendSrcAlpha = 0, prevBlendDstAlpha = 0;

    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElementBuffer);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);

    // Ensure backing FBO texture and depth/stencil renderbuffer
    if (m_glesTexWidth != static_cast<uint32_t>(surfW) || m_glesTexHeight != static_cast<uint32_t>(surfH)) {
        m_glesTexWidth = static_cast<uint32_t>(surfW);
        m_glesTexHeight = static_cast<uint32_t>(surfH);

        glBindTexture(GL_TEXTURE_2D, m_glesCanvasTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_glesTexWidth, m_glesTexHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindRenderbuffer(GL_RENDERBUFFER, m_glesRboDepthStencil);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_glesTexWidth, m_glesTexHeight);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, m_glesFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_glesCanvasTex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_glesRboDepthStencil);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager GLES] FBO incomplete: 0x%X", status);
        }
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, m_glesFbo);
    }

    // Upload background patch to backing texture
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(imgData.stride / 4));
    glBindTexture(GL_TEXTURE_2D, m_glesCanvasTex);
    const uint8_t* patchData = static_cast<const uint8_t*>(imgData.pixel_data) + (static_cast<size_t>(ry) * imgData.stride) + (static_cast<size_t>(rx) * 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, rx, ry, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, patchData);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    // Upload vertex & index buffers
    glBindBuffer(GL_ARRAY_BUFFER, m_glesVbo);
    glBufferData(GL_ARRAY_BUFFER, m_stagedVertices.size() * sizeof(InkVertex), m_stagedVertices.data(), GL_STREAM_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_glesIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_stagedIndices.size() * sizeof(uint32_t), m_stagedIndices.data(), GL_STREAM_DRAW);

    // Setup viewport & scissor
    glViewport(0, 0, surfW, surfH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(rx, ry, rw, rh);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glEnable(GL_STENCIL_TEST);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);

    glUseProgram(m_glesProgram);
    glBindVertexArray(m_glesVao);

    uint8_t currentStencilRef = 0;
    uint32_t lastStrokeId = 0;
    bool hasLastStrokeId = false;

    for (auto& dc : m_drawCommands) {
        dc.uniforms.transform1[2] = static_cast<float>(surfW);
        dc.uniforms.transform1[3] = static_cast<float>(surfH);

        // Blending configuration matching SDL_GPU pipelines
        switch (dc.pipeline) {
            case PipelineType::NormalOpaque:
            case PipelineType::NormalCore:
            case PipelineType::NormalSkirt:
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case PipelineType::MultiplyCore:
            case PipelineType::MultiplySkirt:
                glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case PipelineType::Additive:
                glBlendFunc(GL_ONE, GL_ONE);
                break;
        }

        // Stencil test configuration for semi-transparent strokes
        bool usesStencil = (dc.pipeline == PipelineType::NormalCore || dc.pipeline == PipelineType::NormalSkirt ||
                            dc.pipeline == PipelineType::MultiplyCore || dc.pipeline == PipelineType::MultiplySkirt);

        if (usesStencil) {
            if (!hasLastStrokeId || dc.strokeId != lastStrokeId) {
                currentStencilRef++;
                if (currentStencilRef == 255) {
                    glClear(GL_STENCIL_BUFFER_BIT);
                    currentStencilRef = 1;
                }
                lastStrokeId = dc.strokeId;
                hasLastStrokeId = true;
            }

            if (dc.pipeline == PipelineType::NormalCore || dc.pipeline == PipelineType::MultiplyCore) {
                // Pass 1: Solid core ribbon stamps stencil reference (GL_REPLACE)
                glStencilFunc(GL_NOTEQUAL, currentStencilRef, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                glStencilMask(0xFF);
            } else {
                // Pass 2: Anti-aliasing skirt preserves stencil (GL_KEEP)
                glStencilFunc(GL_NOTEQUAL, currentStencilRef, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                glStencilMask(0x00);
            }
        } else {
            glStencilMask(0x00);
            glStencilFunc(GL_ALWAYS, 0, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        }

        glUniform4fv(m_glesLocTransform0, 1, dc.uniforms.transform0);
        glUniform4fv(m_glesLocTransform1, 1, dc.uniforms.transform1);
        glUniform4fv(m_glesLocParams, 1, dc.uniforms.params);

        // Draw primitives: in our batching, stagedIndices offsets already incorporate base index
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(dc.indexCount), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(static_cast<uintptr_t>(dc.indexOffset * sizeof(uint32_t))));
    }

    // Read back composited region into target surface
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, static_cast<GLint>(imgData.stride / 4));
    uint8_t* dstData = static_cast<uint8_t*>(imgData.pixel_data) + (static_cast<size_t>(ry) * imgData.stride) + (static_cast<size_t>(rx) * 4);
    glReadPixels(rx, ry, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, dstData);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);

    // Restore previous OpenGL state
    glUseProgram(prevProgram);
    glBindVertexArray(prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, prevArrayBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prevElementBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glBindTexture(GL_TEXTURE_2D, prevTexture);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glScissor(prevScissorBox[0], prevScissorBox[1], prevScissorBox[2], prevScissorBox[3]);
    if (!prevScissorTest) glDisable(GL_SCISSOR_TEST);
    if (!prevBlend) glDisable(GL_BLEND);
    glBlendFuncSeparate(prevBlendSrcRgb, prevBlendDstRgb, prevBlendSrcAlpha, prevBlendDstAlpha);
    if (prevCullFace) glEnable(GL_CULL_FACE);
    if (prevDepthTest) glEnable(GL_DEPTH_TEST);
    if (!prevStencilTest) glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);

    m_lastVertexCount   = static_cast<uint32_t>(m_stagedVertices.size());
    m_lastTriangleCount = static_cast<uint32_t>(m_stagedIndices.size() / 3);
    m_lastDrawCallCount = static_cast<uint32_t>(m_drawCommands.size());

    BeginBatch();
    return true;
}

bool GpuInkManager::FlushToContext(BLContext& ctx,
                                   int clipX,
                                   int clipY,
                                   int clipW,
                                   int clipH) {
    if (m_drawCommands.empty()) {
        BeginBatch();
        return false;
    }
    BLImage* targetImg = ctx.target_image();
    if (!targetImg) {
        BeginBatch();
        return false;
    }
    ctx.flush(BL_CONTEXT_FLUSH_SYNC);
    return FlushToSurface(*targetImg, clipX, clipY, clipW, clipH);
}

} // namespace Folio
