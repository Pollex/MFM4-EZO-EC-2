#include "mfm_comm.h"
#include "periph/cpu_gpio.h"
#include "periph/gpio.h"
#include "periph/i2c.h"
#include "sched.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <ztimer.h>
#define ENABLE_DEBUG 0
#include <debug.h>

#ifndef MFM_COMM_ID1_PIN
// #define MFM_COMM_ID1_PIN GPIO_UNDEF
#endif /* ifndef MFM_COMM_ID1_PIN */
#ifndef MFM_COMM_ID2_PIN
// #define MFM_COMM_ID2_PIN GPIO_UNDEF
#endif /* ifndef MFM_COMM_ID2_PIN */
#ifndef MFM_COMM_ID3_PIN
// #define MFM_COMM_ID3_PIN GPIO_UNDEF
#endif /* ifndef MFM_COMM_ID3_PIN */

enum { max_payload_len = 36 };

// ==========================
// Forward definitions
// ==========================

uint16_t calculateCRC_CCITT(uint8_t *data, int length);

int read_firmware_version(mfm_comm_t *comm, uint8_t *data);
int read_protocol_version(mfm_comm_t *comm, uint8_t *data);
int read_sensor_type(mfm_comm_t *comm, uint8_t *data);
int read_init_start(mfm_comm_t *comm, uint8_t *data);
int read_init_status(mfm_comm_t *comm, uint8_t *data);
int read_meas_start(mfm_comm_t *comm, uint8_t *data);
int read_meas_status(mfm_comm_t *comm, uint8_t *data);
int read_meas_time(mfm_comm_t *comm, uint8_t *data);
int read_meas_data(mfm_comm_t *comm, uint8_t *data);
int read_sensor_amount(mfm_comm_t *comm, uint8_t *data);
int read_sensor_selected(mfm_comm_t *comm, uint8_t *data);
int read_meas_type(mfm_comm_t *comm, uint8_t *data);
int read_meas_samples(mfm_comm_t *comm, uint8_t *data);
int read_sensor_data(mfm_comm_t *comm, uint8_t *data);
int read_error_count(mfm_comm_t *comm, uint8_t *data);
int read_error_status(mfm_comm_t *comm, uint8_t *data);

void write_init_start(mfm_comm_t *comm, uint8_t *data, uint8_t len);
void write_meas_start(mfm_comm_t *comm, uint8_t *data, uint8_t len);
void write_meas_time(mfm_comm_t *comm, uint8_t *data, uint8_t len);
void write_sensor_selected(mfm_comm_t *comm, uint8_t *data, uint8_t len);
void write_meas_type(mfm_comm_t *comm, uint8_t *data, uint8_t len);
void write_meas_samples(mfm_comm_t *comm, uint8_t *data, uint8_t len);
void write_error_status(mfm_comm_t *comm, uint8_t *data, uint8_t len);

// ==========================
// Type definitions
// ==========================

typedef enum {
    REG_FIRMWARE_VERSION = 0x01,
    REG_PROTOCOL_VERSION,
    REG_SENSOR_TYPE,
    REG_INIT_START = 0x0A,
    REG_INIT_STATUS,
    REG_MEAS_START = 0x10,
    REG_MEAS_STATUS,
    REG_MEAS_TIME,
    REG_MEAS_DATA     = 0x20,
    REG_SENSOR_AMOUNT = 0x30,
    REG_SENSOR_SELECTED,
    REG_MEAS_TYPE,
    REG_MEAS_SAMPLES,
    REG_SENSOR_DATA = 0x38,
    REG_CONTROL_IO  = 0x40,
    REG_DIRECTION_IO,
    REG_ERROR_COUNT = 0x50,
    REG_ERROR_STATUS,
} reg_id_t;

typedef int (*reg_read_func_t)(mfm_comm_t *comm, uint8_t *data);
typedef void (*reg_write_func_t)(mfm_comm_t *comm, uint8_t *data, uint8_t len);

typedef struct {
    reg_id_t id;
    uint8_t write_size;
    reg_read_func_t read_fn;
    reg_write_func_t write_fn;
} reg_desc_t;

typedef enum {
    COMMAND_INACTIVE     = 0x00,
    COMMAND_ACTIVE       = 0x01,
    COMMAND_DONE         = 0x0A,
    COMMAND_FAILED       = 0x0F,
    COMMAND_ERROR        = 0xF0,
    COMMAND_NOTAVAILABLE = 0xFF,
} cmd_status_t;

#define LEN_BYTES  1
#define CRC_BYTES  2
#define DATA_BYTES 52
#define REG_BYTES  1
// In case of READ, buffer is prefixed with a byte indicating the lenght of the
// data and suffixed with 2 CRC bytes In case of WRITE, buffer is prefixed with
// the register byte and suffixed with 2 CRC bytes
// #define BUFFER_SIZE LEN_BYTES + DATA_BYTES + CRC_BYTES
#define BUFFER_SIZE 255

// ==========================
// Register implementation
// ==========================

const reg_desc_t registers[] = {
    {REG_FIRMWARE_VERSION, 0, read_firmware_version, NULL                 },
    {REG_PROTOCOL_VERSION, 0, read_protocol_version, NULL                 },
    {REG_SENSOR_TYPE,      0, read_sensor_type,      NULL                 },
    {REG_INIT_START,       1, read_init_start,       write_init_start     },
    {REG_INIT_STATUS,      1, read_init_status,      NULL                 },
    {REG_MEAS_START,       1, read_meas_start,       write_meas_start     },
    {REG_MEAS_STATUS,      0, read_meas_status,      NULL                 },
    {REG_MEAS_TIME,        2, read_meas_time,        write_meas_time      },
    {REG_MEAS_DATA,        0, read_meas_data,        NULL                 },
    {REG_SENSOR_AMOUNT,    0, read_sensor_amount,    NULL                 },
    {REG_SENSOR_SELECTED,  1, read_sensor_selected,  write_sensor_selected},
    {REG_MEAS_TYPE,        1, read_meas_type,        write_meas_type      },
    {REG_MEAS_SAMPLES,     1, read_meas_samples,     write_meas_samples   },
    {REG_SENSOR_DATA,      0, read_sensor_data,      NULL                 },
    {REG_CONTROL_IO,       0, NULL,                  NULL                 },
    {REG_DIRECTION_IO,     0, NULL,                  NULL                 },
    {REG_ERROR_COUNT,      0, read_error_count,      NULL                 },
    {REG_ERROR_STATUS,     1, read_error_status,     write_error_status   },
};
const size_t reg_count = sizeof(registers) / sizeof(reg_desc_t);

const reg_desc_t *find_register(uint8_t id) {
    for (unsigned i = 0; i < reg_count; i++) {
        if (registers[i].id == id)
            return &registers[i];
    }
    return NULL;
}

static uint8_t buffer[BUFFER_SIZE] = {0};
static i2c_slave_fsm_t i2c_slave;

// ==========================
// Readable register functions
// ==========================

int read_firmware_version(mfm_comm_t *comm, uint8_t *data) {
    uint8_t data_len = strlen(comm->params.firmware_version);
    if (data_len > 10)
        data_len = 10;
    memcpy(data, (uint8_t *)comm->params.firmware_version, data_len);
    return data_len;
}

int read_protocol_version(mfm_comm_t *comm, uint8_t *data) {
    data[0] = comm->protocol_version;
    return 1;
}

int read_sensor_type(mfm_comm_t *comm, uint8_t *data) {
    data[0] = 0;
    data[1] = comm->params.module_type;
    return 2;
}

int read_init_start(mfm_comm_t *comm, uint8_t *data) {
    data[0] = (comm->sensor_init_status == COMMAND_ACTIVE);
    return 1;
}

int read_init_status(mfm_comm_t *comm, uint8_t *data) {
    data[0] = comm->sensor_init_status;
    return 1;
}

int read_meas_start(mfm_comm_t *comm, uint8_t *data) {
    data[0] = (comm->measurement_status == COMMAND_ACTIVE);
    return 1;
}

int read_meas_status(mfm_comm_t *comm, uint8_t *data) {
    data[0] = comm->measurement_status;
    return 1;
}

int read_meas_time(mfm_comm_t *comm, uint8_t *data) {
    data[0] = comm->params.measurement_time & 0xFF;
    data[1] = (comm->params.measurement_time >> 8) & 0xFF;
    return 2;
}

int read_meas_data(mfm_comm_t *comm, uint8_t *data) {
    if (comm->payload == NULL || comm->payload_len == 0) {
        data[0] = 0;
        return 1;
    }

    uint8_t copy_len = comm->payload_len;
    if (copy_len > (BUFFER_SIZE - 1 - CRC_BYTES)) {
        copy_len = (BUFFER_SIZE - 1 - CRC_BYTES);
    }
    data[0] = copy_len;
    memcpy(data + 1, comm->payload, copy_len);
    return copy_len + 1;
}

int read_sensor_amount(mfm_comm_t *comm, uint8_t *data) {
    data[0] = comm->params.sensor_count;
    return 1;
}

int read_sensor_selected(mfm_comm_t *comm, uint8_t *data) {
    data[0] = comm->active_sensor;
    return 1;
}

int read_meas_type(mfm_comm_t *comm, uint8_t *data) {
    data[0] = comm->measurement_type;
    return 1;
}

int read_meas_samples(mfm_comm_t *comm, uint8_t *data) {
    data[0] = comm->sample_count;
    return 1;
}

int read_sensor_data(mfm_comm_t *comm, uint8_t *data) {
    data[0] = comm->sensor_value & 0xFF;
    data[1] = (comm->sensor_value >> 8) & 0xFF;
    return 2;
}

int read_error_count(mfm_comm_t *comm, uint8_t *data) {
    data[0] = 0;
    data[1] = comm->error != 0;
    return 2;
}

int read_error_status(mfm_comm_t *comm, uint8_t *data) {
    if (comm->get_app_error > 0) {
        data[0]             = comm->error;
        comm->get_app_error = 0;

        return 1;
    }

    if ((comm->error & (1 << MFM_COMM_ERR_APP)) > 0) {
        comm->get_app_error = 1;
    }

    data[0] = comm->error;

    return 1;
}

// ==========================
// Writable register functions
// ==========================

void write_init_start(mfm_comm_t *comm, uint8_t *data, uint8_t len) {
    (void)data;
    (void)len;
    comm->sensor_init_status = COMMAND_ACTIVE;

    // Trigger the mfr sensor init.
    int result = comm->params.sensor_init_fn(comm);
    if (result < 0) {
        comm->error              = -result;
        comm->sensor_init_status = COMMAND_ERROR;
        return;
    }

    // From this point the usercode MUST call mfm_comm_sensor_init_finish(...) or error.
}

void write_meas_start(mfm_comm_t *comm, uint8_t *data, uint8_t len) {
    (void)data;
    (void)len;

    // Enable measuring flag.
    comm->measurement_status = COMMAND_ACTIVE;

    // Trigger a measurement.
    int result = comm->params.perform_measurement_fn(comm);
    if (result < 0) {
        comm->error              = -result;
        comm->measurement_status = COMMAND_ERROR;
        return;
    }

    // From this point the usercode MUST call mfm_comm_measurement_finish(...) or error.
}

void write_meas_time(mfm_comm_t *comm, uint8_t *data, uint8_t len) {
    (void)len;
    comm->params.measurement_time = data[0] | (data[1] << 8);
}

void write_sensor_selected(mfm_comm_t *comm, uint8_t *data, uint8_t len) {
    (void)len;
    comm->active_sensor = data[0];
}

void write_meas_type(mfm_comm_t *comm, uint8_t *data, uint8_t len) {
    (void)len;
    comm->measurement_type = data[0];
}

void write_meas_samples(mfm_comm_t *comm, uint8_t *data, uint8_t len) {
    (void)len;
    comm->sample_count = data[0];
}

void write_error_status(mfm_comm_t *comm, uint8_t *data, uint8_t len) {
    (void)len;
    comm->error &= ~(data[0]);

    // Reset the application error value if the flag is cleared.
    if ((data[0] & (1 << MFM_COMM_ERR_APP)) > 0) {
        comm->app_error = 0;
    }
}

// ==========================
// Core I2C & MFM Protocol logic
// ==========================

static uint8_t i2c_prepare(uint8_t read, uint16_t addr, uint16_t reg_id, uint8_t **data_ptr, void *arg) {
    (void)addr;

    mfm_comm_t *comm      = arg;
    comm->last_call_addr  = addr;
    const reg_desc_t *reg = find_register(reg_id);

    // Register was not found.
    if (reg == NULL)
        return 0;

    // Master is reading from this register.
    if (read) {
        *data_ptr = buffer;

        // Not a read-capable register.
        if (reg->read_fn == NULL)
            return 0;

        // Prepare buffer with read values.
        int len = reg->read_fn(comm, buffer);

        // Calculate CRC and append to data
        uint16_t crc    = calculateCRC_CCITT(buffer, len);
        buffer[len]     = (crc >> 8) & 0xFF;
        buffer[len + 1] = crc & 0xFF;

        return len + CRC_BYTES;
    }

    // Not a write-capable register.
    if (reg->write_fn == NULL)
        return 0;

    // Master is writing to this register. Append register ID for CRC
    // calculation in i2c_finish and add CRC byte len to expected read size.
    buffer[0] = reg_id;
    *data_ptr = &buffer[1];

    return reg->write_size + CRC_BYTES;
}

static void i2c_finish(uint8_t read, uint16_t addr, uint16_t reg_id, size_t len, void *arg) {
    (void)addr;

    // Nothing to do if a read finishes.
    if (read)
        return;

    // Get the relevant register descriptor.
    const reg_desc_t *reg = find_register(reg_id);

    // Register was not found.
    if (reg == NULL)
        return;

    // Sanity check: Not a write-capable register.
    if (reg->write_fn == NULL)
        return;

    // Should atleast have 1 data byte + 2 CRC bytes
    if (len < 3)
        return;

    // Skips the "reg" byte, which is used for CRC calc I2C master writes to
    // buffer[1...]
    uint8_t *data = &buffer[REG_BYTES];

    // Note: len does NOT include REG_BYTE at buffer[0] Validate Checksum.
    uint16_t crc = (data[len - 2] << 8) | data[len - 1];

    // If the CRC is incorrect do nothing.
    if (crc != calculateCRC_CCITT(buffer, len + REG_BYTES - CRC_BYTES)) {
        return;
    }

    // Make sure there are at least the required amount of data available.
    if (reg->write_size > len - CRC_BYTES) {
        return;
    }

    mfm_comm_t *comm     = arg;
    comm->last_call_addr = addr;
    reg->write_fn(comm, data, len - CRC_BYTES);
}

int read_slot_id(void) {
#if !defined(MFM_COMM_ID1_PIN) || !defined(MFM_COMM_ID2_PIN) || !defined(MFM_COMM_ID3_PIN)
    return 0;
#else
    int id = 0;
    if (gpio_read(MFM_COMM_ID1_PIN) > 0)
        id |= 1 << 0;
    if (gpio_read(MFM_COMM_ID2_PIN) > 0)
        id |= 1 << 1;
    if (gpio_read(MFM_COMM_ID3_PIN) > 0)
        id += 3;
    return id;
#endif
}

kernel_pid_t mfm_comm_init(mfm_comm_t *comm, const mfm_comm_params_t params) {
    assert(comm->is_already_initialized == 0);
    comm->is_already_initialized = 1;

    comm->params = params;
    DEBUG("[%s] registering slave\n", __func__);
    i2c_slave_reg(&i2c_slave, i2c_prepare, i2c_finish, 0, comm);

#if !defined(MFM_COMM_ID1_PIN) || !defined(MFM_COMM_ID2_PIN) || !defined(MFM_COMM_ID3_PIN)
#warning "MFM_COMM module is being used but no MFM_COMM_IDx_PIN defines are set. The module is only addressable on the preconfigured address in the periph_config of the i2c slave."
#else
    gpio_init(MFM_COMM_ID1_PIN, GPIO_IN);
    gpio_init(MFM_COMM_ID2_PIN, GPIO_IN);
    gpio_init(MFM_COMM_ID3_PIN, GPIO_IN);
    i2c_set_addr(params.dev, read_slot_id(), 0, 0);
#endif

    return 0;
}

int mfm_comm_sensor_init_finish(mfm_comm_t *comm) {
    comm->sensor_init_status = COMMAND_DONE;

    return 0;
}

int mfm_comm_measurement_finish(mfm_comm_t *comm, void *payload, uint8_t payload_len) {
    if (payload_len > max_payload_len) {
        return -EMSGSIZE;
    }
    if (payload == NULL) {
        return -EINVAL;
    }

    comm->payload            = payload;
    comm->payload_len        = payload_len;
    comm->measurement_status = COMMAND_DONE;

    return 0;
}

void mfm_comm_measurement_error(mfm_comm_t *comm, uint8_t err) {
    comm->error              = MFM_COMM_ERR_APP;
    comm->app_error          = err;
    comm->measurement_status = COMMAND_ERROR;
}

static const uint16_t CRC16Table[] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241, 0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1,
    0xC481, 0x0440, 0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40, 0x0A00, 0xCAC1, 0xCB81, 0x0B40,
    0xC901, 0x09C0, 0x0880, 0xC841, 0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40, 0x1E00, 0xDEC1,
    0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41, 0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040, 0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1,
    0xF281, 0x3240, 0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441, 0x3C00, 0xFCC1, 0xFD81, 0x3D40,
    0xFF01, 0x3FC0, 0x3E80, 0xFE41, 0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840, 0x2800, 0xE8C1,
    0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41, 0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640, 0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0,
    0x2080, 0xE041, 0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240, 0x6600, 0xA6C1, 0xA781, 0x6740,
    0xA501, 0x65C0, 0x6480, 0xA441, 0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41, 0xAA01, 0x6AC0,
    0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840, 0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40, 0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1,
    0xB681, 0x7640, 0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041, 0x5000, 0x90C1, 0x9181, 0x5140,
    0x9301, 0x53C0, 0x5280, 0x9241, 0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440, 0x9C01, 0x5CC0,
    0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40, 0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40, 0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0,
    0x4C80, 0x8C41, 0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641, 0x8201, 0x42C0, 0x4380, 0x8341,
    0x4100, 0x81C1, 0x8081, 0x4040};

/**
 * @brief Calculate the CRC CCITT
 * @param data The message to calculate the CRC from
 * @param length The lenght of the message
 * @return The 16 bits CRC
 */
#define CRC16INITVALUE 0xFFFF // Initial CRC value
uint16_t calculateCRC_CCITT(uint8_t *data, int length) {
    uint16_t crc = CRC16INITVALUE;
    while (length--)
        crc = (crc >> 8) ^ CRC16Table[*data++ ^ (crc & 0xFF)];
    uint16_t temp = ((crc << 8) & 0xFF00) | (crc >> 8); // swap
    return temp;
}
