#ifndef H_CALLIPHLOX_DEMO_DRIVER_COMPONENTS_V0
#define H_CALLIPHLOX_DEMO_DRIVER_COMPONENTS_V0

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus

extern "C"
{
#endif

    struct String
    {
        char* str;

        /// Length of the `str` buffer. Should include the terminating '\0'
        /// if it exists.
        size_t nbytes;

        /// 0 when `str` is heap allocated, otherwise 1.
        /// When 1, then the string needs to live longer than the runtime; it
        /// may have static storage. The caller is responsible for deallocating
        /// any associated resources.
        ///
        /// When 0, storage may be deallocated within the runtime using the
        /// standard library's `free` function.
        uint8_t is_ref;
    };

    struct PID
    {
        float proportional, integral, derivative;
    };

    enum TriggerEvent
    {
        TriggerEvent_AcquisitionStart,
        TriggerEvent_FrameStart,
        TriggerEvent_Exposure,
        TriggerEvent_FrameTriggerWait,
        TriggerEventCount,
        TriggerEvent_Unknown
    };

    enum TriggerEdge
    {
        TriggerEdge_Rising,
        TriggerEdge_Falling,
        TriggerEdge_NotApplicable,
    };

    enum SignalIOKind
    {
        Signal_Input,
        Signal_Output
    };

    struct Trigger
    {
        uint8_t enable;
        uint8_t line;
        enum TriggerEvent event;
        enum SignalIOKind kind;
        enum TriggerEdge edge;
    };

    enum SignalType
    {
        Signal_Analog,
        Signal_Digital,
    };

    enum SampleType
    {
        SampleType_u8,
        SampleType_u16,
        SampleType_i8,
        SampleType_i16,
        SampleType_f32,
        SampleTypeCount,
        SampleType_Unknown
    };

    struct SampleRateHz
    {
        uint64_t numerator, denominator;
    };

    enum Direction
    {
        Direction_Forward,
        Direction_Backward,
        Direction_Count,
        Direction_Unknown
    };

    struct VoltageRange
    {
        float mn, mx;
    };

    struct ImageShape
    {
        struct image_dims_s
        {
            uint32_t channels, width, height, planes;
        } dims;
        struct image_strides_s
        {
            int64_t channels, width, height, planes;
        } strides;
        enum SampleType type;
    };

    struct ImageInfo
    {
        struct ImageShape shape;
        uint64_t hardware_timestamp;
    };

    struct VideoFrame
    {
        /// The total number of bytes for this struct plus the
        /// size of the attached data buffer.
        size_t bytes_of_frame;
        struct ImageShape shape;
        uint64_t frame_id;
        struct video_frame_timestamps_s
        {
            uint64_t hardware;
            uint64_t acq_thread;
        } timestamps;
        uint8_t data[];
    };

#ifdef __cplusplus
}
#endif

#endif // H_CALLIPHLOX_DEMO_DRIVER_COMPONENTS_V0