#include "EngineTestSupport.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace EngineTests {

int gFailures = 0;

void Expect(bool condition, const char *message) {
    if (condition) {
        return;
    }
    ++gFailures;
    std::cerr << "FAILED: " << message << '\n';
}

bool Near(float left, float right, float epsilon) {
    return std::fabs(left - right) <= epsilon;
}

std::filesystem::path CanonicalForTest(const std::filesystem::path &path) {
    std::error_code ec;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : canonical;
}

std::filesystem::path MakeTempRoot(const wchar_t *name) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        (std::wstring(name) + L"_" +
         std::to_wstring(reinterpret_cast<std::uintptr_t>(&gFailures)));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    return root;
}

} // namespace EngineTests
