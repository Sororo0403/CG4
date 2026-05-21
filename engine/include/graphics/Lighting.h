#pragma once
#include <DirectXMath.h>
#include <array>

/// <summary>
/// 点光源1灯分のパラメータ
/// </summary>
struct PointLight {
    DirectX::XMFLOAT4 positionRange = {0.0f, 2.0f, -1.0f, 8.0f};
    DirectX::XMFLOAT4 colorIntensity = {1.0f, 0.55f, 0.35f, 1.1f};
};

/// <summary>
/// シーン全体で共有するライティング定数
/// </summary>
struct SceneLighting {
    DirectX::XMFLOAT3 keyLightDirection = {-0.35f, -1.0f, 0.25f};
    float padding0 = 0.0f;
    DirectX::XMFLOAT4 keyLightColor = {1.20f, 1.08f, 0.96f, 1.0f};
    DirectX::XMFLOAT3 fillLightDirection = {0.55f, -0.35f, -0.75f};
    float padding1 = 0.0f;
    DirectX::XMFLOAT4 fillLightColor = {0.22f, 0.32f, 0.48f, 0.38f};
    DirectX::XMFLOAT4 ambientColor = {0.28f, 0.30f, 0.34f, 1.0f};
    std::array<PointLight, 2> pointLights = {{
        {{0.0f, 2.0f, -1.0f, 8.0f}, {1.0f, 0.55f, 0.35f, 1.1f}},
        {{0.0f, 1.5f, 2.5f, 7.0f}, {0.25f, 0.45f, 1.0f, 0.75f}},
    }};
    DirectX::XMFLOAT4 lightingParams = {48.0f, 0.30f, 2.8f, 0.22f};
    /// <summary>
    /// x: 0=Phong, 1=Blinn-Phong
    /// </summary>
    DirectX::XMFLOAT4 lightingModeParams = {1.0f, 0.0f, 0.0f, 0.0f};
};

/// <summary>
/// シーン全体で共有するフォグ設定
/// </summary>
struct SceneFog {
    DirectX::XMFLOAT4 color = {0.62f, 0.68f, 0.76f, 1.0f};
    DirectX::XMFLOAT4 params = {0.0f, 18.0f, 70.0f, 1.0f};
};

/// <summary>
/// シャドウマップを受ける側の共通サンプリング設定
/// </summary>
struct SceneShadowSettings {
    float bias = 0.0015f;
    float strength = 0.45f;
    float normalBias = 0.0f;
    float filterRadius = 1.45f;
    float depthSoftness = 2600.0f;
    float edgeFade = 0.045f;
};
