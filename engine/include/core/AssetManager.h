#pragma once
#include <filesystem>

class AssetManager {
public:
    /// <summary>
    /// AssetRootを設定する
    /// </summary>
    static void SetAssetRoot(std::filesystem::path assetRoot);
    static std::filesystem::path GetAssetRoot();
    // ビルド出力ディレクトリから起動した場合でも、親ディレクトリを
    // たどってエンジン・アプリの固定リソースを探す。
    static std::filesystem::path ResolvePath(const std::filesystem::path& relativePath);
    // ユーザー指定のアセットパス用。解決後のパスをAssetRoot内に制限する。
    static std::filesystem::path ResolvePathStrict(const std::filesystem::path& relativePath);

private:
    /// <summary>
    /// onicalizeかを取得する
    /// </summary>
    static std::filesystem::path Canonicalize(const std::filesystem::path& path);
};
