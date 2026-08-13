#include "xr_input.h"

#include <cstring>

#include <android/log.h>

#define LOG_TAG "XrInput"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define XR_CHECK(x) do { XrResult r_ = (x); if (r_ != XR_SUCCESS) { LOGE("%s failed: %d", #x, r_); return false; } } while (0)

bool XrInput::Init(XrInstance instance, XrSession session) {
    instance_ = instance;
    session_ = session;

    XrPath touchProfile, aimPosePath, triggerValuePath;
    XR_CHECK(xrStringToPath(instance_, "/user/hand/right", &rightHandPath_));
    XR_CHECK(xrStringToPath(instance_, "/interaction_profiles/oculus/touch_controller", &touchProfile));
    XR_CHECK(xrStringToPath(instance_, "/user/hand/right/input/aim/pose", &aimPosePath));
    XR_CHECK(xrStringToPath(instance_, "/user/hand/right/input/trigger/value", &triggerValuePath));

    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(setInfo.actionSetName, "gameplay");
    std::strcpy(setInfo.localizedActionSetName, "Gameplay");
    setInfo.priority = 0;
    XR_CHECK(xrCreateActionSet(instance_, &setInfo, &actionSet_));

    XrActionCreateInfo trigInfo{XR_TYPE_ACTION_CREATE_INFO};
    std::strcpy(trigInfo.actionName, "trigger");
    std::strcpy(trigInfo.localizedActionName, "Trigger");
    trigInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    trigInfo.countSubactionPaths = 1;
    trigInfo.subactionPaths = &rightHandPath_;
    XR_CHECK(xrCreateAction(actionSet_, &trigInfo, &triggerAction_));

    XrActionCreateInfo aimInfo{XR_TYPE_ACTION_CREATE_INFO};
    std::strcpy(aimInfo.actionName, "aim");
    std::strcpy(aimInfo.localizedActionName, "Aim");
    aimInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
    aimInfo.countSubactionPaths = 1;
    aimInfo.subactionPaths = &rightHandPath_;
    XR_CHECK(xrCreateAction(actionSet_, &aimInfo, &aimAction_));

    XrActionSuggestedBinding bindings[2];
    bindings[0].action = triggerAction_;
    bindings[0].binding = triggerValuePath;
    bindings[1].action = aimAction_;
    bindings[1].binding = aimPosePath;

    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = touchProfile;
    suggested.countSuggestedBindings = 2;
    suggested.suggestedBindings = bindings;
    XR_CHECK(xrSuggestInteractionProfileBindings(instance_, &suggested));

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &actionSet_;
    XR_CHECK(xrAttachSessionActionSets(session_, &attach));

    XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    spaceInfo.action = aimAction_;
    spaceInfo.subactionPath = rightHandPath_;
    spaceInfo.poseInActionSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    spaceInfo.poseInActionSpace.position = {0.0f, 0.0f, 0.0f};
    XR_CHECK(xrCreateActionSpace(session_, &spaceInfo, &aimSpace_));

    init_ = true;
    LOGI("input initialized (Oculus Touch)");
    return true;
}

void XrInput::Sync() {
    if (!init_) return;
    XrActiveActionSet active{actionSet_, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &active;
    xrSyncActions(session_, &syncInfo);
}

bool XrInput::GetAimPose(XrSpace sessionSpace, XrTime time, XrPosef& outPose) {
    if (!init_ || aimSpace_ == XR_NULL_HANDLE) return false;
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (xrLocateSpace(aimSpace_, sessionSpace, time, &loc) != XR_SUCCESS) return false;
    if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
        (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        outPose = loc.pose;
        return true;
    }
    return false;
}

bool XrInput::IsTriggerPressed() {
    if (!init_) return false;
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = triggerAction_;
    getInfo.subactionPath = rightHandPath_;
    if (xrGetActionStateBoolean(session_, &getInfo, &state) != XR_SUCCESS) return false;
    return state.isActive && state.currentState;
}

void XrInput::Shutdown() {
    if (aimSpace_ != XR_NULL_HANDLE) { xrDestroySpace(aimSpace_); aimSpace_ = XR_NULL_HANDLE; }
    if (triggerAction_ != XR_NULL_HANDLE) { xrDestroyAction(triggerAction_); triggerAction_ = XR_NULL_HANDLE; }
    if (aimAction_ != XR_NULL_HANDLE) { xrDestroyAction(aimAction_); aimAction_ = XR_NULL_HANDLE; }
    if (actionSet_ != XR_NULL_HANDLE) { xrDestroyActionSet(actionSet_); actionSet_ = XR_NULL_HANDLE; }
    init_ = false;
}
