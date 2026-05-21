#pragma once
#include "graphics/PostEffectSettings.h"
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

/// <summary>
/// テクスチャを画面全体へ描画するポストエフェクト用レンダラー
/// </summary>
class PostEffectRenderer {
  public:
    using ColorMode = PostEffectColorMode;
    using FilterMode = PostEffectFilterMode;
    using EdgeMode = PostEffectEdgeMode;
    using SpecialMode = PostEffectSpecialMode;

    /// <summary>
    /// ポストエフェクト描画に必要なパイプラインと定数バッファを初期化する
    /// </summary>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager, int width,
                    int height);

    /// <summary>
    /// 描画領域を更新する
    /// </summary>
    void Resize(int width, int height);

    /// <summary>
    /// 指定SRVを全画面へ描画する
    /// </summary>
    void Draw(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
              D3D12_GPU_DESCRIPTOR_HANDLE depthHandle);

    /// <summary>
    /// 色変換エフェクトの種類を設定する
    /// </summary>
    void SetColorMode(ColorMode mode);

    /// <summary>
    /// 現在の色変換エフェクトの種類を取得する
    /// </summary>
    ColorMode GetColorMode() const { return colorMode_; }

    /// <summary>
    /// 平滑化フィルターの種類を設定する
    /// </summary>
    void SetFilterMode(FilterMode mode);

    /// <summary>
    /// 現在の平滑化フィルターの種類を取得する
    /// </summary>
    FilterMode GetFilterMode() const { return filterMode_; }

    /// <summary>
    /// エッジ抽出の種類を設定する
    /// </summary>
    void SetEdgeMode(EdgeMode mode);

    /// <summary>
    /// 現在のエッジ抽出の種類を取得する
    /// </summary>
    EdgeMode GetEdgeMode() const { return edgeMode_; }

    /// <summary>
    /// 輝度エッジの閾値を設定する
    /// </summary>
    void SetLuminanceEdgeThreshold(float threshold);

    /// <summary>
    /// 輝度エッジの閾値を取得する
    /// </summary>
    float GetLuminanceEdgeThreshold() const { return luminanceEdgeThreshold_; }

    /// <summary>
    /// 深度エッジの閾値を設定する
    /// </summary>
    void SetDepthEdgeThreshold(float threshold);

    /// <summary>
    /// 深度エッジの閾値を取得する
    /// </summary>
    float GetDepthEdgeThreshold() const { return depthEdgeThreshold_; }

    /// <summary>
    /// 深度復元に使うNear/Farを設定する
    /// </summary>
    void SetDepthParameters(float nearZ, float farZ);

    /// <summary>
    /// グレースケール変換の輝度係数を設定する
    /// </summary>
    void SetGrayscaleWeights(float r, float g, float b);

    /// <summary>
    /// HDRトーンマップの有効状態を設定する
    /// </summary>
    void SetTonemapEnabled(bool enabled);

    /// <summary>
    /// HDRトーンマップの露光を設定する
    /// </summary>
    void SetExposure(float exposure);

    /// <summary>
    /// HDRトーンマップのガンマを設定する
    /// </summary>
    void SetGamma(float gamma);

    /// <summary>
    /// 簡易Bloomの有効状態を設定する
    /// </summary>
    void SetBloomEnabled(bool enabled);

    /// <summary>
    /// 簡易Bloomの閾値、強度、半径を設定する
    /// </summary>
    void SetBloom(float threshold, float intensity, float radius);

    /// <summary>
    /// ポストノイズの有効状態を設定する
    /// </summary>
    void SetNoiseEnabled(bool enabled);

    /// <summary>
    /// ポストノイズの強度とスケールを設定する
    /// </summary>
    void SetNoise(float strength, float scale);

    /// <summary>
    /// ポストノイズの時間を設定する
    /// </summary>
    void SetNoiseTime(float time);

    /// <summary>
    /// 追加ポストエフェクトの種類を設定する
    /// </summary>
    void SetSpecialMode(SpecialMode mode);

    /// <summary>
    /// 周辺減光の強度と半径を設定する
    /// </summary>
    void SetVignette(float strength, float radius);

    /// <summary>
    /// 放射ブラーの強度を設定する
    /// </summary>
    void SetRadialBlur(float strength);

    /// <summary>
    /// ディゾルブのしきい値、ぼかし幅、ノイズスケールを設定する
    /// </summary>
    void SetDissolve(float amount, float softness, float scale);

    /// <summary>
    /// HDRトーンマップの有効状態を取得する
    /// </summary>
    bool IsTonemapEnabled() const { return tonemapEnabled_; }

    /// <summary>
    /// HDRトーンマップの露光を取得する
    /// </summary>
    float GetExposure() const { return exposure_; }

    /// <summary>
    /// HDRトーンマップのガンマを取得する
    /// </summary>
    float GetGamma() const { return gamma_; }

    /// <summary>
    /// 簡易Bloomの有効状態を取得する
    /// </summary>
    bool IsBloomEnabled() const { return bloomEnabled_; }

    /// <summary>
    /// ポストノイズの有効状態を取得する
    /// </summary>
    bool IsNoiseEnabled() const { return noiseEnabled_; }

    /// <summary>
    /// 現在の追加ポストエフェクトの種類を取得する
    /// </summary>
    SpecialMode GetSpecialMode() const { return specialMode_; }

  private:
    /// <summary>
    /// ルートシグネチャを生成する
    /// </summary>
    void CreateRootSignature();

    /// <summary>
    /// パイプラインステートを生成する
    /// </summary>
    void CreatePipelineState();

    /// <summary>
    /// エフェクト用定数バッファを生成する
    /// </summary>
    void CreateConstantBuffer();

    /// <summary>
    /// 現在のエフェクト設定を定数バッファへ書き込む
    /// </summary>
    void UpdateConstantBuffer();

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;
    PostEffectConstants *mappedConstBuffer_ = nullptr;
    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};
    ColorMode colorMode_ = ColorMode::None;
    FilterMode filterMode_ = FilterMode::None;
    EdgeMode edgeMode_ = EdgeMode::None;
    float grayscaleWeights_[3]{0.2125f, 0.7154f, 0.0721f};
    float luminanceEdgeThreshold_ = 0.2f;
    float depthEdgeThreshold_ = 0.02f;
    float nearZ_ = 0.1f;
    float farZ_ = 100.0f;
    bool tonemapEnabled_ = true;
    float exposure_ = 1.0f;
    float gamma_ = 2.2f;
    bool bloomEnabled_ = false;
    float bloomThreshold_ = 1.0f;
    float bloomIntensity_ = 0.25f;
    float bloomRadius_ = 2.0f;
    bool noiseEnabled_ = false;
    float noiseStrength_ = 0.025f;
    float noiseScale_ = 240.0f;
    float noiseTime_ = 0.0f;
    SpecialMode specialMode_ = SpecialMode::None;
    float vignetteStrength_ = 0.0f;
    float vignetteRadius_ = 0.72f;
    float radialBlurStrength_ = 0.0f;
    float dissolveAmount_ = 0.0f;
    float dissolveSoftness_ = 0.08f;
    float dissolveScale_ = 42.0f;
    int width_ = 1;
    int height_ = 1;
};
