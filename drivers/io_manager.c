/* SPDX-License-Identifier: AGPL-3.0-only */
/*
 * NT-inspired I/O manager for FlairOS.
 *
 * The registry is intentionally fixed-size and synchronous while the kernel
 * has no general-purpose scheduler, locking primitives, or asynchronous IRP
 * completion path. Keep this layer independent from device-specific drivers.
 */

#include "../include/io.h"

static struct io_driver *drivers[IO_DRIVER_MAX];
static struct io_device *devices[IO_DEVICE_MAX];
static unsigned int driver_count;
static unsigned int device_count;

static int name_valid(const char *name) {
    unsigned int n = 0;
    if (!name || !name[0])
        return 0;
    while (name[n]) {
        if (++n >= IO_NAME_MAX)
            return 0;
    }
    return 1;
}

static int name_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int driver_is_registered(const struct io_driver *driver) {
    unsigned int i;
    for (i = 0; i < driver_count; ++i)
        if (drivers[i] == driver)
            return 1;
    return 0;
}

static int device_index(const struct io_device *device) {
    unsigned int i;
    for (i = 0; i < device_count; ++i)
        if (devices[i] == device)
            return (int)i;
    return -1;
}

void io_manager_init(void) {
    unsigned int i;
    for (i = 0; i < driver_count; ++i)
        drivers[i]->registered = 0;
    for (i = 0; i < device_count; ++i) {
        devices[i]->registered = 0;
        devices[i]->open_count = 0;
    }
    for (i = 0; i < IO_DRIVER_MAX; ++i)
        drivers[i] = 0;
    for (i = 0; i < IO_DEVICE_MAX; ++i)
        devices[i] = 0;
    driver_count = 0;
    device_count = 0;
}

int io_register_driver(struct io_driver *driver) {
    unsigned int i;
    int has_dispatch = 0;

    if (!driver || !name_valid(driver->name) || driver_is_registered(driver))
        return IO_STATUS_INVALID;
    if (driver_count >= IO_DRIVER_MAX)
        return IO_STATUS_NO_SPACE;

    for (i = 0; i < IO_MJ_MAX; ++i)
        if (driver->dispatch[i])
            has_dispatch = 1;
    if (!has_dispatch)
        return IO_STATUS_INVALID;

    for (i = 0; i < driver_count; ++i)
        if (name_equal(drivers[i]->name, driver->name))
            return IO_STATUS_BUSY;

    drivers[driver_count++] = driver;
    driver->registered = 1;
    return IO_STATUS_SUCCESS;
}

int io_create_device(struct io_driver *driver, struct io_device *device,
                     const char *name, void *extension) {
    unsigned int i;

    if (!driver || !driver_is_registered(driver) || !device ||
        !name_valid(name) || device_index(device) >= 0)
        return IO_STATUS_INVALID;
    if (device_count >= IO_DEVICE_MAX)
        return IO_STATUS_NO_SPACE;

    for (i = 0; i < device_count; ++i)
        if (name_equal(devices[i]->name, name))
            return IO_STATUS_BUSY;

    device->name = name;
    device->driver = driver;
    device->extension = extension;
    device->open_count = 0;
    device->flags = 0;
    device->registered = 1;
    devices[device_count++] = device;
    return IO_STATUS_SUCCESS;
}

int io_delete_device(struct io_device *device) {
    int index = device_index(device);
    unsigned int i;

    if (index < 0 || !device->registered)
        return IO_STATUS_NOT_FOUND;
    if (device->open_count != 0)
        return IO_STATUS_BUSY;

    device->registered = 0;
    device->driver = 0;
    device->extension = 0;
    device->name = 0;
    for (i = (unsigned int)index; i + 1u < device_count; ++i)
        devices[i] = devices[i + 1u];
    devices[--device_count] = 0;
    return IO_STATUS_SUCCESS;
}

struct io_device *io_lookup_device(const char *name) {
    unsigned int i;
    if (!name_valid(name))
        return 0;
    for (i = 0; i < device_count; ++i)
        if (name_equal(devices[i]->name, name))
            return devices[i];
    return 0;
}

int io_call_driver(struct io_device *device, struct io_request *request) {
    io_dispatch_fn dispatch;
    int status;

    if (!device || !device->registered || device_index(device) < 0 ||
        !request || (unsigned int)request->major >= (unsigned int)IO_MJ_MAX)
        return IO_STATUS_INVALID;

    if ((request->major == IO_MJ_READ || request->major == IO_MJ_WRITE) &&
        request->length != 0 && !request->buffer)
        return IO_STATUS_INVALID;

    dispatch = device->driver->dispatch[request->major];
    request->information = 0;
    request->status = IO_STATUS_NOT_SUPPORTED;
    if (!dispatch)
        return IO_STATUS_NOT_SUPPORTED;

    status = dispatch(device, request);
    request->status = status;
    return status;
}

int io_open(const char *name, struct io_device **out_device) {
    struct io_device *device;
    struct io_request request;
    int status;

    if (!out_device)
        return IO_STATUS_INVALID;
    *out_device = 0;
    device = io_lookup_device(name);
    if (!device)
        return IO_STATUS_NOT_FOUND;
    if (device->open_count == 0xFFFFFFFFu)
        return IO_STATUS_BUSY;

    request.major = IO_MJ_CREATE;
    request.buffer = 0;
    request.length = 0;
    request.offset = 0;
    request.control_code = 0;
    request.information = 0;
    request.status = IO_STATUS_SUCCESS;
    request.driver_context = 0;
    status = io_call_driver(device, &request);
    if (status != IO_STATUS_SUCCESS)
        return status;

    ++device->open_count;
    *out_device = device;
    return IO_STATUS_SUCCESS;
}

int io_close(struct io_device *device) {
    struct io_request request;
    int status;

    if (!device || !device->registered || device_index(device) < 0)
        return IO_STATUS_INVALID;
    if (device->open_count == 0)
        return IO_STATUS_INVALID;

    request.major = IO_MJ_CLOSE;
    request.buffer = 0;
    request.length = 0;
    request.offset = 0;
    request.control_code = 0;
    request.information = 0;
    request.status = IO_STATUS_SUCCESS;
    request.driver_context = 0;
    status = io_call_driver(device, &request);
    if (status == IO_STATUS_SUCCESS)
        --device->open_count;
    return status;
}

unsigned int io_driver_count(void) {
    return driver_count;
}

unsigned int io_device_count(void) {
    return device_count;
}
