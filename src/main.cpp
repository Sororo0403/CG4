#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "SceneContext.h"
#include "SceneManager.h"
#include "SoundManager.h"
#include "SpriteManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <memory>

#ifdef _DEBUG
#include "ImguiManager.h"
#endif // _DEBUG

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // WinApp初期化
    WinApp winApp;
    winApp.Initialize(hInstance, nCmdShow, 1280, 720, L"3145_身技一体");

    // クライアント領域の幅と高さ
    int width = winApp.GetWidth();
    int height = winApp.GetHeight();

    // DirectX
    DirectXCommon dxCommon;
    dxCommon.Initialize(winApp.GetHwnd(), width, height);

    // SrvManager
    SrvManager srvManager;
    srvManager.Initialize(&dxCommon, 512);

    // Input
    Input input;
    input.Initialize(hInstance, winApp.GetHwnd());

    // SoundManager
    SoundManager soundManager;
    soundManager.Initialize();

    // TextureManager
    TextureManager textureManager;
    textureManager.Initialize(&dxCommon, &srvManager);

    // ModelManager
    ModelManager modelManager;
    modelManager.Initialize(&dxCommon, &srvManager, &textureManager);

    // SpriteManager
    SpriteManager spriteManager;
    spriteManager.Initialize(&dxCommon, &textureManager, &srvManager, width,
                             height);

    dxCommon.BeginUpload();

    dxCommon.EndUpload();

    textureManager.ReleaseUploadBuffers();

#ifdef _DEBUG
    // ImguiManager
    ImguiManager imguiManager;
    imguiManager.Initialize(&winApp, &dxCommon, &srvManager);
#endif // _DEBUG

    SceneContext sceneCtx{};
    sceneCtx.input = &input;
    sceneCtx.winApp = &winApp;
    sceneCtx.sound = &soundManager;
    sceneCtx.model = &modelManager;
    sceneCtx.sprite = &spriteManager;
    sceneCtx.texture = &textureManager;
    sceneCtx.dxCommon = &dxCommon;

    sceneCtx.deltaTime = 0.0f;

#ifdef _DEBUG
    sceneCtx.imgui = &imguiManager;
#endif // _DEBUG

    // SceneManager
    SceneManager sceneManager;
    sceneManager.Initialize(sceneCtx);
    sceneManager.ChangeScene(std::make_unique<GameScene>());

    // 高精細タイマの周波数を取得
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    LARGE_INTEGER prevTime;
    QueryPerformanceCounter(&prevTime);

    // メインループ
    while (winApp.ProcessMessage()) {
        // deltaTime計算
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);

        float deltaTime =
            static_cast<float>(currentTime.QuadPart - prevTime.QuadPart) /
            static_cast<float>(freq.QuadPart);

        prevTime = currentTime;

        sceneCtx.deltaTime = deltaTime;

        // 入力更新
        input.Update(deltaTime);

        // Scene 更新
        sceneManager.Update();

        // 描画
        dxCommon.BeginFrame();

#ifdef _DEBUG
        ID3D12GraphicsCommandList *cmdList = dxCommon.GetCommandList();
        imguiManager.Begin(cmdList);
#endif // _DEBUG

        sceneManager.Draw();

#ifdef _DEBUG
        imguiManager.End(cmdList);
#endif // _DEBUG

        dxCommon.EndFrame();
    }

    return 0;
}
