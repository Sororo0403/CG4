#include "EngineTestSupport.h"
#include "EngineTestSuites.h"

#include "core/DebugSettingsStore.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace EngineTests {

void TestDebugSettingsStoreRoundTrip() {
    const std::filesystem::path root =
        MakeTempRoot(L"EngineTestsDebugSettings");
    const std::filesystem::path path = root / L"debug" / L"settings.json";

    DebugSettingsStore settings;
    settings.SetBool("enabled", true);
    settings.SetInt("seed", 42);
    settings.SetFloat("exposure", 1.25f);
    settings.SetString("mode", "canopy");
    settings.SetFloat3("position", DirectX::XMFLOAT3{1.0f, 2.0f, 3.0f});
    settings.SetFloat4("color", DirectX::XMFLOAT4{0.1f, 0.2f, 0.3f, 0.4f});

    Expect(settings.Save(path), "debug settings must save to nested path");

    DebugSettingsStore loaded;
    Expect(loaded.Load(path), "debug settings must load saved JSON");
    Expect(loaded.GetBool("enabled").value_or(false),
           "debug bool setting must round-trip");
    Expect(loaded.GetInt("seed").value_or(0) == 42,
           "debug int setting must round-trip");
    Expect(Near(loaded.GetFloat("exposure").value_or(0.0f), 1.25f),
           "debug float setting must round-trip");
    Expect(loaded.GetString("mode").value_or({}) == "canopy",
           "debug string setting must round-trip");

    const auto position = loaded.GetFloat3("position");
    Expect(position && Near(position->x, 1.0f) && Near(position->y, 2.0f) &&
               Near(position->z, 3.0f),
           "debug float3 setting must round-trip");
    const auto color = loaded.GetFloat4("color");
    Expect(color && Near(color->x, 0.1f) && Near(color->w, 0.4f),
           "debug float4 setting must round-trip");

    loaded.Json()["badFloat3"] = nlohmann::json::array({"x", 2.0f, 3.0f});
    Expect(!loaded.GetFloat3("badFloat3").has_value(),
           "debug float3 getter must reject non-numeric arrays");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

} // namespace EngineTests
