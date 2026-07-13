#pragma once

#include "graphics/DirectXCommon.h"

inline bool CanReleaseGpuResources(DirectXCommon* dxCommon, bool hasGpuResources,
                                   bool allowFrameAbort = false) {
    if (!hasGpuResources || dxCommon == nullptr || dxCommon->IsDeviceRemoved()) {
        return true;
    }
    if (dxCommon->IsCommandListRecording()) {
        if (!allowFrameAbort) {
            return false;
        }
        // Destructors cannot preserve ComPtr-owned GPU resources after this
        // function returns, so discard unsubmitted commands before releasing.
        dxCommon->AbortFrame();
    }
    return dxCommon->WaitForGpuIfPossible();
}
