#pragma once
#include <openxr/openxr.h>

// The pinned OpenXR SDK predates this ratified extension. These ABI declarations
// follow Khronos OpenXR-SDK/include/openxr/openxr.h (Apache-2.0 OR MIT).
// No runtime is changed or extension assumed: callers must enable it first.
#ifndef XR_EXT_hand_tracking_data_source
#define XR_EXT_hand_tracking_data_source 1
#define XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME "XR_EXT_hand_tracking_data_source"
constexpr XrStructureType XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT = static_cast<XrStructureType>(1000428000);
constexpr XrStructureType XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT = static_cast<XrStructureType>(1000428001);
enum XrHandTrackingDataSourceEXT {
    XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT = 1,
    XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT = 2,
    XR_HAND_TRACKING_DATA_SOURCE_MAX_ENUM_EXT = 0x7fffffff
};
struct XrHandTrackingDataSourceInfoEXT {
    XrStructureType type;
    const void* next;
    uint32_t requestedDataSourceCount;
    XrHandTrackingDataSourceEXT* requestedDataSources;
};
struct XrHandTrackingDataSourceStateEXT {
    XrStructureType type;
    void* next;
    XrBool32 isActive;
    XrHandTrackingDataSourceEXT dataSource;
};
#endif
