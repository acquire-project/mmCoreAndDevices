#ifndef H_CALLIPHLOX_DEMO_STAGE_AXIS_V0
#define H_CALLIPHLOX_DEMO_STAGE_AXIS_V0

#include <device/components.h>
#include <device/metadata.h>
#include <device/device.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct DeviceManager;

    struct StageAxisProperties
    {
        struct stage_axis_properties_state_s
        {
            float position;
            float velocity;
        } target, immediate;
        struct PID feedback;
    };

    struct StageAxisPropertyMetadata
    {
        struct Property position;
        struct Property velocity;
        struct
        {
            struct Property proportional;
            struct Property integral;
            struct Property derivative;
        } PID;
    };

    struct StageAxis
    {
        struct Device device;
        enum DeviceState state;

        enum DeviceStatusCode (*set)(struct StageAxis*,
                                     struct StageAxisProperties* settings);
        enum DeviceStatusCode (*get)(const struct StageAxis*,
                                     struct StageAxisProperties* settings);
        enum DeviceStatusCode (*get_meta)(
          const struct StageAxis*,
          struct StageAxisPropertyMetadata* meta);
        enum DeviceStatusCode (*start)(struct StageAxis*);
        enum DeviceStatusCode (*stop)(struct StageAxis*);
    };

    struct StageAxis* stage_axis_open(
      const struct DeviceManager* system,
      const struct DeviceIdentifier* identifier);

    void stage_axis_close(struct StageAxis* self);

    enum DeviceStatusCode stage_axis_set(struct StageAxis* self,
                                         struct StageAxisProperties* settings);

    enum DeviceStatusCode stage_axis_get(const struct StageAxis* self,
                                         struct StageAxisProperties* settings);

    enum DeviceStatusCode stage_axis_get_meta(
      const struct StageAxis* self,
      struct StageAxisPropertyMetadata* meta);

    enum DeviceStatusCode stage_axis_start(struct StageAxis* self);

    enum DeviceStatusCode stage_axis_stop(struct StageAxis* self);

#ifdef __cplusplus
}
#endif

#endif // H_CALLIPHLOX_DEMO_STAGE_AXIS_V0