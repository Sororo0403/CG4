#pragma once

#include "nlohmann/json.hpp"

#include <DirectXMath.h>
#include <filesystem>
#include <optional>
#include <string>

class DebugSettingsStore {
public:
    bool Load(const std::filesystem::path& path);
    bool Save(const std::filesystem::path& path) const;
    void Clear();

    bool Has(const std::string& key) const;
    void Remove(const std::string& key);

    void SetBool(const std::string& key, bool value);
    void SetInt(const std::string& key, int value);
    void SetFloat(const std::string& key, float value);
    void SetString(const std::string& key, const std::string& value);
    void SetFloat3(const std::string& key, const DirectX::XMFLOAT3& value);
    void SetFloat4(const std::string& key, const DirectX::XMFLOAT4& value);

    std::optional<bool> GetBool(const std::string& key) const;
    std::optional<int> GetInt(const std::string& key) const;
    std::optional<float> GetFloat(const std::string& key) const;
    std::optional<std::string> GetString(const std::string& key) const;
    std::optional<DirectX::XMFLOAT3> GetFloat3(const std::string& key) const;
    std::optional<DirectX::XMFLOAT4> GetFloat4(const std::string& key) const;

    nlohmann::json& Json() {
        return values_;
    }
    const nlohmann::json& Json() const {
        return values_;
    }

private:
    const nlohmann::json* Find(const std::string& key) const;

    nlohmann::json values_ = nlohmann::json::object();
};
