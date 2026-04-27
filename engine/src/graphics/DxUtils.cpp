#include "DxUtils.h"

void DxUtils::ThrowIfFailed(HRESULT hr, const char *msg) {
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "%s (HRESULT=0x%08X)\n", msg, hr);
        OutputDebugStringA(buf);
        throw std::runtime_error(buf);
    }
}

UINT DxUtils::Align256(UINT size) { return (size + 0xFF) & ~0xFF; }
