#pragma once

#include <filesystem>

namespace EngineTests {

extern int gFailures;

void Expect(bool condition, const char *message);
bool Near(float left, float right, float epsilon = 0.0001f);
std::filesystem::path CanonicalForTest(const std::filesystem::path &path);
std::filesystem::path MakeTempRoot(const wchar_t *name);

} // namespace EngineTests
