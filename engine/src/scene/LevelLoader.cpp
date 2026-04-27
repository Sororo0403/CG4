#include "LevelLoader.h"
#include <DirectXMath.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;
using namespace DirectX;

namespace {

XMFLOAT3 ParseVector3(const json &node, const char *key) {
    if (!node.contains(key) || !node.at(key).is_array() ||
        node.at(key).size() != 3) {
        throw std::runtime_error(std::string("Missing or invalid vector: ") +
                                 key);
    }

    const json &array = node.at(key);

    return {
        array.at(0).get<float>(),
        array.at(2).get<float>(),
        array.at(1).get<float>(),
    };
}

XMFLOAT4 QuaternionFromEulerDegrees(const XMFLOAT3 &degrees) {
    const XMVECTOR quaternion = XMQuaternionRotationRollPitchYaw(
        XMConvertToRadians(degrees.x), XMConvertToRadians(degrees.y),
        XMConvertToRadians(degrees.z));

    XMFLOAT4 result{};
    XMStoreFloat4(&result, quaternion);
    return result;
}

Transform ParseTransform(const json &node) {
    if (!node.is_object()) {
        throw std::runtime_error("transform must be an object");
    }

    Transform transform{};
    transform.position = ParseVector3(node, "translation");
    transform.scale = ParseVector3(node, "scaling");

    const XMFLOAT3 rotationDegrees = ParseVector3(node, "rotation");
    transform.rotation = QuaternionFromEulerDegrees(rotationDegrees);

    return transform;
}

LevelObjectData ParseObject(const json &node) {
    if (!node.is_object()) {
        throw std::runtime_error("object must be an object");
    }

    if (node.contains("disabled")) {
        if (node["disabled"].get<bool>()) {
            return LevelObjectData{};
        }
    }

    if (!node.contains("type") || !node.at("type").is_string()) {
        throw std::runtime_error("object.type must be a string");
    }

    if (!node.contains("transform")) {
        throw std::runtime_error("object.transform is required");
    }

    LevelObjectData object{};
    object.type = node.at("type").get<std::string>();
    object.name = node.value("name", object.type);
    object.fileName = node.value("file_name", "");
    object.transform = ParseTransform(node.at("transform"));

    if (node.contains("children")) {
        if (!node.at("children").is_array()) {
            throw std::runtime_error("object.children must be an array");
        }

        for (const json &child : node.at("children")) {
            LevelObjectData childObj = ParseObject(child);
            if (!childObj.type.empty()) {
                object.children.push_back(childObj);
            }
        }
    }

    return object;
}

} // namespace

LevelData LevelLoader::Load(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open level file: " + path.string());
    }

    json root;
    file >> root;

    if (!root.is_object()) {
        throw std::runtime_error("Level root must be an object");
    }

    if (!root.contains("name") || !root.at("name").is_string()) {
        throw std::runtime_error("Level name must be a string");
    }

    if (!root.contains("objects") || !root.at("objects").is_array()) {
        throw std::runtime_error("Level objects must be an array");
    }

    LevelData level{};
    level.name = root.at("name").get<std::string>();

    for (const json &object : root.at("objects")) {
        if (object.contains("disabled")) {
            if (object["disabled"].get<bool>()) {
                continue;
            }
        }

        LevelObjectData obj = ParseObject(object);

        if (!obj.type.empty()) {
            level.objects.push_back(obj);
        }
    }

    return level;
}
