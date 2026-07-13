#include "core/DebugSettingsStore.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <fstream>

namespace {

bool IsFloatArray(const nlohmann::json& value, size_t expectedSize) {
    if (!value.is_array() || value.size() != expectedSize) {
        return false;
    }
    return std::all_of(value.begin(), value.end(),
                       [](const auto& element) { return element.is_number(); });
}

} // namespace

bool DebugSettingsStore::Load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    nlohmann::json loaded;
    try {
        in >> loaded;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    if (!loaded.is_object()) {
        return false;
    }
    try {
        values_ = std::move(loaded);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool DebugSettingsStore::Save(const std::filesystem::path& path) const {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    std::ofstream out(path);
    if (!out) {
        return false;
    }
    try {
        out << values_.dump(2) << '\n';
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

void DebugSettingsStore::Clear() {
    try {
        values_ = nlohmann::json::object();
    } catch (const std::exception&) {
    }
}

bool DebugSettingsStore::Has(const std::string& key) const {
    try {
        return values_.contains(key);
    } catch (const std::exception&) {
        return false;
    }
}

void DebugSettingsStore::Remove(const std::string& key) {
    try {
        values_.erase(key);
    } catch (const std::exception&) {
    }
}

void DebugSettingsStore::SetBool(const std::string& key, bool value) {
    try {
        values_[key] = value;
    } catch (const std::exception&) {
    }
}

void DebugSettingsStore::SetInt(const std::string& key, int value) {
    try {
        values_[key] = value;
    } catch (const std::exception&) {
    }
}

void DebugSettingsStore::SetFloat(const std::string& key, float value) {
    try {
        values_[key] = value;
    } catch (const std::exception&) {
    }
}

void DebugSettingsStore::SetString(const std::string& key, const std::string& value) {
    try {
        values_[key] = value;
    } catch (const std::exception&) {
    }
}

void DebugSettingsStore::SetFloat3(const std::string& key, const DirectX::XMFLOAT3& value) {
    try {
        values_[key] = {value.x, value.y, value.z};
    } catch (const std::exception&) {
    }
}

void DebugSettingsStore::SetFloat4(const std::string& key, const DirectX::XMFLOAT4& value) {
    try {
        values_[key] = {value.x, value.y, value.z, value.w};
    } catch (const nlohmann::json::exception&) {
    }
}

std::optional<bool> DebugSettingsStore::GetBool(const std::string& key) const {
    const nlohmann::json* value = Find(key);
    if (!value || !value->is_boolean()) {
        return std::nullopt;
    }
    try {
        return value->get<bool>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<int> DebugSettingsStore::GetInt(const std::string& key) const {
    const nlohmann::json* value = Find(key);
    if (!value || !value->is_number_integer()) {
        return std::nullopt;
    }
    try {
        return value->get<int>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<float> DebugSettingsStore::GetFloat(const std::string& key) const {
    const nlohmann::json* value = Find(key);
    if (!value || !value->is_number()) {
        return std::nullopt;
    }
    try {
        return value->get<float>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::string> DebugSettingsStore::GetString(const std::string& key) const {
    const nlohmann::json* value = Find(key);
    if (!value || !value->is_string()) {
        return std::nullopt;
    }
    try {
        return value->get<std::string>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<DirectX::XMFLOAT3> DebugSettingsStore::GetFloat3(const std::string& key) const {
    const nlohmann::json* value = Find(key);
    if (!value || !IsFloatArray(*value, 3u)) {
        return std::nullopt;
    }
    try {
        return DirectX::XMFLOAT3{(*value)[0].get<float>(), (*value)[1].get<float>(),
                                 (*value)[2].get<float>()};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<DirectX::XMFLOAT4> DebugSettingsStore::GetFloat4(const std::string& key) const {
    const nlohmann::json* value = Find(key);
    if (!value || !IsFloatArray(*value, 4u)) {
        return std::nullopt;
    }
    try {
        return DirectX::XMFLOAT4{(*value)[0].get<float>(), (*value)[1].get<float>(),
                                 (*value)[2].get<float>(), (*value)[3].get<float>()};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

const nlohmann::json* DebugSettingsStore::Find(const std::string& key) const {
    try {
        const auto it = values_.find(key);
        return it == values_.end() ? nullptr : &(*it);
    } catch (const std::exception&) {
        return nullptr;
    }
}
