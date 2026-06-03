#pragma once

#include "model/Transform.h"

class EditorGizmo {
  public:
    enum class Mode {
        Translate,
        Rotate,
        Scale,
    };

    void SetMode(Mode mode) { mode_ = mode; }
    Mode GetMode() const { return mode_; }

    bool DrawTransformControls(const char *label, Transform &transform);

  private:
    Mode mode_ = Mode::Translate;
};
