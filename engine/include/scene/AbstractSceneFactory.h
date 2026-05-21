#pragma once
#include <memory>
#include <string>

class BaseScene;

/// <summary>
/// シーン名から具体的なシーンを生成するFactory Methodの抽象基底
/// </summary>
class AbstractSceneFactory {
  public:
    virtual ~AbstractSceneFactory() = default;

    /// <summary>
    /// 指定されたシーン名に対応するシーンを生成する
    /// </summary>
    virtual std::unique_ptr<BaseScene>
    CreateScene(const std::string &sceneName) = 0;
};
