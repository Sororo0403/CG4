#include "scene/WorldStateStore.h"

#include <algorithm>
#include <fstream>

namespace {

nlohmann::json Float3ToJson(const DirectX::XMFLOAT3 &value) {
    return nlohmann::json::array({value.x, value.y, value.z});
}

bool JsonToFloat3(const nlohmann::json &json, DirectX::XMFLOAT3 &value) {
    if (!json.is_array() || json.size() != 3u ||
        !std::all_of(json.begin(), json.end(), [](const auto &element) {
            return element.is_number();
        })) {
        return false;
    }
    value = DirectX::XMFLOAT3{json[0].get<float>(), json[1].get<float>(),
                              json[2].get<float>()};
    return true;
}

} // namespace

bool WorldStateStore::Save(const std::filesystem::path &path,
                           const WorldState &state) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    nlohmann::json json;
    json["sceneName"] = state.sceneName;
    json["seed"] = state.seed;
    json["playerPosition"] = Float3ToJson(state.playerPosition);
    json["cameraPosition"] = Float3ToJson(state.cameraPosition);
    json["cameraRotation"] = Float3ToJson(state.cameraRotation);
    json["userData"] = state.userData;

    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << json.dump(2) << '\n';
    return true;
}

bool WorldStateStore::Load(const std::filesystem::path &path,
                           WorldState &state) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    nlohmann::json json;
    try {
        in >> json;
        state.sceneName = json.value("sceneName", std::string{});
        state.seed = json.value("seed", 0u);
        if (json.contains("playerPosition")) {
            JsonToFloat3(json["playerPosition"], state.playerPosition);
        }
        if (json.contains("cameraPosition")) {
            JsonToFloat3(json["cameraPosition"], state.cameraPosition);
        }
        if (json.contains("cameraRotation")) {
            JsonToFloat3(json["cameraRotation"], state.cameraRotation);
        }
        state.userData = json.value("userData", nlohmann::json::object());
    } catch (const nlohmann::json::exception &) {
        return false;
    }
    return true;
}
