/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef FLAIROS_IO_H
#define FLAIROS_IO_H

/*
 * FlairOS I/O manager.
 *
 * This is an NT-inspired driver/device/request model, not an NT-compatible
 * ABI. Dispatch is synchronous in this first implementation.
 */

#define IO_DRIVER_MAX 16u
#define IO_DEVICE_MAX 32u
#define IO_NAME_MAX 24u

enum io_major_function {
    IO_MJ_CREATE = 0,
    IO_MJ_CLOSE,
    IO_MJ_READ,
    IO_MJ_WRITE,
    IO_MJ_DEVICE_CONTROL,
    IO_MJ_MAX
};

enum io_status {
    IO_STATUS_SUCCESS = 0,
    IO_STATUS_INVALID = -1,
    IO_STATUS_NOT_FOUND = -2,
    IO_STATUS_NOT_SUPPORTED = -3,
    IO_STATUS_BUSY = -4,
    IO_STATUS_NO_SPACE = -5,
    IO_STATUS_IO_ERROR = -6
};

struct io_device;
struct io_request;

typedef int (*io_dispatch_fn)(struct io_device *, struct io_request *);

struct io_request {
    enum io_major_function major;
    void *buffer;
    unsigned int length;
    unsigned int offset;
    unsigned int control_code;
    unsigned int information;
    int status;
    void *driver_context;
};

struct io_driver {
    const char *name;
    io_dispatch_fn dispatch[IO_MJ_MAX];
    int registered;
};

struct io_device {
    const char *name;
    struct io_driver *driver;
    void *extension;
    unsigned int open_count;
    unsigned int flags;
    int registered;
};

void io_manager_init(void);
int io_register_driver(struct io_driver *driver);
int io_create_device(struct io_driver *driver, struct io_device *device,
                     const char *name, void *extension);
int io_delete_device(struct io_device *device);
struct io_device *io_lookup_device(const char *name);
int io_call_driver(struct io_device *device, struct io_request *request);
int io_open(const char *name, struct io_device **out_device);
int io_close(struct io_device *device);
unsigned int io_driver_count(void);
unsigned int io_device_count(void);

#endif
