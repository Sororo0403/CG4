#include "Model.hlsli"

Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

cbuffer Transform : register(b0)
{
    float4x4 matWVP;
    float4x4 matWorld;
    float4 cameraPos;
    float4 effectColor;
    float4 effectParams;
    float4 keyLightDirection;
    float4 keyLightColor;
    float4 fillLightDirection;
    float4 fillLightColor;
    float4 ambientColor;
    float4 pointLight0PositionRange;
    float4 pointLight0ColorIntensity;
    float4 pointLight1PositionRange;
    float4 pointLight1ColorIntensity;
    float4 lightingParams;
};

cbuffer Material : register(b2)
{
    float4 color;
    float4x4 uvTransform;
    int enableTexture;
    float3 padding;
};

float4 main(ModelVSOutput input) : SV_TARGET
{
    float2 uv = mul(float4(input.uv, 0.0f, 1.0f), uvTransform).xy;

    float4 texColor = float4(1, 1, 1, 1);
    if (enableTexture != 0)
    {
        texColor = tex0.Sample(samp0, uv);
    }

    float4 finalColor = texColor * color;
    float3 normal = normalize(input.worldNormal);
    float3 viewDir = normalize(cameraPos.xyz - input.worldPos);

    float3 keyDir = normalize(-keyLightDirection.xyz);
    float3 fillDir = normalize(-fillLightDirection.xyz);

    float keyDiffuse = saturate(dot(normal, keyDir));
    float fillDiffuse = saturate(dot(normal, fillDir));
    float wrap = saturate(lightingParams.w);
    keyDiffuse = saturate((keyDiffuse + wrap) / (1.0f + wrap));
    fillDiffuse = saturate((fillDiffuse + wrap * 0.5f) / (1.0f + wrap * 0.5f));

    float3 halfVector = normalize(keyDir + viewDir);
    float specularPower = max(lightingParams.x, 1.0f);
    float specularStrength = saturate(lightingParams.y);
    float keySpecular = pow(saturate(dot(normal, halfVector)), specularPower) * specularStrength;

    float rimPower = max(lightingParams.z, 0.5f);
    float rim = pow(saturate(1.0f - dot(normal, viewDir)), rimPower);

    float3 pointAccum = float3(0.0f, 0.0f, 0.0f);

    float3 point0Vector = pointLight0PositionRange.xyz - input.worldPos;
    float point0Distance = length(point0Vector);
    if (point0Distance > 0.0001f)
    {
        float3 point0Dir = point0Vector / point0Distance;
        float point0Attenuation = saturate(1.0f - point0Distance / max(pointLight0PositionRange.w, 0.001f));
        point0Attenuation *= point0Attenuation;
        float point0Diffuse = saturate(dot(normal, point0Dir));
        pointAccum += pointLight0ColorIntensity.rgb * point0Diffuse *
                      point0Attenuation * pointLight0ColorIntensity.w;
    }

    float3 point1Vector = pointLight1PositionRange.xyz - input.worldPos;
    float point1Distance = length(point1Vector);
    if (point1Distance > 0.0001f)
    {
        float3 point1Dir = point1Vector / point1Distance;
        float point1Attenuation = saturate(1.0f - point1Distance / max(pointLight1PositionRange.w, 0.001f));
        point1Attenuation *= point1Attenuation;
        float point1Diffuse = saturate(dot(normal, point1Dir));
        pointAccum += pointLight1ColorIntensity.rgb * point1Diffuse *
                      point1Attenuation * pointLight1ColorIntensity.w;
    }

    float3 lighting =
        ambientColor.rgb +
        keyLightColor.rgb * keyDiffuse +
        fillLightColor.rgb * fillDiffuse +
        pointAccum +
        keyLightColor.rgb * keySpecular +
        fillLightColor.rgb * rim * fillLightColor.a;

    finalColor.rgb *= lighting;

    float effectIntensity = effectParams.x;
    if (effectIntensity > 0.0001f)
    {
        float fresnelPower = max(effectParams.y, 0.5f);
        float noiseAmount = saturate(effectParams.z);
        float time = effectParams.w;

        float effectRim = pow(saturate(1.0f - abs(dot(normal, viewDir))),
                        fresnelPower);

        float noise =
            sin(input.worldPos.x * 10.0f + time * 15.0f) *
            sin(input.worldPos.y * 12.0f - time * 11.0f) *
            sin(input.worldPos.z * 9.0f + time * 17.0f);
        noise = lerp(1.0f, 0.65f + 0.35f * noise, noiseAmount);

        float pulse = 0.8f + 0.2f * sin(time * 20.0f + input.worldPos.y * 8.0f);
        float glow = effectRim * noise * pulse * effectIntensity;

        float bodyFade = saturate(effectIntensity * 0.38f);
        finalColor.rgb *= lerp(1.0f, 0.24f, bodyFade);
        finalColor.rgb = lerp(finalColor.rgb, finalColor.rgb * float3(0.28f, 0.08f, 0.16f),
                              bodyFade * 0.75f);

        finalColor.rgb += effectColor.rgb * glow;
        finalColor.a = saturate(finalColor.a + effectColor.a * glow * 0.55f);
    }

    return finalColor;
}
