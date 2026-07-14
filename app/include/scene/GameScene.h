#pragma once

#include "Engine.h"

#ifdef DrawText
#undef DrawText
#endif

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class GameScene final : public BaseScene {
  public:
    /// <summary>
    /// GameSceneが保持するシーン固有リソースを解放する。
    /// </summary>
    ~GameScene() override;

    /// <summary>
    /// シーンのカメラ、モデル、GPUパーティクルを初期化する。
    /// </summary>
    /// <param name="ctx">エンジンの各サービスへアクセスするシーンコンテキスト。</param>
    void Initialize(const SceneContext &ctx) override;

    /// <summary>
    /// 入力、アニメーション、アタッチメント、GPUパーティクルをフレーム更新する。
    /// </summary>
    void Update() override;

    /// <summary>
    /// GameSceneで使用するライト情報をレンダリングシーンへ登録する。
    /// </summary>
    /// <param name="lightingScene">ライト情報の登録先。</param>
    void SubmitLighting(LightingScene &lightingScene) override;

    /// <summary>
    /// 不透明な背景、キャラクター、武器を描画する。
    /// </summary>
    void Draw() override;

    /// <summary>
    /// GPUパーティクルなどの半透明オブジェクトを描画する。
    /// </summary>
    void DrawTransparent() override;

    /// <summary>
    /// ポストプロセス後にボーンデバッグと編集UIを描画する。
    /// </summary>
    void DrawPostProcessOverlay() override;

  private:
    /// <summary>
    /// ウィンドウのアスペクト比に合わせてカメラを初期化する。
    /// </summary>
    void InitializeCamera();

    /// <summary>
    /// キャラクター、BrainStem、地面、武器、デバッグ形状を生成または読み込む。
    /// </summary>
    void InitializeModels();

    /// <summary>
    /// GPUパーティクル、複数Emitter、Field、ライト設定を初期化する。
    /// </summary>
    void InitializeParticles();

    /// <summary>
    /// 入力からキャラクターの移動量と向きを更新する。
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過秒数。</param>
    void UpdateMovement(float deltaTime);

    /// <summary>
    /// ボーン表示切り替えと選択操作を処理する。
    /// </summary>
    void UpdateDebugControls();

    /// <summary>
    /// 歩行とスニークのアニメーションを補間してスケルトンへ反映する。
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過秒数。</param>
    void UpdateModelAnimation(float deltaTime);

    /// <summary>
    /// アニメーション姿勢へ首と頭のLookAt IK補正を適用する。
    /// </summary>
    /// <param name="model">補正対象のスケルトンを保持するモデル。</param>
    /// <param name="localMatrices">補正するボーンのローカル姿勢行列。</param>
    void ApplyHeadLookAtIk(const Model& model,
                           std::vector<DirectX::XMMATRIX>& localMatrices) const;

    /// <summary>
    /// 手足のボーン位置からワールド空間のアタッチメント位置を更新する。
    /// </summary>
    void UpdateAttachmentPoints();

    /// <summary>
    /// 左右の手に追従するEmitterとキャラクター中心のFieldを更新する。
    /// </summary>
    void UpdateParticleEmitters();

    /// <summary>
    /// アニメーション済みキャラクターモデルを描画する。
    /// </summary>
    void DrawCharacter();

    /// <summary>
    /// 右手ボーンに追従する武器を描画する。
    /// </summary>
    void DrawWeapon();

    /// <summary>
    /// 地面とBrainStemモデルを描画する。
    /// </summary>
    void DrawSceneProps();

    /// <summary>
    /// 設定に応じたボーン階層線を画面上へ描画する。
    /// </summary>
    void DrawBoneDebugOverlay();

    /// <summary>
    /// バインドポーズのボーン階層線を描画する。
    /// </summary>
    /// <param name="model">描画対象のスケルトンを保持するモデル。</param>
    void DrawBindPoseBoneOverlay(const Model& model);

    /// <summary>
    /// 現在のアニメーション姿勢のボーン階層線を描画する。
    /// </summary>
    /// <param name="model">描画対象のスケルトンを保持するモデル。</param>
    void DrawAnimatedBoneOverlay(const Model& model);

    /// <summary>
    /// 選択中ボーンのローカルXYZ軸を赤、緑、青の線で描画する。
    /// </summary>
    /// <param name="model">軸を描画するスケルトンを保持するモデル。</param>
    void DrawSelectedBoneAxes(const Model& model);

    /// <summary>
    /// 頭からLookAtターゲットまでの線とターゲットマーカーを描画する。
    /// </summary>
    void DrawLookAtDebug();

    /// <summary>
    /// ボーン選択とアニメーション操作用のデバッグパネルを描画する。
    /// </summary>
    void DrawDebugPanel();

    /// <summary>
    /// Emitter、Field、Trail、ライトを操作するGPU Particle Editorを描画する。
    /// </summary>
    void DrawParticleEditor();

    /// <summary>
    /// 1つのEmitter設定を編集するUIを描画する。
    /// </summary>
    /// <param name="label">UI上に表示するEmitter名。</param>
    /// <param name="settings">表示および更新するEmitter設定。</param>
    /// <param name="enabled">Emitterの有効状態。</param>
    /// <returns>UI操作によって設定または有効状態が変更された場合はtrue。</returns>
    bool DrawParticleEmitterControls(const char* label, ParticleEmitterSettings& settings,
                                     bool& enabled);

    /// <summary>
    /// 全Particle Fieldの設定を編集するUIを描画する。
    /// </summary>
    /// <returns>いずれかのField設定が変更された場合はtrue。</returns>
    bool DrawParticleFieldControls();

    /// <summary>
    /// 選択中またはマウスで指しているボーン名を描画する。
    /// </summary>
    void DrawBoneLabels();

    /// <summary>
    /// マウスカーソルに最も近い画面内のボーンを検索する。
    /// </summary>
    /// <param name="model">検索対象のスケルトンを保持するモデル。</param>
    /// <returns>見つかったボーンのインデックス。対象がない場合は-1。</returns>
    int FindHoveredBone(const Model& model) const;

    /// <summary>
    /// 指定したボーン名を背景とリーダー線付きで描画する。
    /// </summary>
    /// <param name="model">ボーン名を保持するモデル。</param>
    /// <param name="boneIndex">描画するボーンのインデックス。</param>
    /// <param name="selected">選択中の強調表示を行う場合はtrue。</param>
    void DrawBoneLabel(const Model& model, uint32_t boneIndex, bool selected);

    /// <summary>
    /// 候補名に一致するキャラクターのボーンを検索する。
    /// </summary>
    /// <param name="candidates">完全一致または部分一致を試すボーン名候補。</param>
    /// <returns>見つかったボーンのインデックス。対象がない場合は無効リソースID。</returns>
    uint32_t FindBoneIndex(const std::vector<std::string> &candidates) const;

    /// <summary>
    /// アニメーション姿勢のボーン位置をワールド空間で取得する。
    /// </summary>
    /// <param name="boneIndex">取得するボーンのインデックス。</param>
    /// <returns>ボーンのワールド座標。</returns>
    DirectX::XMFLOAT3 BoneWorldPosition(uint32_t boneIndex) const;

    /// <summary>
    /// アニメーション姿勢のボーン行列をワールド空間で取得する。
    /// </summary>
    /// <param name="boneIndex">取得するボーンのインデックス。</param>
    /// <returns>ボーンのワールド行列。取得できない場合はキャラクターのワールド行列。</returns>
    DirectX::XMMATRIX BoneWorldMatrix(uint32_t boneIndex) const;

    /// <summary>
    /// バインドポーズのボーン位置をワールド空間で取得する。
    /// </summary>
    /// <param name="boneIndex">取得するボーンのインデックス。</param>
    /// <returns>バインドポーズにおけるボーンのワールド座標。</returns>
    DirectX::XMFLOAT3 BindBoneWorldPosition(uint32_t boneIndex) const;

    /// <summary>
    /// キャラクターの位置、回転、スケールからワールド行列を生成する。
    /// </summary>
    /// <returns>キャラクターのワールド行列。</returns>
    DirectX::XMMATRIX CharacterWorldMatrix() const;

    /// <summary>
    /// ボーン名が主要ボーンの命名規則に該当するか判定する。
    /// </summary>
    /// <param name="boneName">判定するボーン名。</param>
    /// <returns>主要ボーンに該当する場合はtrue。</returns>
    static bool IsMajorDebugBone(const std::string &boneName);

    /// <summary>
    /// ボーン名からインポート時の名前空間接頭辞を取り除く。
    /// </summary>
    /// <param name="boneName">表示用に変換するボーン名。</param>
    /// <returns>デバッグ表示用のボーン名。</returns>
    static std::string DisplayBoneName(const std::string &boneName);

    /// <summary>
    /// ワールド座標を現在のカメラでスクリーン座標へ射影する。
    /// </summary>
    /// <param name="world">射影するワールド座標。</param>
    /// <param name="screen">射影結果を書き込むスクリーン座標。</param>
    /// <returns>座標がカメラ前方にあり射影できた場合はtrue。</returns>
    bool ProjectWorldToScreen(const DirectX::XMFLOAT3 &world,
                              DirectX::XMFLOAT2 &screen) const;

    /// <summary>
    /// 2つのワールド座標を射影し、ボーン用の画面線分として描画する。
    /// </summary>
    /// <param name="a">線分の始点となるワールド座標。</param>
    /// <param name="b">線分の終点となるワールド座標。</param>
    /// <param name="color">ImGui形式の線色。</param>
    /// <param name="thickness">線の太さ。</param>
    void DrawScreenBoneLine(const DirectX::XMFLOAT3 &a,
                            const DirectX::XMFLOAT3 &b, uint32_t color,
                            float thickness);

    Camera camera_{};
    uint32_t humanModelId_ = kInvalidResourceId;
    uint32_t sneakModelId_ = kInvalidResourceId;
    uint32_t brainStemModelId_ = kInvalidResourceId;
    uint32_t groundModelId_ = kInvalidResourceId;
    uint32_t weaponHandleModelId_ = kInvalidResourceId;
    uint32_t weaponGuardModelId_ = kInvalidResourceId;
    uint32_t weaponBladeModelId_ = kInvalidResourceId;
    uint32_t boneJointModelId_ = kInvalidResourceId;
    uint32_t boneCenterModelId_ = kInvalidResourceId;
    uint32_t boneLeftModelId_ = kInvalidResourceId;
    uint32_t boneRightModelId_ = kInvalidResourceId;
    uint32_t boneBindModelId_ = kInvalidResourceId;
    uint32_t boneSelectedModelId_ = kInvalidResourceId;
    uint32_t whiteTextureId_ = kInvalidResourceId;
    std::vector<DirectX::XMFLOAT4X4> bindPoseMatrices_;

    Transform characterTransform_{};
    float characterYaw_ = 0.0f;
    float walkTimeScale_ = 1.0f;
    float animationTime_ = 0.0f;
    float sneakBlend_ = 0.0f;
    float manualSneakBlend_ = -1.0f;
    bool lookAtIkEnabled_ = true;
    float lookAtWeight_ = 0.85f;
    float lookAtMaxAngleDegrees_ = 72.0f;
    int lookAtForwardAxis_ = 4;
    DirectX::XMFLOAT3 lookAtTarget_{1.2f, 1.55f, 2.0f};

    uint32_t rightHandBone_ = kInvalidResourceId;
    uint32_t leftHandBone_ = kInvalidResourceId;
    uint32_t rightFootBone_ = kInvalidResourceId;
    uint32_t leftFootBone_ = kInvalidResourceId;
    uint32_t neckBone_ = kInvalidResourceId;
    uint32_t headBone_ = kInvalidResourceId;

    DirectX::XMFLOAT3 rightHandWorld_{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 leftHandWorld_{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 rightFootWorld_{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 leftFootWorld_{0.0f, 0.0f, 0.0f};

    bool debugRigEnabled_ = true;
    bool debugLabelsEnabled_ = true;
    bool debugLocalAxesEnabled_ = true;
    bool debugBindPoseEnabled_ = false;
    bool debugMajorBonesOnly_ = false;
    int selectedBoneIndex_ = -1;

    GPUParticleSystem handParticles_{};
    bool handParticlesReady_ = false;
    uint32_t rightParticleEmitterId_ = kInvalidParticleEmitterId;
    uint32_t leftParticleEmitterId_ = kInvalidParticleEmitterId;
    ParticleEmitterSettings rightParticleEmitter_{};
    ParticleEmitterSettings leftParticleEmitter_{};
    std::vector<ParticleFieldSettings> particleFields_;
    GPUParticleMaterialSettings particleMaterial_{};
    GPUParticleLightingSettings particleLighting_{};
    bool rightParticleEmitterEnabled_ = true;
    bool leftParticleEmitterEnabled_ = true;
    bool particleTrailEnabled_ = false;
};
