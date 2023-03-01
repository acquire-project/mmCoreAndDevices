#ifndef H_CALLIPHLOX_DEMO_DEVICE_MANAGER_V0
#define H_CALLIPHLOX_DEMO_DEVICE_MANAGER_V0

#include <stdint.h>
#include <device/device.h>

#ifdef __cplusplus
extern "C"
{
#endif
    struct DeviceIdentifier;

    struct DeviceManager
    {
        void* impl;
    };

    enum DeviceStatusCode device_manager_init(struct DeviceManager* self);
    enum DeviceStatusCode device_manager_destroy(struct DeviceManager* self);

    uint32_t device_manager_count(const struct DeviceManager* self);

    enum DeviceStatusCode device_manager_get(struct DeviceIdentifier* out,
                                             const struct DeviceManager* self,
                                             uint32_t index);

    /// Query for a device with a matching `kind` and `name`.
    ///
    /// @param[in] self     The deviceManager context to query.
    /// @param[in] kind     The kind of device to select.
    /// @param[in] name     A regex pattern matching the name of the device.
    ///                     May be NULL or empty. Does not need to be null
    ///                     terminated. If NULL or empty, the query returns
    ///                     the first device matching `kind`.
    /// @param[in] bytes_of_name The number of bytes in the "name" string.
    ///                     Should exclude any terminating NULLs.
    /// @param[out] out     The id of the first device discovered that matches
    ///                     the `kind` and `name`.
    enum DeviceStatusCode device_manager_select(
      const struct DeviceManager* self,
      enum DeviceKind kind,
      const char* name,
      size_t bytes_of_name,
      struct DeviceIdentifier* out);

    struct Driver* device_manager_get_driver(
      const struct DeviceManager* self,
      const struct DeviceIdentifier* identifier);

#ifdef __cplusplus
}
#endif

#endif // H_CALLIPHLOX_DEMO_DEVICE_MANAGER_V0

/*
USAGE

*/

// FIXME: this api is bad. get descriptor/identifier + get device.  Why both?
// clarify.