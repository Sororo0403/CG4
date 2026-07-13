#include "graphics/ShaderCompiler.h"

#include "core/AssetManager.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ShaderCompiler {

std::wstring ResolveShaderPath(const std::wstring& path) {
    try {
        return AssetManager::ResolvePath(std::filesystem::path(path)).wstring();
    } catch (const std::exception&) {
        return {};
    }
}

std::wstring Widen(const std::string& value) {
    try {
        return std::wstring(value.begin(), value.end());
    } catch (const std::exception&) {
        return {};
    }
}

std::wstring NormalizeShaderTarget(const std::string& target) {
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

std::string BlobToString(IDxcBlobUtf8* blob) {
    if (!blob || blob->GetStringLength() == 0) {
        return {};
    }
    try {
        return std::string(blob->GetStringPointer(), blob->GetStringLength());
    } catch (const std::exception&) {
        return {};
    }
}

std::string NarrowAscii(const std::wstring& value) {
    std::string result;
    try {
        result.resize(value.size());
        std::transform(value.begin(), value.end(), result.begin(),
                       [](wchar_t ch) { return static_cast<char>(ch); });
    } catch (const std::exception&) {
        return {};
    }
    return result;
}

constexpr uint32_t kShaderCacheVersion = 1u;
constexpr bool kShaderDiskCacheEnabled = false;
constexpr uint64_t kShaderHashOffset = 1469598103934665603ull;
constexpr uint64_t kShaderHashPrime = 1099511628211ull;

const char* ShaderBuildConfigName() {
#ifdef _DEBUG
    return "debug";
#else
    return "release";
#endif
}

void HashAppendBytes(uint64_t& hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0u; index < size; ++index) {
        hash ^= static_cast<uint64_t>(bytes[index]);
        hash *= kShaderHashPrime;
    }
}

void HashAppendString(uint64_t& hash, const std::string& value) {
    HashAppendBytes(hash, value.data(), value.size());
    const uint8_t terminator = 0u;
    HashAppendBytes(hash, &terminator, sizeof(terminator));
}

void HashAppendWideString(uint64_t& hash, const std::wstring& value) {
    HashAppendBytes(hash, value.data(), value.size() * sizeof(wchar_t));
    const wchar_t terminator = L'\0';
    HashAppendBytes(hash, &terminator, sizeof(terminator));
}

bool ReadBinaryFile(const std::filesystem::path& path, std::vector<uint8_t>& bytes) {
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
    try {
        bytes.resize(static_cast<size_t>(size));
    } catch (const std::exception&) {
        return false;
    }
    if (bytes.empty()) {
        return true;
    }
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    return in.good();
}

std::filesystem::path CanonicalPathBestEffort(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return canonical;
    }
    error.clear();
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? path.lexically_normal() : absolute.lexically_normal();
}

bool HashShaderDependencyFile(const std::filesystem::path& path, uint64_t& hash,
                              std::unordered_set<std::wstring>& visited, uint32_t depth) {
    if (depth > 16u) {
        return false;
    }

    std::filesystem::path canonical;
    std::wstring key;
    try {
        canonical = CanonicalPathBestEffort(path);
        key = canonical.wstring();
        if (!visited.insert(key).second) {
            return true;
        }
    } catch (const std::exception&) {
        return false;
    }

    std::vector<uint8_t> bytes;
    if (!ReadBinaryFile(canonical, bytes)) {
        return false;
    }
    HashAppendWideString(hash, key);
    HashAppendBytes(hash, bytes.data(), bytes.size());

    std::string text;
    try {
        text.assign(bytes.begin(), bytes.end());
    } catch (const std::exception&) {
        return false;
    }
    size_t lineStart = 0u;
    while (lineStart < text.size()) {
        const size_t lineEnd = text.find_first_of("\r\n", lineStart);
        std::string line;
        try {
            line = text.substr(lineStart, lineEnd == std::string::npos ? std::string::npos
                                                                       : lineEnd - lineStart);
        } catch (const std::exception&) {
            return false;
        }
        const size_t includePos = line.find("#include");
        if (includePos != std::string::npos) {
            const size_t quoteStart = line.find('"', includePos);
            const size_t quoteEnd = quoteStart == std::string::npos
                                        ? std::string::npos
                                        : line.find('"', quoteStart + 1u);
            if (quoteStart != std::string::npos && quoteEnd != std::string::npos &&
                quoteEnd > quoteStart + 1u) {
                std::filesystem::path includePath;
                try {
                    const std::string includeName =
                        line.substr(quoteStart + 1u, quoteEnd - quoteStart - 1u);
                    includePath = canonical.parent_path() / Widen(includeName);
                } catch (const std::exception&) {
                    return false;
                }
                if (!HashShaderDependencyFile(includePath, hash, visited, depth + 1u)) {
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

bool ComputeShaderDependencyHash(const std::filesystem::path& path, const std::string& entry,
                                 const std::wstring& target, uint64_t& hash) {
    hash = kShaderHashOffset;
    HashAppendBytes(hash, &kShaderCacheVersion, sizeof(kShaderCacheVersion));
    HashAppendString(hash, ShaderBuildConfigName());
    HashAppendString(hash, entry);
    HashAppendWideString(hash, target);
    std::unordered_set<std::wstring> visited;
    return HashShaderDependencyFile(path, hash, visited, 0u);
}

std::filesystem::path ShaderCachePath(uint64_t hash) {
    std::ostringstream name;
    name << "shader_v" << kShaderCacheVersion << '_' << ShaderBuildConfigName() << '_' << std::hex
         << std::setw(16) << std::setfill('0') << hash << ".cso";
    return AssetManager::GetAssetRoot() / "generated" / "shader_cache" / name.str();
}

std::mutex& ShaderCacheMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IDxcBlob>>& ShaderMemoryCache() {
    static std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IDxcBlob>> cache;
    return cache;
}

Microsoft::WRL::ComPtr<IDxcUtils> SharedDxcUtils() {
    using Microsoft::WRL::ComPtr;
    static ComPtr<IDxcUtils> utils = [] {
        ComPtr<IDxcUtils> instance;
        if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&instance)))) {
            instance.Reset();
        }
        return instance;
    }();
    return utils;
}

Microsoft::WRL::ComPtr<IDxcCompiler3> SharedDxcCompiler() {
    using Microsoft::WRL::ComPtr;
    static ComPtr<IDxcCompiler3> compiler = [] {
        ComPtr<IDxcCompiler3> instance;
        if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&instance)))) {
            instance.Reset();
        }
        return instance;
    }();
    return compiler;
}

Microsoft::WRL::ComPtr<IDxcIncludeHandler> SharedDxcIncludeHandler() {
    using Microsoft::WRL::ComPtr;
    static ComPtr<IDxcIncludeHandler> includeHandler = [] {
        ComPtr<IDxcIncludeHandler> instance;
        ComPtr<IDxcUtils> utils = SharedDxcUtils();
        if (!utils || FAILED(utils->CreateDefaultIncludeHandler(&instance))) {
            instance.Reset();
        }
        return instance;
    }();
    return includeHandler;
}

Microsoft::WRL::ComPtr<IDxcBlob> CreateBlobFromBytes(const std::vector<uint8_t>& bytes) {
    using Microsoft::WRL::ComPtr;
    if (bytes.empty() || bytes.size() > UINT32_MAX) {
        return {};
    }

    ComPtr<IDxcUtils> utils = SharedDxcUtils();
    if (!utils) {
        return {};
    }
    ComPtr<IDxcBlobEncoding> encoded;
    if (FAILED(utils->CreateBlob(bytes.data(), static_cast<UINT32>(bytes.size()), DXC_CP_ACP,
                                 &encoded)) ||
        !encoded) {
        return {};
    }
    ComPtr<IDxcBlob> blob;
    if (FAILED(encoded.As(&blob)) || !blob) {
        return {};
    }
    return blob;
}

Microsoft::WRL::ComPtr<IDxcBlob> TryLoadCachedShader(const std::filesystem::path& path) {
    using Microsoft::WRL::ComPtr;
    std::wstring key;
    try {
        key = path.wstring();
    } catch (const std::exception&) {
        return {};
    }
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
    try {
        ShaderMemoryCache()[key] = blob;
    } catch (const std::exception&) {
    }
    return blob;
}

void SaveCachedShader(const std::filesystem::path& path, IDxcBlob* shader) {
    if (!shader || shader->GetBufferSize() == 0u) {
        return;
    }

    std::lock_guard<std::mutex> lock(ShaderCacheMutex());
    std::wstring key;
    try {
        key = path.wstring();
    } catch (const std::exception&) {
        return;
    }
    try {
        if (!ShaderMemoryCache().contains(key)) {
            ShaderMemoryCache()[key] = shader;
        }
    } catch (const std::exception&) {
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return;
    }
    std::filesystem::path tempPath;
    try {
        tempPath = path.parent_path() / (path.filename().string() + ".tmp");
    } catch (const std::exception&) {
        return;
    }
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out.write(static_cast<const char*>(shader->GetBufferPointer()),
              static_cast<std::streamsize>(shader->GetBufferSize()));
    out.close();
    if (!out.good()) {
        return;
    }
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(tempPath, path, error);
}

struct ShaderCompileRequest {
    std::wstring resolvedPath;
    std::wstring wideEntry;
    std::wstring normalizedTarget;
    bool isLibraryTarget = false;
};

bool MakeShaderCompileRequest(const std::wstring& path, const std::string& entry,
                              const std::string& target, ShaderCompileRequest& request) {
    request.resolvedPath = ResolveShaderPath(path);
    request.wideEntry = Widen(entry);
    request.normalizedTarget = NormalizeShaderTarget(target);
    request.isLibraryTarget = request.normalizedTarget.rfind(L"lib_", 0) == 0;
    return !request.resolvedPath.empty() && !request.normalizedTarget.empty() &&
           (request.isLibraryTarget || !request.wideEntry.empty());
}

Microsoft::WRL::ComPtr<IDxcBlob> TryLoadCachedShader(const ShaderCompileRequest& request,
                                                     const std::string& entry) {
    if constexpr (!kShaderDiskCacheEnabled) {
        (void)request;
        (void)entry;
        return {};
    } else {
        uint64_t dependencyHash = 0u;
        if (!ComputeShaderDependencyHash(request.resolvedPath, entry, request.normalizedTarget,
                                         dependencyHash)) {
            return {};
        }
        return TryLoadCachedShader(ShaderCachePath(dependencyHash));
    }
}

std::vector<LPCWSTR> BuildDxcArguments(const ShaderCompileRequest& request) {
    std::vector<LPCWSTR> arguments = {request.resolvedPath.c_str()};
    if (!request.isLibraryTarget) {
        arguments.push_back(L"-E");
        arguments.push_back(request.wideEntry.c_str());
    }
    arguments.push_back(L"-T");
    arguments.push_back(request.normalizedTarget.c_str());
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
    return arguments;
}

bool TryLoadShaderSource(IDxcUtils* utils, const std::wstring& path,
                         Microsoft::WRL::ComPtr<IDxcBlobEncoding>& source,
                         DxcBuffer& sourceBuffer) {
    using Microsoft::WRL::ComPtr;
    if (FAILED(utils->LoadFile(path.c_str(), nullptr, &source)) || !source) {
        OutputDebugStringA("ShaderCompiler: DXC LoadFile failed\n");
        return false;
    }

    sourceBuffer.Ptr = source->GetBufferPointer();
    sourceBuffer.Size = source->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_UTF8;
    return true;
}

Microsoft::WRL::ComPtr<IDxcResult> CompileShaderSource(IDxcCompiler3* compiler,
                                                       IDxcIncludeHandler* includeHandler,
                                                       const DxcBuffer& sourceBuffer,
                                                       std::vector<LPCWSTR>& arguments) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDxcResult> result;
    HRESULT hr =
        compiler->Compile(&sourceBuffer, arguments.data(), static_cast<UINT32>(arguments.size()),
                          includeHandler, IID_PPV_ARGS(&result));
    if (FAILED(hr) || !result) {
        OutputDebugStringA("ShaderCompiler: DXC Compile invocation failed\n");
        return {};
    }
    return result;
}

bool IsSuccessfulCompileResult(IDxcResult* result) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    const std::string errorMessage = BlobToString(errors.Get());
    if (!errorMessage.empty()) {
        OutputDebugStringA(errorMessage.c_str());
    }

    HRESULT status = S_OK;
    if (FAILED(result->GetStatus(&status))) {
        OutputDebugStringA("ShaderCompiler: DXC GetStatus failed\n");
        return false;
    }
    if (FAILED(status)) {
        if (errorMessage.empty()) {
            OutputDebugStringA("ShaderCompiler: DXC shader compile failed\n");
        }
        return false;
    }
    return true;
}

Microsoft::WRL::ComPtr<IDxcBlob> GetCompiledShaderObject(IDxcResult* result) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDxcBlob> shader;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr)) || !shader) {
        OutputDebugStringA("ShaderCompiler: DXC GetOutput object failed\n");
        return {};
    }
    return shader;
}

void SaveCachedShader(const ShaderCompileRequest& request, const std::string& entry,
                      IDxcBlob* shader) {
    if constexpr (kShaderDiskCacheEnabled) {
        uint64_t dependencyHash = 0u;
        if (ComputeShaderDependencyHash(request.resolvedPath, entry, request.normalizedTarget,
                                        dependencyHash)) {
            SaveCachedShader(ShaderCachePath(dependencyHash), shader);
        }
    } else {
        (void)request;
        (void)entry;
        (void)shader;
    }
}

Microsoft::WRL::ComPtr<IDxcBlob> Compile(const std::wstring& path, const std::string& entry,
                                         const std::string& target) {
    using Microsoft::WRL::ComPtr;

    ShaderCompileRequest request;
    if (!MakeShaderCompileRequest(path, entry, target, request)) {
        OutputDebugStringA("ShaderCompiler: invalid shader compile request\n");
        return {};
    }

    if (ComPtr<IDxcBlob> cached = TryLoadCachedShader(request, entry)) {
        return cached;
    }

    ComPtr<IDxcUtils> utils = SharedDxcUtils();
    ComPtr<IDxcCompiler3> compiler = SharedDxcCompiler();
    ComPtr<IDxcIncludeHandler> includeHandler = SharedDxcIncludeHandler();
    if (!utils) {
        OutputDebugStringA("ShaderCompiler: DxcCreateInstance(DxcUtils) failed\n");
        return {};
    }
    if (!compiler) {
        OutputDebugStringA("ShaderCompiler: DxcCreateInstance(DxcCompiler) failed\n");
        return {};
    }
    if (!includeHandler) {
        OutputDebugStringA("ShaderCompiler: CreateDefaultIncludeHandler failed\n");
        return {};
    }

    DxcBuffer sourceBuffer{};
    ComPtr<IDxcBlobEncoding> source;
    if (!TryLoadShaderSource(utils.Get(), request.resolvedPath, source, sourceBuffer)) {
        return {};
    }

    std::vector<LPCWSTR> arguments;
    try {
        arguments = BuildDxcArguments(request);
    } catch (const std::exception&) {
        OutputDebugStringA("ShaderCompiler: argument allocation failed\n");
        return {};
    }

    ComPtr<IDxcResult> result =
        CompileShaderSource(compiler.Get(), includeHandler.Get(), sourceBuffer, arguments);
    if (!result || !IsSuccessfulCompileResult(result.Get())) {
        return {};
    }

    ComPtr<IDxcBlob> shader = GetCompiledShaderObject(result.Get());
    if (shader) {
        SaveCachedShader(request, entry, shader.Get());
    }
    return shader;
}

} // namespace ShaderCompiler
