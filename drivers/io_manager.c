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


/* ---- Boot-time self-test (requirement REQ-IO-1 .. REQ-IO-6, see docs/REQUIREMENTS.md) ---- */
#ifdef FLAIROS_HOST_TEST
static void serial_str(const char *s) { (void)s; }
#else
void serial_str(const char *s);
#endif

struct io_selftest_ext { unsigned char data[32]; unsigned int size; };

static int io_selftest_dispatch(struct io_device *device, struct io_request *request) {
    struct io_selftest_ext *e = (struct io_selftest_ext *)device->extension;
    unsigned int i, n;
    switch (request->major) {
    case IO_MJ_CREATE:
    case IO_MJ_CLOSE:
        return IO_STATUS_SUCCESS;
    case IO_MJ_WRITE:
        if (request->offset > sizeof(e->data) || request->length > sizeof(e->data) - request->offset)
            return IO_STATUS_NO_SPACE;
        for (i = 0; i < request->length; ++i)
            e->data[request->offset + i] = ((const unsigned char *)request->buffer)[i];
        if (request->offset + request->length > e->size) e->size = request->offset + request->length;
        request->information = request->length;
        return IO_STATUS_SUCCESS;
    case IO_MJ_READ:
        if (request->offset > e->size) return IO_STATUS_INVALID;
        n = e->size - request->offset;
        if (n > request->length) n = request->length;
        for (i = 0; i < n; ++i)
            ((unsigned char *)request->buffer)[i] = e->data[request->offset + i];
        request->information = n;
        return IO_STATUS_SUCCESS;
    case IO_MJ_DEVICE_CONTROL:
        if (request->control_code == 0x100u) { request->information = e->size; return IO_STATUS_SUCCESS; }
        return IO_STATUS_NOT_SUPPORTED;
    default:
        return IO_STATUS_NOT_SUPPORTED;
    }
}

int io_selftest(void) {
    static struct io_selftest_ext ext;
    static struct io_driver drv = { "selftest", { io_selftest_dispatch, io_selftest_dispatch,
        io_selftest_dispatch, io_selftest_dispatch, io_selftest_dispatch }, 0 };
    static struct io_device dev;
    struct io_device *opened = 0;
    struct io_request rq;
    unsigned char msg[8] = {'F','l','a','i','r','O','S',0}, back[8] = {0};
    unsigned int base_drivers = driver_count, base_devices = device_count, i;
    int ok = 1;
#define IO_CHECK(c) do { if (!(c)) ok = 0; } while (0)
    ext.size = 0;
    IO_CHECK(io_register_driver(&drv) == IO_STATUS_SUCCESS);
    IO_CHECK(io_register_driver(&drv) == IO_STATUS_INVALID);
    IO_CHECK(io_create_device(&drv, &dev, "selftest0", &ext) == IO_STATUS_SUCCESS);
    IO_CHECK(io_create_device(&drv, &dev, "selftest0", &ext) == IO_STATUS_INVALID);
    IO_CHECK(io_lookup_device("selftest0") == &dev);
    IO_CHECK(io_open("nope", &opened) == IO_STATUS_NOT_FOUND);
    IO_CHECK(io_open("selftest0", &opened) == IO_STATUS_SUCCESS && opened == &dev && dev.open_count == 1);
    IO_CHECK(io_delete_device(&dev) == IO_STATUS_BUSY);
    rq.major = IO_MJ_WRITE; rq.buffer = msg; rq.length = sizeof(msg); rq.offset = 0;
    rq.control_code = 0; rq.information = 0; rq.status = 0; rq.driver_context = 0;
    IO_CHECK(io_call_driver(opened, &rq) == IO_STATUS_SUCCESS && rq.information == sizeof(msg));
    rq.major = IO_MJ_READ; rq.buffer = back; rq.length = sizeof(back); rq.offset = 0; rq.information = 0;
    IO_CHECK(io_call_driver(opened, &rq) == IO_STATUS_SUCCESS && rq.information == sizeof(back));
    for (i = 0; i < sizeof(msg); ++i) IO_CHECK(msg[i] == back[i]);
    rq.offset = 99; IO_CHECK(io_call_driver(opened, &rq) == IO_STATUS_INVALID);
    rq.major = IO_MJ_WRITE; rq.buffer = msg; rq.length = sizeof(msg); rq.offset = 30;
    IO_CHECK(io_call_driver(opened, &rq) == IO_STATUS_NO_SPACE);
    rq.major = IO_MJ_DEVICE_CONTROL; rq.control_code = 0x100u; rq.information = 0;
    IO_CHECK(io_call_driver(opened, &rq) == IO_STATUS_SUCCESS && rq.information == sizeof(msg));
    rq.control_code = 0xDEADu;
    IO_CHECK(io_call_driver(opened, &rq) == IO_STATUS_NOT_SUPPORTED);
    IO_CHECK(io_close(opened) == IO_STATUS_SUCCESS);
    IO_CHECK(io_delete_device(&dev) == IO_STATUS_SUCCESS);
    IO_CHECK(device_count == base_devices);
    /* Retire the private driver so the test leaves the manager exactly as found. */
    if (driver_count == base_drivers + 1 && drivers[driver_count - 1] == &drv) {
        drivers[--driver_count] = 0;
        drv.registered = 0;
    } else ok = 0;
#undef IO_CHECK
    serial_str(ok ? "io: selftest passed\n" : "io: selftest FAILED\n");
    return ok;
}
