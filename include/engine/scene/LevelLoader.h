#pragma once
#include "Transform.h"
#include <filesystem>
#include <string>
#include <vector>

struct LevelObjectData {
    std::string name;
    std::string type;
    std::string fileName;
    Transform transform;
    std::vector<LevelObjectData> children;
};

struct LevelData {
    std::string name;
    std::vector<LevelObjectData> objects;
};

class LevelLoader {
  public:
    static LevelData Load(const std::filesystem::path &path);
};
