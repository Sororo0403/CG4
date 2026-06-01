#pragma once
#include <filesystem>

class AssetManager {
  public:
    /// <summary>
    /// AssetRootを設定する
    /// </summary>
    static void SetAssetRoot(std::filesystem::path assetRoot);
    static const std::filesystem::path &GetAssetRoot();
    static std::filesystem::path
    ResolvePath(const std::filesystem::path &relativePath);

  private:
    /// <summary>
    /// onicalizeかを取得する
    /// </summary>
    static std::filesystem::path Canonicalize(const std::filesystem::path &path);
};
