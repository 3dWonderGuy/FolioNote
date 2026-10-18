/**
 * =========================================================================================
 * @file core/gpu/gpu_ink_manager.cpp
 * @brief Implementation of SDL_GPU Hardware-Accelerated Triangle Mesh Inking Manager
 * =========================================================================================
 */

#include "core/gpu/gpu_ink_manager.hpp"
#include "core/gpu/gpu_ink_shaders.hpp"

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
    if (m_initialized && m_device != nullptr) {
        return true;
    }
    if (m_initAttempted) {
        return false;
    }
    m_initAttempted = true;

    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
        SDL_InitSubSystem(SDL_INIT_VIDEO);
    }

    m_device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_MSL,
        false,
        nullptr);

    if (!m_device) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager] SDL_CreateGPUDevice unavailable: %s", SDL_GetError());
        return false;
    }

    if (!CreatePipelines()) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[GpuInkManager] Failed to create SDL_GPU graphics pipelines");
        Shutdown();
        return false;
    }

    m_stagedVertices.reserve(65536);
    m_stagedIndices.reserve(196608);
    m_drawCommands.reserve(512);

    m_initialized = true;
    return true;
}

void GpuInkManager::Shutdown() {
    if (!m_device) {
        m_initialized = false;
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
    if (m_pipelineMultiply) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipelineMultiply);
        m_pipelineMultiply = nullptr;
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
    if (!m_device) return "none";
    const char* drv = SDL_GetGPUDeviceDriver(m_device);
    return drv ? drv : "unknown";
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

    SDL_GPUDepthStencilState dsMultiply = dsDisabled;
    if (m_depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID) {
        dsMultiply.enable_stencil_test = true;
        dsMultiply.compare_mask        = 0xFF;
        dsMultiply.write_mask          = 0xFF;

        // Front & Back: Only pass if stencil != ref. On pass, replace with ref. On fail, keep.
        // This ensures the first fragment writes the ref and passes, and subsequent overlapping
        // fragments from the same stroke test (stencil == ref) and fail, achieving zero intra-stroke darkening.
        dsMultiply.front_stencil_state.compare_op    = SDL_GPU_COMPAREOP_NOT_EQUAL;
        dsMultiply.front_stencil_state.pass_op       = SDL_GPU_STENCILOP_REPLACE;
        dsMultiply.front_stencil_state.fail_op       = SDL_GPU_STENCILOP_KEEP;
        dsMultiply.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
        dsMultiply.back_stencil_state                = dsMultiply.front_stencil_state;
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

    m_pipelineNormal   = makePipeline(blendNormal, dsMultiply);
    m_pipelineMultiply = makePipeline(blendMultiply, dsMultiply);
    m_pipelineAdditive = makePipeline(blendAdditive, dsDisabled);

    return m_pipelineNormal != nullptr && m_pipelineMultiply != nullptr && m_pipelineAdditive != nullptr;
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

    DrawCommand cmd;
    cmd.vertexOffset = static_cast<int32_t>(m_stagedVertices.size());
    cmd.indexOffset  = static_cast<uint32_t>(m_stagedIndices.size());
    cmd.indexCount   = static_cast<uint32_t>(mesh.indices.size());
    cmd.blendMode    = blendMode;
    cmd.strokeId     = (strokeId != 0) ? strokeId : m_nextAutoStrokeId++;

    cmd.uniforms.transform0[0] = static_cast<float>(worldToScreen.m00);
    cmd.uniforms.transform0[1] = static_cast<float>(worldToScreen.m01);
    cmd.uniforms.transform0[2] = static_cast<float>(worldToScreen.m10);
    cmd.uniforms.transform0[3] = static_cast<float>(worldToScreen.m11);
    cmd.uniforms.transform1[0] = static_cast<float>(worldToScreen.m20);
    cmd.uniforms.transform1[1] = static_cast<float>(worldToScreen.m21);
    cmd.uniforms.params[0]     = std::max(0.01f, scalePxPerMm);
    cmd.uniforms.params[1]     = std::clamp(opacity, 0.0f, 1.0f);
    cmd.uniforms.params[2]     = 0.5f;
    cmd.uniforms.params[3]     = 0.0f;

    m_stagedVertices.insert(m_stagedVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    m_stagedIndices.insert(m_stagedIndices.end(), mesh.indices.begin(), mesh.indices.end());
    m_drawCommands.push_back(cmd);
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
        clipX = m_activeClipX;
        clipY = m_activeClipY;
        clipW = m_activeClipW;
        clipH = m_activeClipH;
    }

    int rx = (clipW > 0 && clipH > 0) ? std::clamp(clipX, 0, surfW - 1) : 0;
    int ry = (clipW > 0 && clipH > 0) ? std::clamp(clipY, 0, surfH - 1) : 0;
    int rw = (clipW > 0 && clipH > 0) ? std::clamp(clipW, 1, surfW - rx) : surfW;
    int rh = (clipW > 0 && clipH > 0) ? std::clamp(clipH, 1, surfH - ry) : surfH;

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
        if (dc.blendMode == BlendMode::Multiply) {
            wantPipe = m_pipelineMultiply;
        } else if (dc.blendMode == BlendMode::Additive) {
            wantPipe = m_pipelineAdditive;
        }

        if (wantPipe != currentPipe) {
            SDL_BindGPUGraphicsPipeline(renderPass, wantPipe);
            currentPipe = wantPipe;
        }

        if ((dc.blendMode == BlendMode::Multiply || dc.blendMode == BlendMode::Normal) && m_depthStencilTexture) {
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

bool GpuInkManager::FlushToContext(BLContext& ctx,
                                   int clipX,
                                   int clipY,
                                   int clipW,
                                   int clipH) {
    if (m_drawCommands.empty()) {
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
