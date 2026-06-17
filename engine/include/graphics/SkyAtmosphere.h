#pragma once

#include <DirectXMath.h>

struct SkyAtmosphereParameters {
    DirectX::XMFLOAT3 sunDirection = {0.0f, 1.0f, 0.0f};
    float skyIntensity = 1.0f;

    DirectX::XMFLOAT3 sunColor = {1.0f, 0.96f, 0.86f};
    float sunIntensity = 1.0f;

    DirectX::XMFLOAT3 rayleighScattering = {5.802f, 13.558f, 33.100f};
    float rayleighStrength = 1.0f;

    DirectX::XMFLOAT3 mieScattering = {3.996f, 3.996f, 3.996f};
    float mieStrength = 1.0f;

    DirectX::XMFLOAT3 ozoneAbsorption = {0.650f, 1.881f, 0.085f};
    float exposure = 1.0f;

    float mieAnisotropy = 0.76f;
    float aerosolOpticalDepth = 0.08f;
    float cloudCoverage = 0.36f;
    float cloudDensity = 0.46f;

    float cloudHeightKm = 3.4f;
    float cloudSpeed = 0.05f;
    float precipitation = 0.0f;
    float timeSeconds = 0.0f;

    DirectX::XMFLOAT3 groundAlbedo = {0.18f, 0.20f, 0.16f};
    float horizonFog = 0.18f;

    float sunAngularRadius = 0.004675f;
    float ozoneStrength = 0.32f;

    float cloudThicknessKm = 0.65f;
    float cloudScale = 1.0f;
    float cloudDetailStrength = 0.36f;
    float cloudCrispness = 1.7f;

    float cloudBaseSoftness = 0.30f;
    float cloudSilverLiningStrength = 0.55f;
    float cloudShadowStrength = 0.58f;
    float cloudFlowDirectionRadians = 0.0f;

    float starFieldRotationRadians = 0.0f;
    float starFieldDrift = 0.0f;
    float starIntensity = 1.8f;
    float starLimitingMagnitude = 6.95f;

    float starTwinkleStrength = 0.32f;
    float starColorSaturation = 0.90f;
    float milkyWayIntensity = 0.46f;
    float starMapAvailable = 0.0f;
};
