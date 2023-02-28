#ifndef H_CALLIPHLOX_DEMO_SIGNALS_V0
#define H_CALLIPHLOX_DEMO_SIGNALS_V0

#include <device/components.h>
#include <device/metadata.h>
#include <device/device.h>

struct Channel
{
    enum SampleType sample_type;
    enum SignalType signal_type;
    enum SignalIOKind signal_io_kind;
    struct VoltageRange voltage_range;
    uint8_t line; // logical line id
    char display_name[64];
};

struct SignalProperties
{
    struct signal_properties_channels_s
    {
        uint8_t line_count;
        struct Channel lines[32];
    } channels;
    struct signal_properties_timing_s
    {
        uint8_t terminal;
        enum TriggerEdge edge;
        struct SampleRateHz samples_per_second;
    } timing;
    struct signal_properties_triggers_s
    {
        uint8_t line_count;
        struct Trigger lines[32];
    } triggers;
};

struct SignalPropertyMetadata
{
    struct signal_properties_metadata_channels_s
    {
        uint8_t line_count;

        char display_names[32][32];
        size_t logical_ids[32];
        uint32_t input;
        uint32_t output;

        uint64_t supported_sample_types;
    } channels;
};

struct Signal
{
    struct Device device;
    enum DeviceState state;

    enum DeviceState (*set)(struct Signal* self,
                            struct SignalProperties* settings);
    enum DeviceState (*get)(const struct Signal* self,
                            struct SignalProperties* settings);
    enum DeviceState (*get_meta)(const struct Signal* self,
                                 struct SignalPropertyMetadata* meta);
    enum DeviceState (*start)(struct Signal* self);
    enum DeviceState (*stop)(struct Signal* self);

    enum DeviceState (*write_ao)(struct Signal* self,
                                 uint8_t* buf,
                                 size_t nbytes);
    // TODO: Finish Signal.
};

#endif // H_CALLIPHLOX_DEMO_SIGNALS_V0
