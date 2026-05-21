#pragma once
#include <filesystem>

class AssetManager {
  public:
    static void SetAssetRoot(std::filesystem::path assetRoot);
    static const std::filesystem::path &GetAssetRoot();
    static std::filesystem::path
    ResolvePath(const std::filesystem::path &relativePath);

  private:
    static std::filesystem::path Canonicalize(const std::filesystem::path &path);
};
