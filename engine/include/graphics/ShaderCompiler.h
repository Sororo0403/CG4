#pragma once
#include "core/AssetManager.h"
#include "debug/DebugLog.h"
#include "graphics/DxUtils.h"
#include <d3dcompiler.h>
#include <filesystem>
#include <string>
#include <wrl.h>

namespace ShaderCompiler {

/// <summary>
/// シェーダーファイルの実在パスを探索する
/// </summary>
/// <param name="path">探索対象の相対または絶対パス</param>
/// <returns>解決済みのシェーダーパス</returns>
inline std::wstring ResolveShaderPath(const std::wstring &path) {
    return AssetManager::ResolvePath(std::filesystem::path(path)).wstring();
}

/// <summary>
/// HLSLシェーダーをコンパイルする
/// </summary>
/// <param name="path">シェーダーファイルのパス</param>
/// <param name="entry">エントリポイント名</param>
/// <param name="target">シェーダーモデル</param>
/// <returns>コンパイル済みシェーダーバイナリ</returns>
inline Microsoft::WRL::ComPtr<ID3DBlob> Compile(const std::wstring &path,
                                                const std::string &entry,
                                                const std::string &target) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    const std::wstring resolvedPath = ResolveShaderPath(path);

    HRESULT hr = D3DCompileFromFile(
        resolvedPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry.c_str(), target.c_str(), flags, 0, &shader, &error);

    if (FAILED(hr)) {
        if (error) {
            const char *message =
                static_cast<const char *>(error->GetBufferPointer());
            OutputDebugStringA(message);
#ifdef _DEBUG
            DebugLog::Get().Write(
                "Graphics", "ShaderCompiler", "compile_failed", message,
                {{"path", std::filesystem::path(resolvedPath).generic_string()},
                 {"entry", entry},
                 {"target", target}});
#endif
        }
        DxUtils::ThrowIfFailed(hr, "D3DCompileFromFile failed");
    }

    return shader;
}

} // namespace ShaderCompiler
