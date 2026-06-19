#pragma once
#include "core/AssetManager.h"
#include <Windows.h>
#include <dxcapi.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <wrl.h>

namespace ShaderCompiler {

/// <summary>
/// シェーダーファイルの実在パスを探索する
/// </summary>
/// <param name="path">探索対象の相対または絶対パス</param>
/// <returns>解決済みのシェーダーパス</returns>
inline std::wstring ResolveShaderPath(const std::wstring &path) {
    // Shader files are fixed engine/app resources, so allow parent-directory
    // search when the executable is launched from a generated output folder.
    return AssetManager::ResolvePath(std::filesystem::path(path)).wstring();
}

inline std::wstring Widen(const std::string &value) {
    return std::wstring(value.begin(), value.end());
}

inline std::wstring NormalizeShaderTarget(const std::string &target) {
    if (target.rfind("vs_", 0) == 0) {
        return L"vs_6_6";
    }
    if (target.rfind("ps_", 0) == 0) {
        return L"ps_6_6";
    }
    if (target.rfind("cs_", 0) == 0) {
        return L"cs_6_6";
    }
    if (target.rfind("ms_", 0) == 0) {
        return L"ms_6_6";
    }
    if (target.rfind("as_", 0) == 0) {
        return L"as_6_6";
    }
    if (target.rfind("lib_", 0) == 0) {
        return L"lib_6_6";
    }
    return {};
}

inline std::string BlobToString(IDxcBlobUtf8 *blob) {
    if (!blob || blob->GetStringLength() == 0) {
        return {};
    }
    return std::string(blob->GetStringPointer(), blob->GetStringLength());
}

inline std::string NarrowAscii(const std::wstring &value) {
    std::string result;
    result.resize(value.size());
    std::transform(value.begin(), value.end(), result.begin(),
                   [](wchar_t ch) { return static_cast<char>(ch); });
    return result;
}

inline constexpr uint32_t kShaderCacheVersion = 1u;
inline constexpr uint64_t kShaderHashOffset = 1469598103934665603ull;
inline constexpr uint64_t kShaderHashPrime = 1099511628211ull;

inline const char *ShaderBuildConfigName() {
#ifdef _DEBUG
    return "debug";
#else
    return "release";
#endif
}

inline void HashAppendBytes(uint64_t &hash, const void *data, size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t index = 0u; index < size; ++index) {
        hash ^= static_cast<uint64_t>(bytes[index]);
        hash *= kShaderHashPrime;
    }
}

inline void HashAppendString(uint64_t &hash, const std::string &value) {
    HashAppendBytes(hash, value.data(), value.size());
    const uint8_t terminator = 0u;
    HashAppendBytes(hash, &terminator, sizeof(terminator));
}

inline void HashAppendWideString(uint64_t &hash, const std::wstring &value) {
    HashAppendBytes(hash, value.data(), value.size() * sizeof(wchar_t));
    const wchar_t terminator = L'\0';
    HashAppendBytes(hash, &terminator, sizeof(terminator));
}

inline bool ReadBinaryFile(const std::filesystem::path &path,
                           std::vector<uint8_t> &bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    bytes.resize(static_cast<size_t>(size));
    if (bytes.empty()) {
        return true;
    }
    in.read(reinterpret_cast<char *>(bytes.data()), size);
    return in.good();
}

inline std::filesystem::path CanonicalPathBestEffort(
    const std::filesystem::path &path) {
    std::error_code error;
    std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return canonical;
    }
    error.clear();
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? path.lexically_normal() : absolute.lexically_normal();
}

inline bool HashShaderDependencyFile(
    const std::filesystem::path &path, uint64_t &hash,
    std::unordered_set<std::wstring> &visited, uint32_t depth) {
    if (depth > 16u) {
        return false;
    }

    const std::filesystem::path canonical = CanonicalPathBestEffort(path);
    const std::wstring key = canonical.wstring();
    if (!visited.insert(key).second) {
        return true;
    }

    std::vector<uint8_t> bytes;
    if (!ReadBinaryFile(canonical, bytes)) {
        return false;
    }
    HashAppendWideString(hash, key);
    HashAppendBytes(hash, bytes.data(), bytes.size());

    const std::string text(bytes.begin(), bytes.end());
    size_t lineStart = 0u;
    while (lineStart < text.size()) {
        const size_t lineEnd = text.find_first_of("\r\n", lineStart);
        const std::string line =
            text.substr(lineStart, lineEnd == std::string::npos
                                       ? std::string::npos
                                       : lineEnd - lineStart);
        const size_t includePos = line.find("#include");
        if (includePos != std::string::npos) {
            const size_t quoteStart = line.find('"', includePos);
            const size_t quoteEnd =
                quoteStart == std::string::npos
                    ? std::string::npos
                    : line.find('"', quoteStart + 1u);
            if (quoteStart != std::string::npos &&
                quoteEnd != std::string::npos && quoteEnd > quoteStart + 1u) {
                const std::string includeName =
                    line.substr(quoteStart + 1u, quoteEnd - quoteStart - 1u);
                const std::filesystem::path includePath =
                    canonical.parent_path() / Widen(includeName);
                if (!HashShaderDependencyFile(includePath, hash, visited,
                                              depth + 1u)) {
                    return false;
                }
            }
        }
        if (lineEnd == std::string::npos) {
            break;
        }
        lineStart = lineEnd + 1u;
    }
    return true;
}

inline bool ComputeShaderDependencyHash(const std::filesystem::path &path,
                                        const std::string &entry,
                                        const std::wstring &target,
                                        uint64_t &hash) {
    hash = kShaderHashOffset;
    HashAppendBytes(hash, &kShaderCacheVersion, sizeof(kShaderCacheVersion));
    HashAppendString(hash, ShaderBuildConfigName());
    HashAppendString(hash, entry);
    HashAppendWideString(hash, target);
    std::unordered_set<std::wstring> visited;
    return HashShaderDependencyFile(path, hash, visited, 0u);
}

inline std::filesystem::path ShaderCachePath(uint64_t hash) {
    std::ostringstream name;
    name << "shader_v" << kShaderCacheVersion << '_' << ShaderBuildConfigName()
         << '_' << std::hex << std::setw(16) << std::setfill('0') << hash
         << ".cso";
    return AssetManager::GetAssetRoot() / "generated" / "shader_cache" /
           name.str();
}

inline std::mutex &ShaderCacheMutex() {
    static std::mutex mutex;
    return mutex;
}

inline std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IDxcBlob>> &
ShaderMemoryCache() {
    static std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IDxcBlob>>
        cache;
    return cache;
}

inline Microsoft::WRL::ComPtr<IDxcBlob>
CreateBlobFromBytes(const std::vector<uint8_t> &bytes) {
    using Microsoft::WRL::ComPtr;
    if (bytes.empty() || bytes.size() > UINT32_MAX) {
        return {};
    }

    ComPtr<IDxcUtils> utils;
    if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
        !utils) {
        return {};
    }
    ComPtr<IDxcBlobEncoding> encoded;
    if (FAILED(utils->CreateBlob(bytes.data(),
                                  static_cast<UINT32>(bytes.size()),
                                  DXC_CP_ACP, &encoded)) ||
        !encoded) {
        return {};
    }
    ComPtr<IDxcBlob> blob;
    if (FAILED(encoded.As(&blob)) || !blob) {
        return {};
    }
    return blob;
}

inline Microsoft::WRL::ComPtr<IDxcBlob>
TryLoadCachedShader(const std::filesystem::path &path) {
    using Microsoft::WRL::ComPtr;
    const std::wstring key = path.wstring();
    {
        std::lock_guard<std::mutex> lock(ShaderCacheMutex());
        auto cached = ShaderMemoryCache().find(key);
        if (cached != ShaderMemoryCache().end()) {
            return cached->second;
        }
    }

    std::vector<uint8_t> bytes;
    if (!ReadBinaryFile(path, bytes)) {
        return {};
    }
    ComPtr<IDxcBlob> blob = CreateBlobFromBytes(bytes);
    if (!blob) {
        return {};
    }
    std::lock_guard<std::mutex> lock(ShaderCacheMutex());
    ShaderMemoryCache()[key] = blob;
    return blob;
}

inline void SaveCachedShader(const std::filesystem::path &path,
                             IDxcBlob *shader) {
    if (!shader || shader->GetBufferSize() == 0u) {
        return;
    }

    std::lock_guard<std::mutex> lock(ShaderCacheMutex());
    const std::wstring key = path.wstring();
    if (ShaderMemoryCache().find(key) == ShaderMemoryCache().end()) {
        ShaderMemoryCache()[key] = shader;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return;
    }
    const std::filesystem::path tempPath =
        path.parent_path() / (path.filename().string() + ".tmp");
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out.write(static_cast<const char *>(shader->GetBufferPointer()),
              static_cast<std::streamsize>(shader->GetBufferSize()));
    out.close();
    if (!out.good()) {
        return;
    }
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(tempPath, path, error);
}

/// <summary>
/// HLSLシェーダーをDXCでShader Model 6.6へコンパイルする
/// </summary>
inline Microsoft::WRL::ComPtr<IDxcBlob>
Compile(const std::wstring &path, const std::string &entry,
        const std::string &target) {
    using Microsoft::WRL::ComPtr;

    const std::wstring resolvedPath = ResolveShaderPath(path);
    const std::wstring wideEntry = Widen(entry);
    const std::wstring normalizedTarget = NormalizeShaderTarget(target);
    const bool isLibraryTarget = normalizedTarget.rfind(L"lib_", 0) == 0;
    if (resolvedPath.empty() || normalizedTarget.empty() ||
        (!isLibraryTarget && wideEntry.empty())) {
        OutputDebugStringA("ShaderCompiler: invalid shader compile request\n");
        return {};
    }

    uint64_t dependencyHash = 0u;
    std::filesystem::path cachePath;
    if (ComputeShaderDependencyHash(resolvedPath, entry, normalizedTarget,
                                    dependencyHash)) {
        cachePath = ShaderCachePath(dependencyHash);
        if (Microsoft::WRL::ComPtr<IDxcBlob> cached =
                TryLoadCachedShader(cachePath)) {
            return cached;
        }
    }

    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    ComPtr<IDxcIncludeHandler> includeHandler;
    if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
        !utils) {
        OutputDebugStringA("ShaderCompiler: DxcCreateInstance(DxcUtils) failed\n");
        return {};
    }
    if (FAILED(DxcCreateInstance(CLSID_DxcCompiler,
                                 IID_PPV_ARGS(&compiler))) ||
        !compiler) {
        OutputDebugStringA("ShaderCompiler: DxcCreateInstance(DxcCompiler) failed\n");
        return {};
    }
    if (FAILED(utils->CreateDefaultIncludeHandler(&includeHandler)) ||
        !includeHandler) {
        OutputDebugStringA("ShaderCompiler: CreateDefaultIncludeHandler failed\n");
        return {};
    }

    ComPtr<IDxcBlobEncoding> source;
    if (FAILED(utils->LoadFile(resolvedPath.c_str(), nullptr, &source)) ||
        !source) {
        OutputDebugStringA("ShaderCompiler: DXC LoadFile failed\n");
        return {};
    }

    DxcBuffer sourceBuffer{};
    sourceBuffer.Ptr = source->GetBufferPointer();
    sourceBuffer.Size = source->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_UTF8;

    std::vector<LPCWSTR> arguments = {resolvedPath.c_str()};
    if (!isLibraryTarget) {
        arguments.push_back(L"-E");
        arguments.push_back(wideEntry.c_str());
    }
    arguments.push_back(L"-T");
    arguments.push_back(normalizedTarget.c_str());
    arguments.push_back(L"-HV");
    arguments.push_back(L"2021");
    arguments.push_back(L"-all_resources_bound");
#ifdef _DEBUG
    arguments.push_back(L"-Zi");
    arguments.push_back(L"-Qembed_debug");
    arguments.push_back(L"-Od");
#else
    arguments.push_back(L"-O3");
#endif

    ComPtr<IDxcResult> result;
    HRESULT hr = compiler->Compile(&sourceBuffer, arguments.data(),
                                   static_cast<UINT32>(arguments.size()),
                                   includeHandler.Get(), IID_PPV_ARGS(&result));
    if (FAILED(hr) || !result) {
        OutputDebugStringA("ShaderCompiler: DXC Compile invocation failed\n");
        return {};
    }

    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    const std::string errorMessage = BlobToString(errors.Get());
    if (!errorMessage.empty()) {
        OutputDebugStringA(errorMessage.c_str());
    }

    HRESULT status = S_OK;
    if (FAILED(result->GetStatus(&status))) {
        OutputDebugStringA("ShaderCompiler: DXC GetStatus failed\n");
        return {};
    }
    if (FAILED(status)) {
        if (errorMessage.empty()) {
            OutputDebugStringA("ShaderCompiler: DXC shader compile failed\n");
        }
        return {};
    }

    ComPtr<IDxcBlob> shader;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader),
                                 nullptr)) ||
        !shader) {
        OutputDebugStringA("ShaderCompiler: DXC GetOutput object failed\n");
        return {};
    }
    if (!cachePath.empty()) {
        SaveCachedShader(cachePath, shader.Get());
    }
    return shader;
}

} // namespace ShaderCompiler
