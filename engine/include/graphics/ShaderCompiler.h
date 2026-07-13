#pragma once

#include <Windows.h>
#include <dxcapi.h>
#include <string>
#include <wrl.h>

namespace ShaderCompiler {

std::wstring ResolveShaderPath(const std::wstring& path);

Microsoft::WRL::ComPtr<IDxcBlob> Compile(const std::wstring& path, const std::string& entry,
                                         const std::string& target);

} // namespace ShaderCompiler
