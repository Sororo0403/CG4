#pragma once
#include "DxUtils.h"
#include <d3dcompiler.h>
#include <filesystem>
#include <string>
#include <wrl.h>

namespace ShaderCompiler {

inline std::wstring ResolveShaderPath(const std::wstring &path) {
    namespace fs = std::filesystem;

    fs::path candidate(path);
    if (fs::exists(candidate)) {
        return candidate.wstring();
    }

    fs::path cwd = fs::current_path();
    if (fs::exists(cwd / candidate)) {
        return (cwd / candidate).wstring();
    }

    for (fs::path dir = cwd; !dir.empty(); dir = dir.parent_path()) {
        if (fs::exists(dir / candidate)) {
            return (dir / candidate).wstring();
        }

        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory()) {
                continue;
            }

            fs::path childCandidate = entry.path() / candidate;
            if (fs::exists(childCandidate)) {
                return childCandidate.wstring();
            }
        }

        if (dir == dir.root_path()) {
            break;
        }
    }

    return path;
}

inline Microsoft::WRL::ComPtr<ID3DBlob>
Compile(const std::wstring &path, const std::string &entry,
        const std::string &target) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif // _DEBUG

    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    const std::wstring resolvedPath = ResolveShaderPath(path);

    HRESULT hr = D3DCompileFromFile(resolvedPath.c_str(), nullptr,
                                    D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entry.c_str(), target.c_str(), flags, 0,
                                    &shader, &error);

    if (FAILED(hr)) {
        if (error) {
            OutputDebugStringA(
                static_cast<const char *>(error->GetBufferPointer()));
        }
        DxUtils::ThrowIfFailed(hr, "D3DCompileFromFile failed");
    }

    return shader;
}

} // namespace ShaderCompiler
