#include "PostProcessCommon.hlsli"
#include "ColorGradingPS.hlsli"
#include "BlurPS.hlsli"
#include "EdgePS.hlsli"
#include "BloomPS.hlsli"
#include "VignettePS.hlsli"
#include "ToonPS.hlsli"
#include "NoisePS.hlsli"
#include "LensFlarePS.hlsli"

Texture2D renderTexture : register(t0);
Texture2D depthTexture : register(t1);
Texture2D bloomTexture : register(t2);
SamplerState textureSampler : register(s0);

bool IsPostProcessBypass()
{
    return colorMode == 0 &&
           filterMode == 0 &&
           edgeMode == 0 &&
           tonemapEnabled == 0 &&
           bloomEnabled == 0 &&
           noiseEnabled == 0 &&
           (specialMode == 0 || specialMode == 2) &&
           lensFlareEnabled == 0 &&
           enableVignetting == 0 &&
           randomMode == 0 &&
           radialBlurStrength <= 0.0f &&
           sceneDimStrength <= 0.0f &&
           primaryVignetteTintStrength <= 0.0f &&
           secondaryVignetteTintStrength <= 0.0f &&
           (toonEnabled == 0 || toonStrength <= 0.0f);
}

float4 main(PostProcessVSOutput input) : SV_TARGET
{
    if (IsPostProcessBypass())
    {
        return renderTexture.Sample(textureSampler, input.uv);
    }

    float4 outputColor = ApplyBlur(renderTexture, textureSampler, input.uv);
    outputColor = ApplyRadialBlur(renderTexture, textureSampler, input.uv,
                                  outputColor);

    outputColor.rgb = ApplyColorGrading(outputColor.rgb);

    outputColor = ApplyEdge(outputColor, renderTexture, depthTexture,
                            textureSampler, input.uv);

    outputColor.rgb = ApplyBloom(bloomTexture, textureSampler, outputColor.rgb,
                                 input.uv);
    outputColor.rgb = ApplyLensFlare(depthTexture, textureSampler,
                                     outputColor.rgb, input.uv);
    outputColor.rgb = ApplyToneMapping(outputColor.rgb);
    outputColor.rgb = ApplyNoise(outputColor.rgb, input.uv);
    outputColor.rgb = ApplyVignetting(outputColor.rgb, input.uv);
    outputColor.rgb *= 1.0f - saturate(sceneDimStrength) * 0.62f;
    outputColor.rgb = ApplyVignetteTint(outputColor.rgb, input.uv,
                                        primaryVignetteTintStrength,
                                        primaryVignetteTintColor);
    outputColor.rgb = ApplyVignetteTint(outputColor.rgb, input.uv,
                                        secondaryVignetteTintStrength,
                                        secondaryVignetteTintColor);
    outputColor.rgb = ApplySpecialEffect(outputColor.rgb, input.uv);
    outputColor.rgb =
        ApplyToon(renderTexture, textureSampler, outputColor.rgb, input.uv);
    outputColor = ApplyRandomNoise(outputColor, input.uv);

    return outputColor;
}
