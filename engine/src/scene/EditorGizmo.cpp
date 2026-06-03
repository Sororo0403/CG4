#include "scene/EditorGizmo.h"

#include <DirectXMath.h>

#ifdef _DEBUG
#include "imgui.h"
#endif

bool EditorGizmo::DrawTransformControls(const char *label,
                                        Transform &transform) {
#ifdef _DEBUG
    bool changed = false;
    if (ImGui::TreeNode(label ? label : "Transform")) {
        int mode = static_cast<int>(mode_);
        const char *items[] = {"Translate", "Rotate", "Scale"};
        if (ImGui::Combo("Mode", &mode, items, 3)) {
            mode_ = static_cast<Mode>(mode);
        }
        changed |= ImGui::DragFloat3("Position", &transform.position.x, 0.05f);
        if (ImGui::DragFloat4("Rotation", &transform.rotation.x, 0.01f)) {
            DirectX::XMVECTOR rotation = DirectX::XMLoadFloat4(&transform.rotation);
            rotation = DirectX::XMQuaternionNormalize(rotation);
            DirectX::XMStoreFloat4(&transform.rotation, rotation);
            changed = true;
        }
        changed |= ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.001f);
        ImGui::TreePop();
    }
    return changed;
#else
    (void)label;
    (void)transform;
    return false;
#endif
}
