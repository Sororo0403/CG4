#pragma once

class Input;
class WinApp;
class SoundManager;
class ModelManager;
class SpriteManager;
class TextureManager;
class DirectXCommon;

#ifdef _DEBUG
class ImguiManager;
#endif // _DEBUG

struct SceneContext {
    Input *input = nullptr;
    WinApp *winApp = nullptr;
    SoundManager *sound = nullptr;
    ModelManager *model = nullptr;
    SpriteManager *sprite = nullptr;
    TextureManager *texture = nullptr;
    DirectXCommon *dxCommon = nullptr;
    float deltaTime = 0.0f;

#ifdef _DEBUG
    ImguiManager *imgui = nullptr;
#endif // _DEBUG
};
