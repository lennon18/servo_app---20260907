#include "gsa200.h"

#include <string.h>

#include "bsp_uart.h"

#define GSA200_FRAME_SIZE (32U)
#define GSA200_HEADER_AA (0xAAU)
#define GSA200_HEADER_55 (0x55U)
#define GSA200_FORMAT_ID (0x01U)
#define GSA200_DATA_TIMEOUT_MS (1000U)

volatile gsa200_data_t g_gsa200;

volatile float gsa200_gyro_x_dps;
volatile float gsa200_gyro_y_dps;
volatile float gsa200_gyro_z_dps;
volatile float gsa200_accel_x_g;
volatile float gsa200_accel_y_g;
volatile float gsa200_accel_z_g;
volatile float gsa200_temperature_c;
volatile uint32_t gsa200_sample_counter;
volatile uint32_t gsa200_valid_frames;
volatile uint32_t gsa200_checksum_errors;
volatile uint32_t gsa200_format_errors;
volatile uint32_t gsa200_sequence_errors;
volatile uint32_t gsa200_dropped_frames;
volatile uint32_t gsa200_uart_errors;
volatile uint32_t gsa200_valid_frames_per_sec;
volatile uint32_t gsa200_checksum_errors_per_sec;
volatile uint32_t gsa200_format_errors_per_sec;
volatile uint32_t gsa200_sequence_errors_per_sec;
volatile uint32_t gsa200_dropped_frames_per_sec;
volatile uint32_t gsa200_uart_errors_per_sec;
volatile uint32_t gsa200_active_baudrate;
volatile uint32_t gsa200_last_valid_ms;
volatile uint8_t gsa200_data_valid;

static uint8_t frame[GSA200_FRAME_SIZE];
static uint8_t frame_index;
static uint8_t have_previous_counter;
static uint32_t rate_last_ms;
static uint32_t rate_valid_frames;
static uint32_t rate_checksum_errors;
static uint32_t rate_format_errors;
static uint32_t rate_sequence_errors;
static uint32_t rate_dropped_frames;
static uint32_t rate_uart_errors;

/* The connected GSA200 sends each IEEE-754 payload word MSB first. */
static uint32_t read_u32_be(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) |
           ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) |
           ((uint32_t)source[3]);
}

static float read_float_be(const uint8_t *source)
{
    uint32_t bits = read_u32_be(source);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint8_t counter_delta(uint8_t previous, uint8_t current)
{
    /* The device counter wraps through 0 (0..255), so the delta is modulo 256. */
    return (uint8_t)(current - previous);
}

static void resync_after_bad_frame(void)
{
    uint8_t index;

    for (index = 1U; index + 1U < GSA200_FRAME_SIZE; ++index)
    {
        if ((frame[index] == GSA200_HEADER_AA) &&
            (frame[index + 1U] == GSA200_HEADER_55))
        {
            memmove(frame, &frame[index], GSA200_FRAME_SIZE - index);
            frame_index = (uint8_t)(GSA200_FRAME_SIZE - index);
            return;
        }
    }
    frame_index = 0U;
}

static void mirror_values(void)
{
    gsa200_gyro_x_dps = g_gsa200.gyro_dps[0];
    gsa200_gyro_y_dps = g_gsa200.gyro_dps[1];
    gsa200_gyro_z_dps = g_gsa200.gyro_dps[2];
    gsa200_accel_x_g = g_gsa200.accel_g[0];
    gsa200_accel_y_g = g_gsa200.accel_g[1];
    gsa200_accel_z_g = g_gsa200.accel_g[2];
    gsa200_temperature_c = g_gsa200.temperature_c;
    gsa200_sample_counter = g_gsa200.sample_counter;
    gsa200_valid_frames = g_gsa200.valid_frames;
    gsa200_checksum_errors = g_gsa200.checksum_errors;
    gsa200_format_errors = g_gsa200.format_errors;
    gsa200_sequence_errors = g_gsa200.sequence_errors;
    gsa200_dropped_frames = g_gsa200.dropped_frames;
    gsa200_uart_errors = g_gsa200.uart_errors;
    gsa200_active_baudrate = g_gsa200.active_baudrate;
    gsa200_last_valid_ms = g_gsa200.last_valid_ms;
    gsa200_data_valid = g_gsa200.data_valid;
}

static void update_rates(uint32_t now_ms)
{
    uint32_t elapsed_ms = (uint32_t)(now_ms - rate_last_ms);

    if (elapsed_ms < 1000U)
    {
        return;
    }

    gsa200_valid_frames_per_sec =
        (uint32_t)(((uint64_t)(g_gsa200.valid_frames - rate_valid_frames) * 1000U) /
                   elapsed_ms);
    gsa200_checksum_errors_per_sec =
        (uint32_t)(((uint64_t)(g_gsa200.checksum_errors - rate_checksum_errors) * 1000U) /
                   elapsed_ms);
    gsa200_format_errors_per_sec =
        (uint32_t)(((uint64_t)(g_gsa200.format_errors - rate_format_errors) * 1000U) /
                   elapsed_ms);
    gsa200_sequence_errors_per_sec =
        (uint32_t)(((uint64_t)(g_gsa200.sequence_errors - rate_sequence_errors) * 1000U) /
                   elapsed_ms);
    gsa200_dropped_frames_per_sec =
        (uint32_t)(((uint64_t)(g_gsa200.dropped_frames - rate_dropped_frames) * 1000U) /
                   elapsed_ms);
    gsa200_uart_errors_per_sec =
        (uint32_t)(((uint64_t)(g_gsa200.uart_errors - rate_uart_errors) * 1000U) /
                   elapsed_ms);

    rate_last_ms = now_ms;
    rate_valid_frames = g_gsa200.valid_frames;
    rate_checksum_errors = g_gsa200.checksum_errors;
    rate_format_errors = g_gsa200.format_errors;
    rate_sequence_errors = g_gsa200.sequence_errors;
    rate_dropped_frames = g_gsa200.dropped_frames;
    rate_uart_errors = g_gsa200.uart_errors;
}

static uint8_t accept_frame(uint32_t now_ms)
{
    uint8_t counter = frame[30];
    uint8_t delta;
    int16_t raw_temperature;
    uint8_t checksum = 0U;
    uint8_t index;

    for (index = 0U; index < (GSA200_FRAME_SIZE - 1U); ++index)
    {
        checksum = (uint8_t)(checksum + frame[index]);
    }
    if (checksum != frame[GSA200_FRAME_SIZE - 1U])
    {
        ++g_gsa200.checksum_errors;
        return 0U;
    }

    if (have_previous_counter != 0U)
    {
        delta = counter_delta(g_gsa200.sample_counter, counter);
        if (delta == 0U)
        {
            ++g_gsa200.sequence_errors;
        }
        else if (delta > 1U)
        {
            g_gsa200.dropped_frames += (uint32_t)(delta - 1U);
        }
    }

    g_gsa200.gyro_dps[0] = read_float_be(&frame[4]);
    g_gsa200.gyro_dps[1] = read_float_be(&frame[8]);
    g_gsa200.gyro_dps[2] = read_float_be(&frame[12]);
    g_gsa200.accel_g[0] = read_float_be(&frame[16]);
    g_gsa200.accel_g[1] = read_float_be(&frame[20]);
    g_gsa200.accel_g[2] = read_float_be(&frame[24]);
    raw_temperature = (int16_t)(((uint16_t)frame[28] << 8) |
                                 ((uint16_t)frame[29]));
    g_gsa200.temperature_c = (float)raw_temperature * 0.01f;
    g_gsa200.sample_counter = counter;
    memcpy((void *)g_gsa200.last_frame, frame, GSA200_FRAME_SIZE);
    ++g_gsa200.valid_frames;
    g_gsa200.data_valid = 1U;
    g_gsa200.last_valid_ms = now_ms;
    have_previous_counter = 1U;
    mirror_values();
    return 1U;
}

static void process_byte(uint8_t byte, uint32_t now_ms)
{
    if (frame_index == 0U)
    {
        if (byte == GSA200_HEADER_AA)
        {
            frame[frame_index++] = byte;
        }
        return;
    }

    if (frame_index == 1U)
    {
        if (byte == GSA200_HEADER_55)
        {
            frame[frame_index++] = byte;
        }
        else
        {
            frame_index = (byte == GSA200_HEADER_AA) ? 1U : 0U;
            if (frame_index != 0U)
            {
                frame[0] = byte;
            }
        }
        return;
    }

    frame[frame_index++] = byte;
    if (frame_index == 4U)
    {
        if ((frame[2] != GSA200_FORMAT_ID) ||
            (frame[3] != GSA200_FRAME_SIZE))
        {
            ++g_gsa200.format_errors;
            if ((frame[2] == GSA200_HEADER_AA) &&
                (frame[3] == GSA200_HEADER_55))
            {
                frame[0] = frame[2];
                frame[1] = frame[3];
                frame_index = 2U;
            }
            else if (frame[3] == GSA200_HEADER_AA)
            {
                frame[0] = GSA200_HEADER_AA;
                frame_index = 1U;
            }
            else
            {
                frame_index = 0U;
            }
        }
        return;
    }

    if (frame_index == GSA200_FRAME_SIZE)
    {
        if (accept_frame(now_ms) == 0U)
        {
            resync_after_bad_frame();
        }
        else
        {
            frame_index = 0U;
        }
    }
}

void gsa200_init(void)
{
    memset((void *)&g_gsa200, 0, sizeof(g_gsa200));
    memset(frame, 0, sizeof(frame));
    frame_index = 0U;
    have_previous_counter = 0U;
    rate_last_ms = 0U;
    rate_valid_frames = 0U;
    rate_checksum_errors = 0U;
    rate_format_errors = 0U;
    rate_sequence_errors = 0U;
    rate_dropped_frames = 0U;
    rate_uart_errors = 0U;
    gsa200_valid_frames_per_sec = 0U;
    gsa200_checksum_errors_per_sec = 0U;
    gsa200_format_errors_per_sec = 0U;
    gsa200_sequence_errors_per_sec = 0U;
    gsa200_dropped_frames_per_sec = 0U;
    gsa200_uart_errors_per_sec = 0U;
    g_gsa200.active_baudrate = GSA200_BAUDRATE;
    mirror_values();
}

void gsa200_poll(uint32_t now_ms)
{
    uint8_t byte;

    while (uart2_read_byte(&byte) != 0U)
    {
        process_byte(byte, now_ms);
    }

    g_gsa200.uart_errors = uart2_rx_error_count;
    if ((g_gsa200.data_valid != 0U) &&
        ((uint32_t)(now_ms - g_gsa200.last_valid_ms) >=
         GSA200_DATA_TIMEOUT_MS))
    {
        g_gsa200.data_valid = 0U;
    }
    mirror_values();
    update_rates(now_ms);
}
