#include "graphics/DxUtils.h"

UINT DxUtils::Align256(UINT size) { return (size + 0xFF) & ~0xFF; }
