#pragma once
#include <Windows.h>
#include <stdexcept>

/// <summary>
/// DirectX関連の共通ユーティリティ関数群
/// </summary>
namespace DxUtils {

/// <summary>
/// HRESULTを検証し、失敗していれば例外を送出する
/// </summary>
/// <param name="hr">判定対象となるHRESULT</param>
/// <param name="msg">失敗時に例外として投げるエラーメッセージ</param>
void ThrowIfFailed(HRESULT hr, const char *msg);

/// <summary>
/// 256byteアラインを行う
/// </summary>
/// <param name="size">256byteアラインを行うサイズ</param>
/// <returns>256byteアライン後のサイズ</returns>
UINT Align256(UINT size);

} // namespace DxUtils
