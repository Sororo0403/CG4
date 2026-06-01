#include "model/ModelManager.h"
#include "core/AssetManager.h"
#include "core/MathUtils.h"
#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"
#include "model/MaterialManager.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <numbers>
#include <system_error>
#include <utility>
#include <vector>

using namespace DirectX;

namespace {

constexpr std::array<Vertex, 4> kPlaneVertices = {{
    {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
}};

constexpr std::array<uint32_t, 6> kPlaneIndices = {0, 1, 2, 2, 1, 3};
constexpr uint32_t kMaxProceduralSegments = 4096;
constexpr uint32_t kMaxTerrainGrid = 1024;

float Hash01(int32_t x, int32_t z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u ^
                 static_cast<uint32_t>(z) * 668265263u ^ seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= h >> 16u;
    return static_cast<float>(h & 0x00FFFFFFu) /
           static_cast<float>(0x00FFFFFFu);
}

XMFLOAT3 CalculateFaceNormal(const XMFLOAT3 &a, const XMFLOAT3 &b,
                             const XMFLOAT3 &c) {
    XMVECTOR av = XMLoadFloat3(&a);
    XMVECTOR bv = XMLoadFloat3(&b);
    XMVECTOR cv = XMLoadFloat3(&c);
    XMVECTOR normal = XMVector3Cross(bv - av, cv - av);
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(normal));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return {0.0f, 1.0f, 0.0f};
    }
    normal = XMVector3Normalize(normal);
    XMFLOAT3 out{};
    XMStoreFloat3(&out, normal);
    if (!std::isfinite(out.x) || !std::isfinite(out.y) ||
        !std::isfinite(out.z)) {
        return {0.0f, 1.0f, 0.0f};
    }
    if (out.y < 0.0f) {
        out.x = -out.x;
        out.y = -out.y;
        out.z = -out.z;
    }
    return out;
}

std::filesystem::path ResolveModelPath(const std::filesystem::path &path) {
    return AssetManager::ResolvePath(path);
}

std::filesystem::path SafeCurrentPath() {
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(L".") : path;
}

std::wstring NormalizeModelPathKey(const std::filesystem::path &path) {
    std::wstring key = path.lexically_normal().wstring();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
#endif
    return key;
}

std::string MakeAssimpModelPath(const std::filesystem::path &resolvedPath) {
    std::error_code ec;
    const std::filesystem::path relative =
        std::filesystem::relative(resolvedPath, SafeCurrentPath(), ec);
    if (!ec && !relative.empty()) {
        auto begin = relative.begin();
        if (begin != relative.end() && *begin != L"..") {
            return relative.generic_string();
        }
    }

    return resolvedPath.string();
}

void ResetModelPlayback(Model &model) {
    if (!model.animations.empty()) {
        model.currentAnimation = model.animations.begin()->first;
        model.animationTime = 0.0f;
        model.isLoop = true;
        model.isPlaying = true;
        model.animationFinished = false;
    }
}

uint32_t AppendModel(std::vector<Model> &models, Model model) {
    if (models.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return UINT32_MAX;
    }
    models.push_back(std::move(model));
    return static_cast<uint32_t>(models.size() - 1);
}

uint32_t ClampProceduralSegments(uint32_t value, uint32_t minimum,
                                 uint32_t maximum) {
    return std::clamp(value, minimum, maximum);
}

float ClampFiniteMin(float value, float minimum) {
    if (!std::isfinite(value)) {
        return minimum;
    }
    return (std::max)(value, minimum);
}

float ClampFinite(float value, float minimum, float maximum, float fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

} // namespace

namespace {
ModelManager *gActiveModelManager = nullptr;
}

ModelManager &ModelManager::GetInstance() {
    static ModelManager instance;
    return gActiveModelManager != nullptr ? *gActiveModelManager : instance;
}

void ModelManager::SetActiveInstance(ModelManager *instance) {
    gActiveModelManager = instance;
}

ModelManager::~ModelManager() {
    Finalize();
}

void ModelManager::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                              TextureManager *textureManager) {
    if (!dxCommon || !srvManager || !textureManager) {
        Finalize();
        return;
    }
    Finalize();

    SetActiveInstance(this);
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;

    meshManager_.Initialize(dxCommon_);
    materialManager_.Initialize(dxCommon_);

    assimpLoader_.Initialize(textureManager_, &meshManager_, &materialManager_);

    modelRenderer_.Initialize(dxCommon_, srvManager, &meshManager_,
                              textureManager_, &materialManager_);
}

void ModelManager::Finalize() {
    if (dxCommon_ && !dxCommon_->IsDeviceRemoved() &&
        !dxCommon_->IsCommandListRecording()) {
        dxCommon_->WaitForGpuIfPossible();
    }

    if (srvManager_ != nullptr) {
        for (Model &model : models_) {
            for (ModelSubMesh &subMesh : model.subMeshes) {
                SkinCluster &skinCluster = subMesh.skinCluster;
                if (skinCluster.inputVertexSrvIndex != UINT32_MAX) {
                    srvManager_->FreeIfAllocated(skinCluster.inputVertexSrvIndex);
                    skinCluster.inputVertexSrvIndex = UINT32_MAX;
                }
                if (skinCluster.influenceSrvIndex != UINT32_MAX) {
                    srvManager_->FreeIfAllocated(skinCluster.influenceSrvIndex);
                    skinCluster.influenceSrvIndex = UINT32_MAX;
                }
                if (skinCluster.skinnedVertexUavIndex != UINT32_MAX) {
                    srvManager_->FreeIfAllocated(skinCluster.skinnedVertexUavIndex);
                    skinCluster.skinnedVertexUavIndex = UINT32_MAX;
                }
                if (skinCluster.paletteSrvIndex != UINT32_MAX) {
                    srvManager_->FreeIfAllocated(skinCluster.paletteSrvIndex);
                    skinCluster.paletteSrvIndex = UINT32_MAX;
                }
                if (skinCluster.influenceResource &&
                    skinCluster.mappedInfluence != nullptr) {
                    skinCluster.influenceResource->Unmap(0, nullptr);
                    skinCluster.mappedInfluence = nullptr;
                }
                if (skinCluster.paletteResource &&
                    skinCluster.mappedPalette != nullptr) {
                    skinCluster.paletteResource->Unmap(0, nullptr);
                    skinCluster.mappedPalette = nullptr;
                }
            }
        }
    }

    modelPathToId_.clear();
    models_.clear();
    materialManager_.Finalize();
    meshManager_.Finalize();
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    textureManager_ = nullptr;
    if (gActiveModelManager == this) {
        SetActiveInstance(nullptr);
    }
}

uint32_t ModelManager::Load(const std::wstring &path) {
    std::filesystem::path p = ResolveModelPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        return UINT32_MAX;
    }

    const std::wstring pathKey = NormalizeModelPathKey(p);
    auto it = modelPathToId_.find(pathKey);
    if (it != modelPathToId_.end()) {
        if (it->second >= models_.size()) {
            modelPathToId_.erase(it);
        } else {
            Model &cached = models_[it->second];
            ResetModelPlayback(cached);
            animator_.Update(cached, 0.0f);
            modelRenderer_.UpdateSkinClusters(cached);
            return it->second;
        }
    }

    std::string pathStr = MakeAssimpModelPath(p);

    Model model = assimpLoader_.Load(pathStr);
    if (model.subMeshes.empty()) {
        return UINT32_MAX;
    }
    modelRenderer_.CreateSkinClusters(model);

    ResetModelPlayback(model);

    animator_.Update(model, 0.0f);
    modelRenderer_.UpdateSkinClusters(model);

    uint32_t modelId = AppendModel(models_, std::move(model));
    if (modelId == UINT32_MAX) {
        return modelId;
    }
    modelPathToId_[pathKey] = modelId;

    return modelId;
}

uint32_t ModelManager::CreatePlane(uint32_t textureId,
                                   const Material &material) {
    Material planeMaterial = material;
    if (planeMaterial.baseColorTextureId == UINT32_MAX) {
        planeMaterial.baseColorTextureId = textureId;
    }
    XMStoreFloat4x4(&planeMaterial.uvTransform,
                    XMMatrixTranspose(XMMatrixIdentity()));

    Model model{};
    ModelSubMesh subMesh{};
    subMesh.vertexCount = static_cast<uint32_t>(kPlaneVertices.size());
    subMesh.meshId = meshManager_.CreateMesh(
        kPlaneVertices.data(), sizeof(Vertex),
        static_cast<uint32_t>(kPlaneVertices.size()), kPlaneIndices.data(),
        static_cast<uint32_t>(kPlaneIndices.size()));
    subMesh.textureId = textureId;
    subMesh.materialId = materialManager_.CreateMaterial(planeMaterial);

    model.subMeshes.push_back(subMesh);
    model.meshId = subMesh.meshId;
    model.textureId = textureId;
    model.materialId = subMesh.materialId;

    modelRenderer_.CreateSkinClusters(model);

    return AppendModel(models_, std::move(model));
}

uint32_t ModelManager::CreateBox(uint32_t textureId, const Material &material,
                                 float width, float height, float depth) {
    width = ClampFiniteMin(width, 0.001f);
    height = ClampFiniteMin(height, 0.001f);
    depth = ClampFiniteMin(depth, 0.001f);

    Material boxMaterial = material;
    if (boxMaterial.baseColorTextureId == UINT32_MAX) {
        boxMaterial.baseColorTextureId = textureId;
    }
    XMStoreFloat4x4(&boxMaterial.uvTransform,
                    XMMatrixTranspose(XMMatrixIdentity()));

    const float hx = width * 0.5f;
    const float hz = depth * 0.5f;
    const float y0 = 0.0f;
    const float y1 = height;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(24u);
    indices.reserve(36u);

    auto addFace = [&](const XMFLOAT3 &normal, const XMFLOAT3 &bottomLeft,
                       const XMFLOAT3 &topLeft, const XMFLOAT3 &bottomRight,
                       const XMFLOAT3 &topRight) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        vertices.push_back({bottomLeft, normal, {0.0f, 1.0f}});
        vertices.push_back({topLeft, normal, {0.0f, 0.0f}});
        vertices.push_back({bottomRight, normal, {1.0f, 1.0f}});
        vertices.push_back({topRight, normal, {1.0f, 0.0f}});
        indices.push_back(base + 0u);
        indices.push_back(base + 1u);
        indices.push_back(base + 2u);
        indices.push_back(base + 2u);
        indices.push_back(base + 1u);
        indices.push_back(base + 3u);
    };

    addFace({0.0f, 0.0f, 1.0f}, {-hx, y0, hz}, {-hx, y1, hz},
            {hx, y0, hz}, {hx, y1, hz});
    addFace({0.0f, 0.0f, -1.0f}, {hx, y0, -hz}, {hx, y1, -hz},
            {-hx, y0, -hz}, {-hx, y1, -hz});
    addFace({1.0f, 0.0f, 0.0f}, {hx, y0, hz}, {hx, y1, hz},
            {hx, y0, -hz}, {hx, y1, -hz});
    addFace({-1.0f, 0.0f, 0.0f}, {-hx, y0, -hz}, {-hx, y1, -hz},
            {-hx, y0, hz}, {-hx, y1, hz});
    addFace({0.0f, 1.0f, 0.0f}, {-hx, y1, -hz}, {-hx, y1, hz},
            {hx, y1, -hz}, {hx, y1, hz});
    addFace({0.0f, -1.0f, 0.0f}, {-hx, y0, hz}, {-hx, y0, -hz},
            {hx, y0, hz}, {hx, y0, -hz});

    Model model{};
    ModelSubMesh subMesh{};
    subMesh.vertexCount = static_cast<uint32_t>(vertices.size());
    subMesh.meshId = meshManager_.CreateMesh(
        vertices.data(), sizeof(Vertex), static_cast<uint32_t>(vertices.size()),
        indices.data(), static_cast<uint32_t>(indices.size()));
    subMesh.textureId = textureId;
    subMesh.materialId = materialManager_.CreateMaterial(boxMaterial);

    model.subMeshes.push_back(subMesh);
    model.meshId = subMesh.meshId;
    model.textureId = textureId;
    model.materialId = subMesh.materialId;

    modelRenderer_.CreateSkinClusters(model);
    return AppendModel(models_, std::move(model));
}

uint32_t ModelManager::CreateSphere(uint32_t textureId,
                                    const Material &material, uint32_t slice,
                                    uint32_t stack, float radius) {
    slice = ClampProceduralSegments(slice, 3u, kMaxProceduralSegments);
    stack = ClampProceduralSegments(stack, 2u, kMaxProceduralSegments);
    radius = ClampFiniteMin(radius, 0.001f);

    Material sphereMaterial = material;
    if (sphereMaterial.baseColorTextureId == UINT32_MAX) {
        sphereMaterial.baseColorTextureId = textureId;
    }
    XMStoreFloat4x4(&sphereMaterial.uvTransform,
                    XMMatrixTranspose(XMMatrixIdentity()));

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(slice + 1u) *
                     static_cast<size_t>(stack + 1u));
    indices.reserve(static_cast<size_t>(slice) * static_cast<size_t>(stack) *
                    6u);

    constexpr float pi = std::numbers::pi_v<float>;
    for (uint32_t y = 0; y <= stack; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(stack);
        const float pitch = v * pi;
        const float sinPitch = std::sinf(pitch);
        const float cosPitch = std::cosf(pitch);
        for (uint32_t x = 0; x <= slice; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(slice);
            const float yaw = u * pi * 2.0f;
            XMFLOAT3 normal{std::sinf(yaw) * sinPitch, cosPitch,
                            std::cosf(yaw) * sinPitch};
            XMFLOAT3 position{normal.x * radius, normal.y * radius,
                              normal.z * radius};
            vertices.push_back({position, normal, {u, v}});
        }
    }

    const uint32_t row = slice + 1u;
    for (uint32_t y = 0; y < stack; ++y) {
        for (uint32_t x = 0; x < slice; ++x) {
            const uint32_t i0 = y * row + x;
            const uint32_t i1 = i0 + 1u;
            const uint32_t i2 = i0 + row;
            const uint32_t i3 = i2 + 1u;
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i3);
        }
    }

    Model model{};
    ModelSubMesh subMesh{};
    subMesh.vertexCount = static_cast<uint32_t>(vertices.size());
    subMesh.meshId = meshManager_.CreateMesh(
        vertices.data(), sizeof(Vertex), static_cast<uint32_t>(vertices.size()),
        indices.data(), static_cast<uint32_t>(indices.size()));
    subMesh.textureId = textureId;
    subMesh.materialId = materialManager_.CreateMaterial(sphereMaterial);

    model.subMeshes.push_back(subMesh);
    model.meshId = subMesh.meshId;
    model.textureId = textureId;
    model.materialId = subMesh.materialId;

    modelRenderer_.CreateSkinClusters(model);
    return AppendModel(models_, std::move(model));
}

uint32_t ModelManager::CreateRing(uint32_t textureId, const Material &material,
                                  uint32_t divide, float outerRadius,
                                  float innerRadius) {
    divide = ClampProceduralSegments(divide, 3u, kMaxProceduralSegments);

    outerRadius = ClampFiniteMin(outerRadius, 0.001f);
    innerRadius =
        ClampFinite(innerRadius, 0.0f, outerRadius - 0.0001f, 0.0f);

    Material ringMaterial = material;
    if (ringMaterial.baseColorTextureId == UINT32_MAX) {
        ringMaterial.baseColorTextureId = textureId;
    }
    XMStoreFloat4x4(&ringMaterial.uvTransform,
                    XMMatrixTranspose(XMMatrixIdentity()));

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(divide) * 4u);
    indices.reserve(static_cast<size_t>(divide) * 6u);

    const float radianPerDivide =
        std::numbers::pi_v<float> * 2.0f / static_cast<float>(divide);

    for (uint32_t index = 0; index < divide; ++index) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());

        const float angle = static_cast<float>(index) * radianPerDivide;
        const float angleNext = static_cast<float>(index + 1) * radianPerDivide;

        const float sinV = std::sin(angle);
        const float cosV = std::cos(angle);
        const float sinNext = std::sin(angleNext);
        const float cosNext = std::cos(angleNext);

        const float u = static_cast<float>(index) / static_cast<float>(divide);
        const float uNext =
            static_cast<float>(index + 1) / static_cast<float>(divide);

        vertices.push_back({{-sinV * outerRadius, cosV * outerRadius, 0.0f},
                            {0.0f, 0.0f, 1.0f},
                            {u, 0.0f}});
        vertices.push_back(
            {{-sinNext * outerRadius, cosNext * outerRadius, 0.0f},
             {0.0f, 0.0f, 1.0f},
             {uNext, 0.0f}});
        vertices.push_back({{-sinV * innerRadius, cosV * innerRadius, 0.0f},
                            {0.0f, 0.0f, 1.0f},
                            {u, 1.0f}});
        vertices.push_back(
            {{-sinNext * innerRadius, cosNext * innerRadius, 0.0f},
             {0.0f, 0.0f, 1.0f},
             {uNext, 1.0f}});

        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
        indices.push_back(base + 1);
    }

    Model model{};
    ModelSubMesh subMesh{};
    subMesh.vertexCount = static_cast<uint32_t>(vertices.size());
    subMesh.meshId = meshManager_.CreateMesh(
        vertices.data(), sizeof(Vertex), static_cast<uint32_t>(vertices.size()),
        indices.data(), static_cast<uint32_t>(indices.size()));
    subMesh.textureId = textureId;
    subMesh.materialId = materialManager_.CreateMaterial(ringMaterial);

    model.subMeshes.push_back(subMesh);
    model.meshId = subMesh.meshId;
    model.textureId = textureId;
    model.materialId = subMesh.materialId;

    modelRenderer_.CreateSkinClusters(model);
    return AppendModel(models_, std::move(model));
}

uint32_t ModelManager::CreateCylinder(uint32_t textureId,
                                      const Material &material, uint32_t divide,
                                      float topRadius, float bottomRadius,
                                      float height) {
    divide = ClampProceduralSegments(divide, 3u, kMaxProceduralSegments);

    topRadius = ClampFiniteMin(topRadius, 0.001f);
    bottomRadius = ClampFiniteMin(bottomRadius, 0.001f);
    height = ClampFiniteMin(height, 0.001f);

    Material cylinderMaterial = material;
    if (cylinderMaterial.baseColorTextureId == UINT32_MAX) {
        cylinderMaterial.baseColorTextureId = textureId;
    }
    XMStoreFloat4x4(&cylinderMaterial.uvTransform,
                    XMMatrixTranspose(XMMatrixIdentity()));

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(divide) * 6u);
    indices.reserve(static_cast<size_t>(divide) * 6u);

    const float radianPerDivide =
        std::numbers::pi_v<float> * 2.0f / static_cast<float>(divide);

    for (uint32_t index = 0; index < divide; ++index) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());

        const float angle = static_cast<float>(index) * radianPerDivide;
        const float angleNext = static_cast<float>(index + 1) * radianPerDivide;

        const float sinV = std::sin(angle);
        const float cosV = std::cos(angle);
        const float sinNext = std::sin(angleNext);
        const float cosNext = std::cos(angleNext);

        const float u = static_cast<float>(index) / static_cast<float>(divide);
        const float uNext =
            static_cast<float>(index + 1) / static_cast<float>(divide);

        vertices.push_back({{-sinV * topRadius, height, cosV * topRadius},
                            {-sinV, 0.0f, cosV},
                            {u, 1.0f}});
        vertices.push_back({{-sinNext * topRadius, height, cosNext * topRadius},
                            {-sinNext, 0.0f, cosNext},
                            {uNext, 1.0f}});
        vertices.push_back({{-sinV * bottomRadius, 0.0f, cosV * bottomRadius},
                            {-sinV, 0.0f, cosV},
                            {u, 0.0f}});

        vertices.push_back({{-sinV * bottomRadius, 0.0f, cosV * bottomRadius},
                            {-sinV, 0.0f, cosV},
                            {u, 0.0f}});
        vertices.push_back({{-sinNext * topRadius, height, cosNext * topRadius},
                            {-sinNext, 0.0f, cosNext},
                            {uNext, 1.0f}});
        vertices.push_back(
            {{-sinNext * bottomRadius, 0.0f, cosNext * bottomRadius},
             {-sinNext, 0.0f, cosNext},
             {uNext, 0.0f}});

        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
        indices.push_back(base + 4);
        indices.push_back(base + 5);
    }

    Model model{};
    ModelSubMesh subMesh{};
    subMesh.vertexCount = static_cast<uint32_t>(vertices.size());
    subMesh.meshId = meshManager_.CreateMesh(
        vertices.data(), sizeof(Vertex), static_cast<uint32_t>(vertices.size()),
        indices.data(), static_cast<uint32_t>(indices.size()));
    subMesh.textureId = textureId;
    subMesh.materialId = materialManager_.CreateMaterial(cylinderMaterial);

    model.subMeshes.push_back(subMesh);
    model.meshId = subMesh.meshId;
    model.textureId = textureId;
    model.materialId = subMesh.materialId;

    modelRenderer_.CreateSkinClusters(model);
    return AppendModel(models_, std::move(model));
}

uint32_t ModelManager::CreateLowPolyTerrain(uint32_t textureId,
                                            const Material &material,
                                            uint32_t grid, float size,
                                            float maxHeight, float flatRadius,
                                            uint32_t seed) {
    grid = ClampProceduralSegments(grid, 4u, kMaxTerrainGrid);
    size = ClampFiniteMin(size, 1.0f);
    maxHeight = ClampFiniteMin(maxHeight, 0.0f);
    flatRadius = ClampFinite(flatRadius, 0.0f, size * 0.499f, 0.0f);

    Material terrainMaterial = material;
    if (terrainMaterial.baseColorTextureId == UINT32_MAX) {
        terrainMaterial.baseColorTextureId = textureId;
    }
    XMStoreFloat4x4(&terrainMaterial.uvTransform,
                    XMMatrixTranspose(XMMatrixIdentity()));

    const float halfSize = size * 0.5f;
    const float step = size / static_cast<float>(grid);
    const uint32_t pointCount = grid + 1u;
    std::vector<float> heights(static_cast<size_t>(pointCount) * pointCount);

    auto heightAt = [&](uint32_t xIndex, uint32_t zIndex) -> float & {
        return heights[static_cast<size_t>(zIndex) * pointCount + xIndex];
    };

    for (uint32_t z = 0; z < pointCount; ++z) {
        for (uint32_t x = 0; x < pointCount; ++x) {
            const float worldX = -halfSize + static_cast<float>(x) * step;
            const float worldZ = -halfSize + static_cast<float>(z) * step;
            const float dist = std::sqrt(worldX * worldX + worldZ * worldZ);
            const float outerT =
                MathUtils::SmoothStep01((dist - flatRadius) /
                                        (halfSize - flatRadius));

            const float ridge =
                0.45f *
                    Hash01(static_cast<int32_t>(x), static_cast<int32_t>(z),
                           seed) +
                0.35f *
                    Hash01(static_cast<int32_t>(x / 2u),
                           static_cast<int32_t>(z / 2u), seed + 97u) +
                0.20f *
                    Hash01(static_cast<int32_t>(x / 4u),
                           static_cast<int32_t>(z / 4u), seed + 193u);
            const float wave =
                0.5f + 0.5f * std::sinf(worldX * 0.22f + worldZ * 0.17f);
            heightAt(x, z) =
                (-0.28f + maxHeight * (0.35f + ridge * 0.78f + wave * 0.24f)) *
                outerT;
        }
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(grid) * grid * 6u);
    indices.reserve(static_cast<size_t>(grid) * grid * 6u);

    auto makePoint = [&](uint32_t xIndex, uint32_t zIndex) {
        const float worldX = -halfSize + static_cast<float>(xIndex) * step;
        const float worldZ = -halfSize + static_cast<float>(zIndex) * step;
        return XMFLOAT3{worldX, heightAt(xIndex, zIndex), worldZ};
    };

    auto pushTriangle = [&](const XMFLOAT3 &a, const XMFLOAT3 &b,
                            const XMFLOAT3 &c) {
        const XMFLOAT3 normal = CalculateFaceNormal(a, b, c);
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        vertices.push_back({a, normal, {0.0f, 0.0f}});
        vertices.push_back({b, normal, {1.0f, 0.0f}});
        vertices.push_back({c, normal, {0.5f, 1.0f}});
        indices.push_back(base + 0u);
        indices.push_back(base + 1u);
        indices.push_back(base + 2u);
    };

    for (uint32_t z = 0; z < grid; ++z) {
        for (uint32_t x = 0; x < grid; ++x) {
            XMFLOAT3 p00 = makePoint(x, z);
            XMFLOAT3 p10 = makePoint(x + 1u, z);
            XMFLOAT3 p01 = makePoint(x, z + 1u);
            XMFLOAT3 p11 = makePoint(x + 1u, z + 1u);

            const bool flip =
                Hash01(static_cast<int32_t>(x), static_cast<int32_t>(z),
                       seed + 389u) > 0.5f;
            if (flip) {
                pushTriangle(p00, p10, p11);
                pushTriangle(p00, p11, p01);
            } else {
                pushTriangle(p00, p10, p01);
                pushTriangle(p10, p11, p01);
            }
        }
    }

    Model model{};
    ModelSubMesh subMesh{};
    subMesh.vertexCount = static_cast<uint32_t>(vertices.size());
    subMesh.meshId = meshManager_.CreateMesh(
        vertices.data(), sizeof(Vertex), static_cast<uint32_t>(vertices.size()),
        indices.data(), static_cast<uint32_t>(indices.size()));
    subMesh.textureId = textureId;
    subMesh.materialId = materialManager_.CreateMaterial(terrainMaterial);

    model.subMeshes.push_back(subMesh);
    model.meshId = subMesh.meshId;
    model.textureId = textureId;
    model.materialId = subMesh.materialId;

    modelRenderer_.CreateSkinClusters(model);
    return AppendModel(models_, std::move(model));
}

uint32_t ModelManager::CreateMesh(
    const void *vertexData, uint32_t vertexStride, uint32_t vertexCount,
    const uint32_t *indexData, uint32_t indexCount,
    D3D12_PRIMITIVE_TOPOLOGY primitiveTopology) {
    return meshManager_.CreateMesh(vertexData, vertexStride, vertexCount,
                                   indexData, indexCount, primitiveTopology);
}

const Mesh &ModelManager::GetMesh(uint32_t meshId) const {
    return meshManager_.GetMesh(meshId);
}

void ModelManager::UpdateAnimation(uint32_t modelId, float deltaTime) {
    if (modelId >= models_.size()) {
        return;
    }

    animator_.Update(models_[modelId], deltaTime);
    modelRenderer_.UpdateSkinClusters(models_[modelId]);
}

void ModelManager::PlayAnimation(uint32_t modelId,
                                 const std::string &animationName, bool loop) {
    if (modelId >= models_.size()) {
        return;
    }

    animator_.Play(models_[modelId], animationName, loop);
}

bool ModelManager::IsAnimationFinished(uint32_t modelId) const {
    if (modelId >= models_.size()) {
        return false;
    }

    return animator_.IsFinished(models_[modelId]);
}

Model *ModelManager::GetModel(uint32_t modelId) {
    if (modelId >= models_.size()) {
        return nullptr;
    }

    return &models_[modelId];
}

const Model *ModelManager::GetModel(uint32_t modelId) const {
    if (modelId >= models_.size()) {
        return nullptr;
    }

    return &models_[modelId];
}

const Material &ModelManager::GetMaterial(uint32_t materialId) const {
    return materialManager_.GetMaterial(materialId);
}

void ModelManager::SetMaterial(uint32_t materialId, const Material &material) {
    materialManager_.SetMaterial(materialId, material);
}

void ModelManager::Draw(uint32_t modelId, const Transform &transform,
                        const Camera &camera, uint32_t environmentTextureId) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.Draw(*model, transform, camera, environmentTextureId);
}

void ModelManager::DrawInstanced(uint32_t modelId, const Transform *transforms,
                                 uint32_t instanceCount,
                                 const Camera &camera,
                                 uint32_t environmentTextureId) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstanced(*model, transforms, instanceCount, camera,
                                 environmentTextureId);
}

void ModelManager::DrawInstanced(uint32_t modelId,
                                 const InstanceData *instances,
                                 uint32_t instanceCount,
                                 const Camera &camera,
                                 uint32_t environmentTextureId) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstanced(*model, instances, instanceCount, camera,
                                 environmentTextureId);
}

void ModelManager::DrawShadow(
    uint32_t modelId, const Transform &transform,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawShadow(*model, transform, lightViewProjection);
}

void ModelManager::DrawInstancedShadow(
    uint32_t modelId, const Transform *transforms, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstancedShadow(*model, transforms, instanceCount,
                                       lightViewProjection);
}

void ModelManager::DrawInstancedShadow(
    uint32_t modelId, const InstanceData *instances, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstancedShadow(*model, instances, instanceCount,
                                       lightViewProjection);
}

void ModelManager::PrepareSkinning(uint32_t modelId) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.PrepareSkinning(*model);
}

void ModelManager::PrepareSkinning(std::initializer_list<uint32_t> modelIds) {
    std::vector<const Model *> models;
    models.reserve(modelIds.size());
    for (uint32_t modelId : modelIds) {
        const Model *model = GetModel(modelId);
        if (model) {
            models.push_back(model);
        }
    }

    modelRenderer_.PrepareSkinning(models);
}
