#ifndef H_CALLIPHLOX_DEMO_CAMERA_V0
#define H_CALLIPHLOX_DEMO_CAMERA_V0

#include <stdint.h>

#include "device/device.h"
#include "device/components.h"
#include "device/metadata.h"

#ifdef __cplusplus
extern "C"
{
#endif
    struct DeviceManager;

    struct CameraProperties
    {
        float exposure_time_us;
        float line_interval_us;
        enum Direction readout_direction;
        uint8_t binning;
        enum SampleType pixel_type;
        struct camera_properties_offset_s
        {
            uint32_t x, y;
        } offset;
        struct camera_properties_shape_s
        {
            uint32_t x, y;
        } shape;
        struct camera_properties_triggers_s
        {
            uint8_t line_count;
            struct Trigger lines[32];
        } triggers;
    };

    struct CameraPropertyMetadata
    {
        struct Property exposure_time_us;
        struct Property line_interval_us;
        struct Property readout_direction;
        struct Property binning;
        struct camera_properties_metadata_offset_s
        {
            struct Property x, y;
        } offset;
        struct camera_properties_metadata_shape_s
        {
            struct Property x, y;
        } shape;

        /// bit field: bit i is 1 if SampleType(i) is supported, 0 otherwise
        uint64_t supported_pixel_types;

        struct CameraPropertiesTriggerMetadata
        {
            /// the number of supported digital IO lines
            uint8_t line_count;

            /// name[i] is a short, null terminated string naming line i.
            char names[32][32];
            /// unique identifier for each line
            size_t ids[32];
            /// set bit i if line i can be an input line
            uint32_t input;
            /// set bit i if line i can be an output line
            uint32_t output;
            /// set bit i if an input event can be of kind i (see TriggerEvent)
            uint32_t input_events;
            /// set bit i if an output event can be of kind i (see TriggerEvent)
            uint32_t output_events;
        } triggers;
    };

    struct Camera
    {
        struct Device device;
        enum DeviceState state;

        enum DeviceStatusCode (*set)(struct Camera*,
                                     struct CameraProperties* settings);
        enum DeviceStatusCode (*get)(const struct Camera*,
                                     struct CameraProperties* settings);
        enum DeviceStatusCode (*get_meta)(const struct Camera*,
                                          struct CameraPropertyMetadata* meta);
        enum DeviceStatusCode (*get_shape)(const struct Camera*,
                                           struct ImageShape* shape);
        enum DeviceStatusCode (*start)(struct Camera*);
        enum DeviceStatusCode (*stop)(struct Camera*);

        // Fire the software trigger if it's enabled.
        enum DeviceStatusCode (*execute_trigger)(struct Camera*);

        enum DeviceStatusCode (*get_frame)(struct Camera*,
                                           void* im,
                                           size_t* nbytes,
                                           struct ImageInfo* info);
    };

    struct Camera* camera_open(const struct DeviceManager* system,
                               const struct DeviceIdentifier* identifier);

    void camera_close(struct Camera* camera);

    enum DeviceStatusCode camera_set(struct Camera* camera,
                                     struct CameraProperties* settings);

    enum DeviceStatusCode camera_get(const struct Camera* camera,
                                     struct CameraProperties* settings);

    enum DeviceStatusCode camera_get_meta(const struct Camera* camera,
                                          struct CameraPropertyMetadata* meta);

    enum DeviceStatusCode camera_get_image_shape(const struct Camera* camera,
                                                 struct ImageShape* shape);

    enum DeviceStatusCode camera_start(struct Camera* camera);

    enum DeviceStatusCode camera_stop(struct Camera* camera);

    enum DeviceStatusCode camera_execute_trigger(struct Camera* camera);

    enum DeviceStatusCode camera_get_frame(struct Camera* camera,
                                           void* im,
                                           size_t* nbytes,
                                           struct ImageInfo* info);

#ifdef __cplusplus
}
#endif

#endif // H_CALLIPHLOX_DEMO_CAMERA_V0

// FIXME: Camera abstract interface should return DeviceState.
// TODO: Check higher level interface does state management properly.