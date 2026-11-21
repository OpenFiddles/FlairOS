/* SPDX-License-Identifier: AGPL-3.0-only */
#include <stdio.h>
#include <string.h>
#include "io.h"

struct test_storage {
    unsigned char bytes[64];
    unsigned int size;
};

static int test_dispatch(struct io_device *device, struct io_request *request) {
    struct test_storage *storage = (struct test_storage *)device->extension;
    unsigned int count;

    switch (request->major) {
    case IO_MJ_CREATE:
    case IO_MJ_CLOSE:
        return IO_STATUS_SUCCESS;
    case IO_MJ_READ:
        if (request->offset > storage->size)
            return IO_STATUS_INVALID;
        count = storage->size - request->offset;
        if (count > request->length)
            count = request->length;
        memcpy(request->buffer, storage->bytes + request->offset, count);
        request->information = count;
        return IO_STATUS_SUCCESS;
    case IO_MJ_WRITE:
        if (request->offset > sizeof(storage->bytes) ||
            request->length > sizeof(storage->bytes) - request->offset)
            return IO_STATUS_INVALID;
        memcpy(storage->bytes + request->offset, request->buffer, request->length);
        if (request->offset + request->length > storage->size)
            storage->size = request->offset + request->length;
        request->information = request->length;
        return IO_STATUS_SUCCESS;
    case IO_MJ_DEVICE_CONTROL:
        if (request->control_code == 0x100u) {
            request->information = storage->size;
            return IO_STATUS_SUCCESS;
        }
        return IO_STATUS_NOT_SUPPORTED;
    default:
        return IO_STATUS_NOT_SUPPORTED;
    }
}

int main(void) {
    static struct test_storage storage;
    static struct io_driver driver = {
        "test-storage",
        { test_dispatch, test_dispatch, test_dispatch, test_dispatch, test_dispatch },
        0
    };
    static struct io_device device;
    struct io_device *opened = 0;
    struct io_request request;
    char input[] = "FlairOS I/O manager";
    char output[sizeof(input)];
    int fails = 0;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    io_manager_init();
    CHECK(io_driver_count() == 0);
    CHECK(io_device_count() == 0);
    CHECK(io_register_driver(&driver) == IO_STATUS_SUCCESS);
    CHECK(driver.registered);
    CHECK(io_register_driver(&driver) == IO_STATUS_INVALID);
    CHECK(io_create_device(&driver, &device, "disk0", &storage) == IO_STATUS_SUCCESS);
    CHECK(io_device_count() == 1);
    CHECK(io_lookup_device("disk0") == &device);
    CHECK(io_create_device(&driver, &device, "disk0", &storage) == IO_STATUS_INVALID);
    CHECK(io_open("missing", &opened) == IO_STATUS_NOT_FOUND);
    CHECK(io_open("disk0", &opened) == IO_STATUS_SUCCESS);
    CHECK(opened == &device && device.open_count == 1);
    CHECK(io_delete_device(&device) == IO_STATUS_BUSY);

    request.major = IO_MJ_WRITE;
    request.buffer = input;
    request.length = sizeof(input);
    request.offset = 0;
    request.control_code = 0;
    request.driver_context = 0;
    CHECK(io_call_driver(opened, &request) == IO_STATUS_SUCCESS);
    CHECK(request.information == sizeof(input));

    request.major = IO_MJ_READ;
    request.buffer = output;
    request.length = sizeof(output);
    request.offset = 0;
    CHECK(io_call_driver(opened, &request) == IO_STATUS_SUCCESS);
    CHECK(request.information == sizeof(input));
    CHECK(memcmp(input, output, sizeof(input)) == 0);

    request.major = IO_MJ_DEVICE_CONTROL;
    request.buffer = 0;
    request.length = 0;
    request.control_code = 0x100u;
    CHECK(io_call_driver(opened, &request) == IO_STATUS_SUCCESS);
    CHECK(request.information == sizeof(input));

    request.major = (enum io_major_function)IO_MJ_MAX;
    CHECK(io_call_driver(opened, &request) == IO_STATUS_INVALID);
    CHECK(io_close(opened) == IO_STATUS_SUCCESS);
    CHECK(device.open_count == 0);
    CHECK(io_close(opened) == IO_STATUS_INVALID);
    CHECK(io_delete_device(opened) == IO_STATUS_SUCCESS);
    CHECK(io_lookup_device("disk0") == 0);
    CHECK(io_device_count() == 0);

    if (!fails)
        puts("I/O manager host test: all checks passed");
    return fails ? 1 : 0;
}
