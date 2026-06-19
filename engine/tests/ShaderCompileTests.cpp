#include "EngineTestSupport.h"
#include "EngineTestSuites.h"

#include "core/AssetManager.h"
#include "graphics/ShaderCompiler.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace EngineTests {
namespace {

struct ShaderCompileTarget {
    std::filesystem::path path;
    std::string entry = "main";
    const char *target = nullptr;
};

bool EndsWith(std::wstring_view value, std::wstring_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

const char *InferShaderTarget(const std::filesystem::path &path) {
    const std::wstring filename = path.filename().wstring();
    if (EndsWith(filename, L"VS.hlsl")) {
        return "vs_6_6";
    }
    if (EndsWith(filename, L"PS.hlsl")) {
        return "ps_6_6";
    }
    if (EndsWith(filename, L"CS.hlsl")) {
        return "cs_6_6";
    }
    if (EndsWith(filename, L"AS.hlsl")) {
        return "as_6_6";
    }
    if (EndsWith(filename, L"MS.hlsl")) {
        return "ms_6_6";
    }
    return nullptr;
}

void AppendShaderCompileTargets(const wchar_t *shaderRoot,
                                std::vector<ShaderCompileTarget> &targets,
                                std::vector<std::filesystem::path> &includeFiles) {
    const std::filesystem::path root = AssetManager::ResolvePath(shaderRoot);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) ||
        !std::filesystem::is_directory(root, ec)) {
        std::cerr << "FAILED: shader root is missing: " << root.string()
                  << '\n';
        ++gFailures;
        return;
    }

    std::filesystem::recursive_directory_iterator it(root, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        if (it->path().extension() == L".hlsli") {
            includeFiles.push_back(std::filesystem::weakly_canonical(it->path(), ec));
            ec.clear();
            continue;
        }
        if (it->path().extension() != L".hlsl") {
            continue;
        }

        const char *target = InferShaderTarget(it->path());
        if (target != nullptr) {
            targets.push_back({it->path(), "main", target});
        }
    }

    if (ec) {
        std::cerr << "FAILED: shader directory traversal failed: "
                  << root.string() << ": " << ec.message() << '\n';
        ++gFailures;
    }
}

std::string Trim(std::string_view value) {
    size_t begin = 0u;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1u])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool TryParseInclude(std::string_view line, std::filesystem::path &includePath) {
    std::string trimmed = Trim(line);
    if (trimmed.rfind("#include", 0) != 0) {
        return false;
    }

    const size_t quoteBegin = trimmed.find('"');
    const size_t angleBegin = trimmed.find('<');
    const char terminator = quoteBegin != std::string::npos ? '"' : '>';
    const size_t begin =
        quoteBegin != std::string::npos ? quoteBegin : angleBegin;
    if (begin == std::string::npos) {
        return false;
    }
    const size_t end = trimmed.find(terminator, begin + 1u);
    if (end == std::string::npos || end <= begin + 1u) {
        return false;
    }

    includePath = std::filesystem::path(
        trimmed.substr(begin + 1u, end - begin - 1u));
    return true;
}

void CollectTransitiveIncludes(
    const std::filesystem::path &source,
    std::set<std::filesystem::path> &referencedIncludes,
    std::set<std::filesystem::path> &visitedSources) {
    std::error_code ec;
    const std::filesystem::path canonicalSource =
        std::filesystem::weakly_canonical(source, ec);
    ec.clear();
    if (canonicalSource.empty() || !visitedSources.insert(canonicalSource).second) {
        return;
    }

    std::ifstream stream(canonicalSource);
    if (!stream) {
        std::cerr << "FAILED: shader source is unreadable: "
                  << canonicalSource.string() << '\n';
        ++gFailures;
        return;
    }

    std::string line;
    while (std::getline(stream, line)) {
        std::filesystem::path includePath;
        if (!TryParseInclude(line, includePath)) {
            continue;
        }

        const std::filesystem::path resolved =
            std::filesystem::weakly_canonical(
                canonicalSource.parent_path() / includePath, ec);
        if (ec) {
            std::cerr << "FAILED: shader include is missing: "
                      << (canonicalSource.parent_path() / includePath).string()
                      << '\n';
            ++gFailures;
            ec.clear();
            continue;
        }

        if (resolved.extension() == L".hlsli") {
            referencedIncludes.insert(resolved);
        }
        CollectTransitiveIncludes(resolved, referencedIncludes, visitedSources);
    }
}

void AppendKnownShaderEntryPoints(std::vector<ShaderCompileTarget> &targets) {
    targets.push_back({AssetManager::ResolvePath(
                           L"engine/resources/shaders/sprite/SpritePS.hlsl"),
                       "mainModulate", "ps_6_6"});
    targets.push_back({AssetManager::ResolvePath(
                           L"engine/resources/shaders/sprite/SpritePS.hlsl"),
                       "mainPremultipliedMask", "ps_6_6"});
}

} // namespace

void TestAllRuntimeShadersCompile() {
    std::vector<ShaderCompileTarget> targets;
    std::vector<std::filesystem::path> includeFiles;
    constexpr std::array<const wchar_t *, 1> kShaderRoots = {
        L"engine/resources/shaders"};
    for (const wchar_t *root : kShaderRoots) {
        AppendShaderCompileTargets(root, targets, includeFiles);
    }
    AppendKnownShaderEntryPoints(targets);

    Expect(!targets.empty(), "runtime shader compile test must find shaders");
    for (const ShaderCompileTarget &shader : targets) {
        const auto blob =
            ShaderCompiler::Compile(shader.path.wstring(), shader.entry, shader.target);
        if (!blob) {
            std::cerr << "FAILED: runtime shader must compile: "
                      << shader.path.string() << " [" << shader.entry << " "
                      << shader.target << "]\n";
            ++gFailures;
        }
    }

    std::set<std::filesystem::path> referencedIncludes;
    std::set<std::filesystem::path> visitedSources;
    for (const ShaderCompileTarget &shader : targets) {
        CollectTransitiveIncludes(shader.path, referencedIncludes, visitedSources);
    }
}

} // namespace EngineTests
