#pragma once
#include <openxr/openxr.h>

// OpenXR action-based input for the Quest Touch controllers.
class XrInput {
public:
    bool Init(XrInstance instance, XrSession session);
    void Sync();
    bool GetAimPose(XrSpace sessionSpace, XrTime time, XrPosef& outPose);
    bool IsTriggerPressed();
    void Shutdown();
    bool IsInit() const { return init_; }

private:
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrAction triggerAction_ = XR_NULL_HANDLE;
    XrAction aimAction_ = XR_NULL_HANDLE;
    XrSpace aimSpace_ = XR_NULL_HANDLE;
    XrPath rightHandPath_ = XR_NULL_PATH;
    bool init_ = false;
};
