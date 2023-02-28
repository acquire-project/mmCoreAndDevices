#ifndef H_CALLIPHLOX_METADATA_V0
#define H_CALLIPHLOX_METADATA_V0

#ifdef __cplusplus
extern "C"
{
#endif

    enum PropertyType
    {
        PropertyType_FixedPrecision,
        PropertyType_FloatingPrecision,
        PropertyType_Enum,
        PropertyType_String,
    };

    struct Property
    {
        unsigned writable;
        float low, high;
        enum PropertyType type;
    };

#ifdef __cplusplus
}
#endif

#endif // H_CALLIPHLOX_METADATA_V0

// FIXME: Property should indicate which states are writable (mask or runlevel)