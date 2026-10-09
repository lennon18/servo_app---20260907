#include "pcu_terminal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_uart.h"
#include "gsa200.h"
#include "pcu.h"
#include "pcu_position.h"
#include "stm32h7xx_ll_cortex.h"

#define TERMINAL_LINE_SIZE (64U)
#define PCU_FAULT_MASK PCU_STATUS_FAULT_MASK

volatile uint32_t pcu_terminal_command_count;
volatile uint32_t pcu_terminal_error_count;
volatile int32_t pcu_terminal_setpoint;
volatile uint32_t pcu_terminal_mode;
volatile uint32_t pcu_terminal_output_requested;

static char terminal_line[TERMINAL_LINE_SIZE];
static uint8_t terminal_line_length;

static void terminal_prompt(void)
{
    printf("pcu> ");
}

static void terminal_help(void)
{
    printf("\r\nPCU commands (raw setpoint -32767..32767):\r\n");
    printf("  help                 show this list\r\n");
    printf("  status               show PCU feedback and staged command\r\n");
    printf("  set voltage <raw>    stage voltage open-loop command\r\n");
    printf("  set current <raw>    stage current closed-loop command\r\n");
    printf("  enable               send the staged command and enable output\r\n");
    printf("  disable | stop       immediately disable PCU output\r\n");
    printf("  clear                clear PCU faults with output disabled\r\n");
    printf("  zero <raw>           start PCU automatic zeroing\r\n");
    printf("  move <deg>           move relative to current angle\r\n");
    printf("  goto <deg>           move to absolute PCU angle 0..360\r\n");
    printf("  position status      show position-loop state\r\n");
    printf("  position direction <1|-1>  set position-loop current direction\r\n");
    printf("  position stop        stop position move and disable output\r\n");
    printf("  imu status           show GSA200 live data and link state\r\n");
}

static uint32_t terminal_angle_mdeg(void)
{
    return (uint32_t)(((uint64_t)g_pcu_status.angle_raw * 360000ULL) /
                      16777216ULL);
}

static void terminal_status(void)
{
    uint32_t status = g_pcu_status.status;

    printf("\r\nPCU valid=%u frames=%lu angle=%lu.%03lu deg status=0x%02lX\r\n",
           (unsigned int)g_pcu_status.data_valid,
           (unsigned long)g_pcu_status.valid_frames,
           (unsigned long)(terminal_angle_mdeg() / 1000U),
           (unsigned long)(terminal_angle_mdeg() % 1000U),
           (unsigned long)status);
    printf("faults: encoder=%u overcurrent_short=%u overtemp=%u motor_overload=%u undervolt=%u overvolt=%u enabled=%u requested=%lu\r\n",
           (unsigned int)((status & PCU_STATUS_ENCODER_ALARM) != 0U),
           (unsigned int)((status & PCU_STATUS_OVERCURRENT_SHORT) != 0U),
           (unsigned int)((status & PCU_STATUS_OVERTEMPERATURE) != 0U),
           (unsigned int)((status & PCU_STATUS_MOTOR_OVERLOAD) != 0U),
           (unsigned int)((status & PCU_STATUS_BUS_UNDERVOLT) != 0U),
           (unsigned int)((status & PCU_STATUS_BUS_OVERVOLT) != 0U),
           (unsigned int)((status & PCU_STATUS_OUTPUT_ENABLED) != 0U),
           (unsigned long)pcu_terminal_output_requested);
    printf("rx errors: checksum=%lu format=%lu; staged mode=%lu setpoint=%ld requested=%lu\r\n",
           (unsigned long)g_pcu_status.checksum_errors,
           (unsigned long)g_pcu_status.format_errors,
           (unsigned long)pcu_terminal_mode,
           (long)pcu_terminal_setpoint,
           (unsigned long)pcu_terminal_output_requested);
    printf("position: state=%u current_mdeg=%ld target_mdeg=%ld error_mdeg=%ld command_raw=%ld direction=%ld\r\n",
           (unsigned int)pcu_position_get_state(),
           (long)pcu_position_current_mdeg,
           (long)pcu_position_target_mdeg,
           (long)pcu_position_error_mdeg,
           (long)pcu_position_command_raw,
           (long)pcu_position_direction_sign);
    printf("tx: sent=%lu failed=%lu frame=%02X %02X %02X %02X %02X %02X %02X\r\n",
           (unsigned long)pcu_command_frames_sent,
           (unsigned long)pcu_command_send_failures,
           (unsigned int)pcu_last_command_frame[0],
           (unsigned int)pcu_last_command_frame[1],
           (unsigned int)pcu_last_command_frame[2],
           (unsigned int)pcu_last_command_frame[3],
           (unsigned int)pcu_last_command_frame[4],
           (unsigned int)pcu_last_command_frame[5],
           (unsigned int)pcu_last_command_frame[6]);
}

static void terminal_imu_status(void)
{
    long gyro_x_milli = (long)(gsa200_gyro_x_dps * 1000.0f);
    long gyro_y_milli = (long)(gsa200_gyro_y_dps * 1000.0f);
    long gyro_z_milli = (long)(gsa200_gyro_z_dps * 1000.0f);
    long angle_x_milli = (long)(gsa200_angle_x_deg * 1000.0f);
    long angle_y_milli = (long)(gsa200_angle_y_deg * 1000.0f);
    long angle_z_milli = (long)(gsa200_angle_z_deg * 1000.0f);
    long accel_x_milli = (long)(gsa200_accel_x_g * 1000.0f);
    long accel_y_milli = (long)(gsa200_accel_y_g * 1000.0f);
    long accel_z_milli = (long)(gsa200_accel_z_g * 1000.0f);
    long temperature_milli = (long)(gsa200_temperature_c * 1000.0f);

    printf("\r\nGSA200 valid=%u baud=%lu frames=%lu sample=%lu\r\n",
           (unsigned int)gsa200_data_valid,
           (unsigned long)gsa200_active_baudrate,
           (unsigned long)gsa200_valid_frames,
           (unsigned long)gsa200_sample_counter);
    printf("gyro_milli_dps=(%ld, %ld, %ld) angle_milli_deg=(%ld, %ld, %ld)\r\n",
           gyro_x_milli, gyro_y_milli, gyro_z_milli, angle_x_milli,
           angle_y_milli, angle_z_milli);
    printf("accel_milli_g=(%ld, %ld, %ld) temperature_milli_c=%ld errors=(%lu,%lu) dropped=%lu\r\n",
           accel_x_milli, accel_y_milli, accel_z_milli, temperature_milli,
           (unsigned long)gsa200_checksum_errors,
           (unsigned long)gsa200_format_errors,
           (unsigned long)gsa200_dropped_frames);
}

static uint8_t terminal_parse_setpoint(const char *text, int16_t *value)
{
    char *end;
    long parsed;

    while (*text == ' ')
    {
        ++text;
    }
    if (*text == '\0')
    {
        return 0U;
    }

    parsed = strtol(text, &end, 10);
    while (*end == ' ')
    {
        ++end;
    }
    if ((*end != '\0') || (parsed < PCU_SETPOINT_MIN) ||
        (parsed > PCU_SETPOINT_MAX))
    {
        return 0U;
    }

    *value = (int16_t)parsed;
    return 1U;
}

static uint8_t terminal_parse_angle(const char *text, float *value)
{
    char *end;
    float parsed;

    while (*text == ' ')
    {
        ++text;
    }
    if (*text == '\0')
    {
        return 0U;
    }

    parsed = strtof(text, &end);
    while (*end == ' ')
    {
        ++end;
    }
    if ((*end != '\0') || !isfinite(parsed) || (parsed < -3600.0f) ||
        (parsed > 3600.0f))
    {
        return 0U;
    }

    *value = parsed;
    return 1U;
}

static uint8_t terminal_drive_ready(void)
{
    if (g_pcu_status.data_valid == 0U)
    {
        printf("ERR: no valid PCU feedback; output remains disabled\r\n");
        return 0U;
    }
    if ((g_pcu_status.status & PCU_FAULT_MASK) != 0U)
    {
        printf("ERR: PCU fault status=0x%02X; output remains disabled\r\n",
               (unsigned int)g_pcu_status.status);
        return 0U;
    }
    return 1U;
}

static void terminal_execute(char *line)
{
    int16_t value;
    float angle;
    uint8_t sent = 0U;
    char *cursor;

    for (cursor = line; *cursor != '\0'; ++cursor)
    {
        *cursor = (char)tolower((unsigned char)*cursor);
    }

    if ((strcmp(line, "help") == 0) || (strcmp(line, "?") == 0))
    {
        terminal_help();
    }
    else if (strcmp(line, "status") == 0)
    {
        terminal_status();
    }
    else if (strcmp(line, "position status") == 0)
    {
        terminal_status();
    }
    else if (strncmp(line, "position direction ", 19U) == 0)
    {
        long direction = strtol(&line[19], &cursor, 10);
        while (*cursor == ' ')
        {
            ++cursor;
        }
        if (*cursor != '\0' || (direction != 1L && direction != -1L))
        {
            printf("ERR: direction must be 1 or -1\r\n");
        }
        else
        {
            pcu_position_stop();
            pcu_position_set_direction((int8_t)direction);
            printf("OK: position direction=%ld\r\n", direction);
        }
    }
    else if ((strcmp(line, "imu status") == 0) ||
             (strcmp(line, "gsa status") == 0))
    {
        terminal_imu_status();
    }
    else if (strcmp(line, "position stop") == 0)
    {
        pcu_position_stop();
        pcu_terminal_output_requested = 0U;
        printf("OK: position move stopped\r\n");
    }
    else if ((strcmp(line, "disable") == 0) || (strcmp(line, "stop") == 0))
    {
        pcu_position_stop();
        sent = pcu_disable_output();
        pcu_terminal_output_requested = 0U;
        printf(sent != 0U ? "OK: output disabled\r\n" : "ERR: disable send failed\r\n");
    }
    else if (strcmp(line, "clear") == 0)
    {
        pcu_position_stop();
        (void)pcu_disable_output();
        sent = pcu_clear_fault();
        pcu_terminal_output_requested = 0U;
        printf(sent != 0U ? "OK: fault-clear sent\r\n" : "ERR: fault-clear send failed\r\n");
    }
    else if (strncmp(line, "set voltage ", 12U) == 0)
    {
        if (terminal_parse_setpoint(&line[12], &value) != 0U)
        {
            pcu_position_stop();
            pcu_terminal_mode = PCU_CONTROL_VOLTAGE_OPEN_LOOP;
            pcu_terminal_setpoint = value;
            printf("OK: staged voltage raw=%d; type enable to run\r\n", (int)value);
        }
        else
        {
            printf("ERR: raw value must be -32767..32767\r\n");
        }
    }
    else if (strncmp(line, "set current ", 12U) == 0)
    {
        if (terminal_parse_setpoint(&line[12], &value) != 0U)
        {
            pcu_position_stop();
            pcu_terminal_mode = PCU_CONTROL_CURRENT_CLOSED_LOOP;
            pcu_terminal_setpoint = value;
            printf("OK: staged current raw=%d; type enable to run\r\n", (int)value);
        }
        else
        {
            printf("ERR: raw value must be -32767..32767\r\n");
        }
    }
    else if (strcmp(line, "enable") == 0)
    {
        if (pcu_position_is_active() != 0U)
        {
            printf("ERR: position move is active; stop it before manual enable\r\n");
        }
        else if (terminal_drive_ready() != 0U)
        {
            sent = pcu_send_command(PCU_OUTPUT_ENABLED,
                                    (pcu_control_mode_t)pcu_terminal_mode,
                                    (int16_t)pcu_terminal_setpoint);
            pcu_terminal_output_requested = sent;
            printf(sent != 0U ? "OK: output command sent\r\n" : "ERR: enable send failed\r\n");
        }
    }
    else if (strncmp(line, "zero ", 5U) == 0)
    {
        if (terminal_parse_setpoint(&line[5], &value) == 0U)
        {
            printf("ERR: raw value must be -32767..32767\r\n");
        }
        else if (terminal_drive_ready() != 0U)
        {
            pcu_position_stop();
            (void)pcu_disable_output();
            sent = pcu_auto_zero(value);
            pcu_terminal_output_requested = sent;
            printf(sent != 0U ? "OK: auto-zero command sent\r\n" : "ERR: auto-zero send failed\r\n");
        }
    }
    else if (strncmp(line, "move ", 5U) == 0)
    {
        if (terminal_parse_angle(&line[5], &angle) == 0U)
        {
            printf("ERR: move angle must be -3600..3600 degrees\r\n");
        }
        else if (pcu_position_start_relative(angle) == 0U)
        {
            printf("ERR: position move rejected; check PCU feedback, faults, or active move\r\n");
        }
        else
        {
            printf("OK: relative position move started\r\n");
        }
    }
    else if (strncmp(line, "goto ", 5U) == 0)
    {
        if (terminal_parse_angle(&line[5], &angle) == 0U ||
            angle < 0.0f || angle > 360.0f)
        {
            printf("ERR: absolute angle must be 0..360 degrees\r\n");
        }
        else if (pcu_position_start_absolute(angle) == 0U)
        {
            printf("ERR: absolute position move rejected; check PCU feedback, faults, or active move\r\n");
        }
        else
        {
            printf("OK: absolute position move started\r\n");
        }
    }
    else if (line[0] != '\0')
    {
        printf("ERR: unknown command; type help\r\n");
        pcu_terminal_error_count++;
    }

    pcu_terminal_command_count++;
}

static void terminal_process_byte(uint8_t byte)
{
    if ((byte == '\r') || (byte == '\n'))
    {
        if (terminal_line_length != 0U)
        {
            terminal_line[terminal_line_length] = '\0';
            terminal_execute(terminal_line);
            terminal_line_length = 0U;
            terminal_prompt();
        }
        return;
    }

    if ((byte == 0x08U) || (byte == 0x7FU))
    {
        if (terminal_line_length != 0U)
        {
            terminal_line_length--;
        }
        return;
    }

    if (isprint((int)byte) == 0)
    {
        return;
    }
    if (terminal_line_length >= (TERMINAL_LINE_SIZE - 1U))
    {
        terminal_line_length = 0U;
        pcu_terminal_error_count++;
        printf("\r\nERR: command too long\r\n");
        terminal_prompt();
        return;
    }
    terminal_line[terminal_line_length++] = (char)byte;
}

void pcu_terminal_init(void)
{
    memset(terminal_line, 0, sizeof(terminal_line));
    terminal_line_length = 0U;
    pcu_terminal_command_count = 0U;
    pcu_terminal_error_count = 0U;
    pcu_terminal_setpoint = 0;
    pcu_terminal_mode = PCU_CONTROL_CURRENT_CLOSED_LOOP;
    pcu_terminal_output_requested = 0U;

    printf("\r\nPCU terminal ready on J7/USART3 at 921600 8N1. Type help.\r\n");
    terminal_prompt();
}

void pcu_terminal_poll(void)
{
    uint8_t local_buffer[UART_RX_PACKET_MAX_LEN];
    uint8_t length;
    uint8_t index;

    if (uart3_rx.interrupt_flag == 0U)
    {
        return;
    }

    NVIC_DisableIRQ(USART3_IRQn);
    length = uart3_rx.buffer_length;
    memcpy(local_buffer, (const void *)uart3_rx.buffer, length);
    uart3_rx.interrupt_flag = 0U;
    NVIC_EnableIRQ(USART3_IRQn);

    for (index = 0U; index < length; ++index)
    {
        terminal_process_byte(local_buffer[index]);
    }
}
