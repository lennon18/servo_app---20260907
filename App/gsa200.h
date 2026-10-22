#ifndef __GSA200_H__
#define __GSA200_H__

#include <stdint.h>

#define GSA200_BAUDRATE (921600U)

typedef struct
{
    float gyro_dps[3];
    float accel_g[3];
    float temperature_c;
    uint8_t sample_counter;
    uint8_t data_valid;
    uint32_t valid_frames;
    uint32_t checksum_errors;
    uint32_t format_errors;
    uint32_t sequence_errors;
    uint32_t dropped_frames;
    uint32_t uart_errors;
    uint32_t active_baudrate;
    uint32_t last_valid_ms;
    uint8_t last_frame[32];
} gsa200_data_t;

extern volatile gsa200_data_t g_gsa200;

/* Flat mirrors are intentionally kept for Cortex-Debug Live Watch. */
extern volatile float gsa200_gyro_x_dps;
extern volatile float gsa200_gyro_y_dps;
extern volatile float gsa200_gyro_z_dps;
extern volatile float gsa200_accel_x_g;
extern volatile float gsa200_accel_y_g;
extern volatile float gsa200_accel_z_g;
extern volatile float gsa200_temperature_c;
extern volatile uint32_t gsa200_sample_counter;
extern volatile uint32_t gsa200_valid_frames;
extern volatile uint32_t gsa200_checksum_errors;
extern volatile uint32_t gsa200_format_errors;
extern volatile uint32_t gsa200_sequence_errors;
extern volatile uint32_t gsa200_dropped_frames;
extern volatile uint32_t gsa200_uart_errors;
extern volatile uint32_t gsa200_valid_frames_per_sec;
extern volatile uint32_t gsa200_checksum_errors_per_sec;
extern volatile uint32_t gsa200_format_errors_per_sec;
extern volatile uint32_t gsa200_sequence_errors_per_sec;
extern volatile uint32_t gsa200_dropped_frames_per_sec;
extern volatile uint32_t gsa200_uart_errors_per_sec;
extern volatile uint32_t gsa200_active_baudrate;
extern volatile uint32_t gsa200_last_valid_ms;
extern volatile uint8_t gsa200_data_valid;

void gsa200_init(void);
void gsa200_poll(uint32_t now_ms);

#endif /* __GSA200_H__ */
