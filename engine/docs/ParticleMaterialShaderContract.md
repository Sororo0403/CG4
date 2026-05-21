# Particle Material Shader Contract

Particle material shaders are effect assets. Add a new particle look by adding a
pixel shader and referencing it from an effect JSON file. Do not add a C++ enum
or a branch for each material look.

## JSON

`material` may be a string for the default particle shader, or an object for a
custom pixel shader.

```json
{
  "material": {
    "name": "soft_fiber",
    "pixelShader": "../../../engine/resources/shaders/particle/materials/SoftFiberPS.hlsl",
    "paramBindings": {
      "edgeSoftness": "params0.x",
      "fiberStrength": "params0.y",
      "alphaNoise": "params0.z",
      "innerGlow": "params0.w",
      "fiberScale": "params1.x"
    },
    "params": {
      "edgeSoftness": 0.42,
      "fiberStrength": 0.35,
      "alphaNoise": 0.22,
      "innerGlow": 0.10,
      "fiberScale": 18.0
    }
  },
  "texture": "../textures/plush_shapes_atlas.png",
  "noiseTexture": "../textures/plush_fiber_noise.png"
}
```

`params0` and `params1` arrays are still supported for quick tests:

```json
"params0": [0.42, 0.35, 0.22, 0.10],
"params1": [18.0, 0.0, 0.0, 0.0]
```

## HLSL Registers

All custom particle pixel shaders must use this binding contract:

```hlsl
#include "../GPUParticle.hlsli"

cbuffer ParticleDrawParams : register(b0)
{
    float4x4 viewProjection;
    float4 cameraRight;
    float4 cameraUp;
    float4 tintColor;
    float4 atlasInfo;
    float4 materialParams0;
    float4 materialParams1;
};

Texture2D particleTexture : register(t1);
Texture2D particleNoiseTexture : register(t2);
SamplerState particleSampler : register(s0);
```

`t0` is reserved for the internal particle structured buffer used by
`GPUParticleVS.hlsl`. Pixel shaders should sample `particleTexture` from `t1`
and optional noise or mask data from `t2`.

## Pipeline Cache

The particle draw root signature is shared. Draw PSOs are cached by pixel shader
path, so repeated layers using the same `pixelShader` reuse the same PSO.

The cache key currently assumes:

- renderer: `billboard`
- blend: alpha blend
- depth: disabled
- render target format: `DirectXCommon::kSceneColorFormat`

If renderer, blend, or depth become JSON options later, they must be added to
the cache key.
