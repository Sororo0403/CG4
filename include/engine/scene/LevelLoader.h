#pragma once
#include "Transform.h"
#include <filesystem>
#include <string>
#include <vector>

/// <summary>
/// レベルファイル内のオブジェクト情報
/// </summary>
struct LevelObjectData {
    std::string name;
    std::string type;
    std::string fileName;
    Transform transform;
    std::vector<LevelObjectData> children;
};

/// <summary>
/// レベル全体の読み込み結果
/// </summary>
struct LevelData {
    std::string name;
    std::vector<LevelObjectData> objects;
};

/// <summary>
/// レベルファイルを読み込んで構造化データへ変換する
/// </summary>
class LevelLoader {
  public:
    /// <summary>
    /// レベルデータをファイルから読み込む
    /// </summary>
    /// <param name="path">読み込むレベルファイルのパス</param>
    /// <returns>読み込んだレベルデータ</returns>
    static LevelData Load(const std::filesystem::path &path);
};
