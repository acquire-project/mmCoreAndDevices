#ifndef H_CALLIPHLOX_DEMO_STORAGE_V0
#define H_CALLIPHLOX_DEMO_STORAGE_V0

#include "device.h"
#include "components.h"

#ifdef __cplusplus
extern "C"
{
#endif
    struct DeviceManager;
    struct VideoFrame;

    /// Properties for a storage driver.
    struct StorageProperties
    {
        struct String filename;
        struct String external_metadata_json;
        uint32_t first_frame_id;
    };

    struct Storage
    {
        struct Device device;
        enum DeviceState state;

        enum DeviceState (*set)(struct Storage* self,
                                const struct StorageProperties* settings);
        void (*get)(const struct Storage* self,
                    struct StorageProperties* settings);
        enum DeviceState (*start)(struct Storage* self);
        enum DeviceState (*append)(struct Storage* self,
                                   const struct VideoFrame* frame,
                                   size_t nbytes);
        enum DeviceState (*stop)(struct Storage* self);

        // Only call this from within storage.driver.c.
        // Should really be private to that module.
        void (*destroy)(struct Storage* self);
    };

    /// Initializes StorageProperties, allocating string storage on the heap
    /// and filling out the struct fields.
    /// @returns `Device_Err` when `bytes_of_out` is not large enough.
    /// @param[out] The constructed StorageProperties object.
    /// @param[in] first_frame_id (unused; aiming for future file rollover
    /// support
    /// @param[in] filename A c-style null-terminated string. The file to create
    ///                     for streaming.
    /// @param[in] bytes_of_filename Number of bytes in the `filename` buffer
    ///                              including the terminating null.
    /// @param[in] metadata A c-style null-terminated string. Metadata string
    ///                     to save along side the created file.
    /// @param[in] bytes_of_metadata Number of bytes in the `metadata` buffer
    ///                              including the terminating null.
    enum DeviceStatusCode storage_properties_init(struct StorageProperties* out,
                                                  uint32_t first_frame_id,
                                                  const char* filename,
                                                  size_t bytes_of_filename,
                                                  const char* metadata,
                                                  size_t bytes_of_metadata);

    /// Copies contents, reallocating string storage if necessary.
    /// @param[in,out] dst Must be zero initialized or previously initialized
    ///                    via `storage_properties_init()`
    /// @param[in] src Copied to `dst`
    enum DeviceStatusCode storage_properties_copy(
      struct StorageProperties* dst,
      const struct StorageProperties* src);

    /// Free's allocated string storage.
    void storage_properties_destroy(struct StorageProperties* self);

    /// Checks that a storage device can be initialize with the given settings.
    /// @returns True if settings appear valid, otherwise False
    int storage_validate(const struct DeviceManager* system,
                         const struct DeviceIdentifier* identifier,
                         const struct StorageProperties* settings);

    struct Storage* storage_open(const struct DeviceManager* system,
                                 const struct DeviceIdentifier* identifier,
                                 struct StorageProperties* settings);

    enum DeviceStatusCode storage_get(const struct Storage* self,
                                      struct StorageProperties* settings);

    enum DeviceStatusCode storage_append(struct Storage* self,
                                         const struct VideoFrame* frame,
                                         size_t nbytes);

    enum DeviceStatusCode storage_close(struct Storage* self);

    const char* storage_state_to_string(enum DeviceState state);

#ifdef __cplusplus
}
#endif

#endif // H_CALLIPHLOX_DEMO_STORAGE_V0