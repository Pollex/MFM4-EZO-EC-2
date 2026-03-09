#ifndef APP_MFM_COMM_H
#define APP_MFM_COMM_H

#include "periph/i2c.h"
#include "sched.h"
#include <stdint.h>

typedef int (*mfm_comm_sensor_init_fn)(void *arg);
typedef int (*mfm_comm_perform_measurement_fn)(void *arg);

typedef struct mfm_comm_params_t mfm_comm_params_t;
struct mfm_comm_params_t {
    i2c_t dev;
    char *firmware_version;
    uint8_t module_type;
    uint16_t measurement_time;
    uint8_t sensor_count;
    mfm_comm_sensor_init_fn sensor_init_fn;
    mfm_comm_perform_measurement_fn perform_measurement_fn;
};

typedef struct mfm_comm_t mfm_comm_t;
struct mfm_comm_t {
    mfm_comm_params_t params;
    uint16_t last_call_addr;
    uint8_t is_already_initialized;
    uint8_t measurement_status;
    uint8_t sensor_init_status;
    uint8_t protocol_version;
    uint8_t active_sensor;
    uint8_t measurement_type;
    uint8_t sample_count;
    uint16_t sensor_value;

    uint8_t error;
    uint8_t app_error;
    uint8_t get_app_error;

    // Measurement results buffer.
    void *payload;
    uint8_t payload_len;
};

typedef enum {
    MFM_COMM_ERR_NONE, // No error
    MFM_COMM_ERR_CRC,  // CRC invalid
    MFM_COMM_ERR_REG,  // Register does not exist
    MFM_COMM_ERR_RW,   // Writing a readonly register
    MFM_COMM_ERR_APP,  // Application specific error, read again to get.
} mfm_comm_err_t;

kernel_pid_t mfm_comm_init(mfm_comm_t *comm, mfm_comm_params_t params);
int mfm_comm_sensor_init_finish(mfm_comm_t *comm);
int mfm_comm_measurement_finish(mfm_comm_t *comm, void *payload, uint8_t payload_len);
void mfm_comm_measurement_error(mfm_comm_t *comm, uint8_t err);

#endif /* end of include guard: APP_MFM_COMM_H */
