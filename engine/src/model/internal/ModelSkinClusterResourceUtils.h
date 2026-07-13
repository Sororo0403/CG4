#pragma once

#include "model/Model.h"

namespace ModelSkinClusterResourceUtils {

inline void UnmapSkinClusterMappings(SkinCluster& skinCluster) {
    if (skinCluster.influenceResource && skinCluster.mappedInfluence != nullptr) {
        skinCluster.influenceResource->Unmap(0, nullptr);
        skinCluster.mappedInfluence = nullptr;
    }
    for (SkinPaletteFrame& frame : skinCluster.paletteFrames) {
        if (frame.resource && frame.mappedPalette != nullptr) {
            frame.resource->Unmap(0, nullptr);
            frame.mappedPalette = nullptr;
        }
    }
}

} // namespace ModelSkinClusterResourceUtils
