#include "pcu_position.h"

#include <math.h>

#include "pcu.h"

/* Position-loop values for the J55LWX002A motor. The PCU current command is
   signed and normalized to the configured 3 A peak current. The controller
   deliberately limits its current demand below the motor's peak rating. */
#define POSITION_LOOP_PERIOD_MS       (5U)
#define POSITION_FEEDBACK_TIMEOUT_MS  (5000U)
#define POSITION_MOVE_TIMEOUT_MS      (10000U)
#define POSITION_SETTLE_TIME_MS       (100U)
#define POSITION_TOLERANCE_DEG        (3.0f)
#define POSITION_KP_RAW_PER_DEG       (100.0f)
#define POSITION_KD_RAW_PER_DPS       (25.0f)
#define POSITION_MIN_CURRENT_RAW      (2200)
#define POSITION_START_CURRENT_RAW    (2500)
#define POSITION_START_CURRENT_ERROR_DEG (8.0f)
#define POSITION_HOLD_CURRENT_RAW     (1000)
#define POSITION_MIN_CURRENT_ERROR_DEG (3.0f)
#define POSITION_MAX_CURRENT_RAW      (5000)
#define POSITION_COMMAND_SLEW_RAW     (500)
#define POSITION_DEFAULT_DIRECTION    (1)

#define PCU_POSITION_FAULT_MASK PCU_STATUS_FAULT_MASK

volatile int32_t pcu_position_current_mdeg;
volatile int32_t pcu_position_target_mdeg;
volatile int32_t pcu_position_error_mdeg;
volatile int32_t pcu_position_command_raw;
volatile int32_t pcu_position_direction_sign;

static pcu_position_state_t position_state;
static float current_angle_deg;
static float target_angle_deg;
static float previous_wrapped_angle_deg;
static uint8_t feedback_initialized;
static uint32_t last_feedback_frames;
static uint32_t last_feedback_ms;
static uint32_t move_start_ms;
static uint32_t settle_start_ms;
static uint32_t last_command_ms;
static uint8_t feedback_seen_during_move;
static float control_previous_angle_deg;
static float control_velocity_dps;
static uint32_t control_previous_ms;

static int32_t position_slew_command(int32_t requested_raw)
{
    int32_t command_raw = pcu_position_command_raw;

    if (requested_raw > command_raw)
    {
        int32_t step = requested_raw - command_raw;
        if (step > POSITION_COMMAND_SLEW_RAW)
        {
            step = POSITION_COMMAND_SLEW_RAW;
        }
        command_raw += step;
    }
    else if (requested_raw < command_raw)
    {
        int32_t step = command_raw - requested_raw;
        if (step > POSITION_COMMAND_SLEW_RAW)
        {
            step = POSITION_COMMAND_SLEW_RAW;
        }
        command_raw -= step;
    }

    return command_raw;
}

static int32_t position_mdeg(float angle_deg)
{
    float scaled = angle_deg * 1000.0f;

    return (int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static void position_publish(float error_deg)
{
    pcu_position_current_mdeg = position_mdeg(current_angle_deg);
    pcu_position_target_mdeg = position_mdeg(target_angle_deg);
    pcu_position_error_mdeg = position_mdeg(error_deg);
}

static void position_disable(void)
{
    (void)pcu_disable_output();
}

static void position_fail(pcu_position_state_t state)
{
    pcu_position_command_raw = 0;
    control_velocity_dps = 0.0f;
    position_disable();
    position_state = state;
    settle_start_ms = 0U;
}

static void position_update_feedback(uint32_t now_ms)
{
    float wrapped_angle = g_pcu_status.angle_deg;
    float delta_deg;

    if (g_pcu_status.valid_frames == last_feedback_frames)
    {
        return;
    }

    last_feedback_frames = g_pcu_status.valid_frames;
    last_feedback_ms = now_ms;
    feedback_seen_during_move = 1U;

    if (feedback_initialized == 0U)
    {
        previous_wrapped_angle_deg = wrapped_angle;
        current_angle_deg = wrapped_angle;
        feedback_initialized = 1U;
        position_publish(0.0f);
        return;
    }

    delta_deg = wrapped_angle - previous_wrapped_angle_deg;
    if (delta_deg > 180.0f)
    {
        delta_deg -= 360.0f;
    }
    else if (delta_deg < -180.0f)
    {
        delta_deg += 360.0f;
    }

    current_angle_deg += delta_deg;
    previous_wrapped_angle_deg = wrapped_angle;
}

static uint8_t position_start_allowed(void)
{
    return (uint8_t)(g_pcu_status.data_valid != 0U &&
                     ((g_pcu_status.status & PCU_POSITION_FAULT_MASK) == 0U) &&
                     feedback_initialized != 0U &&
                     position_state != PCU_POSITION_MOVING);
}

static void position_begin(float target_deg)
{
    current_angle_deg = g_pcu_status.angle_deg;
    previous_wrapped_angle_deg = g_pcu_status.angle_deg;
    target_angle_deg = target_deg;
    position_state = PCU_POSITION_MOVING;
    move_start_ms = 0U;
    last_feedback_ms = 0U;
    feedback_seen_during_move = 0U;
    pcu_position_command_raw = 0;
    control_previous_angle_deg = current_angle_deg;
    control_velocity_dps = 0.0f;
    control_previous_ms = 0U;
    settle_start_ms = 0U;
    last_command_ms = 0U;
    position_publish(target_angle_deg - current_angle_deg);
}

void pcu_position_init(void)
{
    position_state = PCU_POSITION_IDLE;
    current_angle_deg = 0.0f;
    target_angle_deg = 0.0f;
    previous_wrapped_angle_deg = 0.0f;
    feedback_initialized = 0U;
    last_feedback_frames = 0U;
    last_feedback_ms = 0U;
    move_start_ms = 0U;
    settle_start_ms = 0U;
    last_command_ms = 0U;
    feedback_seen_during_move = 0U;
    control_previous_angle_deg = 0.0f;
    control_velocity_dps = 0.0f;
    control_previous_ms = 0U;
    pcu_position_current_mdeg = 0;
    pcu_position_target_mdeg = 0;
    pcu_position_error_mdeg = 0;
    pcu_position_command_raw = 0;
    pcu_position_direction_sign = POSITION_DEFAULT_DIRECTION;
}

uint8_t pcu_position_start_relative(float delta_deg)
{
    if (!isfinite(delta_deg) || (delta_deg < -3600.0f) ||
        (delta_deg > 3600.0f) || (position_start_allowed() == 0U))
    {
        return 0U;
    }

    /* Manual current/voltage commands may move the shaft while the position
       loop is idle. Re-anchor the relative move to the latest encoder frame
       instead of using a stale unwrapped value from an earlier move. */
    position_begin(g_pcu_status.angle_deg + delta_deg);
    return 1U;
}

uint8_t pcu_position_start_absolute(float target_deg)
{
    float delta_deg;

    if (!isfinite(target_deg) || (target_deg < 0.0f) ||
        (target_deg > 360.0f) || (position_start_allowed() == 0U))
    {
        return 0U;
    }

    delta_deg = target_deg - g_pcu_status.angle_deg;
    if (delta_deg > 180.0f)
    {
        delta_deg -= 360.0f;
    }
    else if (delta_deg < -180.0f)
    {
        delta_deg += 360.0f;
    }

    /* Absolute targets use the shortest path on the 0..360 degree encoder
       circle; relative move remains available for multi-turn motion. */
    position_begin(g_pcu_status.angle_deg + delta_deg);
    return 1U;
}

void pcu_position_stop(void)
{
    if (position_state == PCU_POSITION_MOVING)
    {
        position_disable();
    }
    pcu_position_command_raw = 0;
    position_state = PCU_POSITION_IDLE;
    settle_start_ms = 0U;
}

void pcu_position_poll(uint32_t now_ms)
{
    float error_deg;
    float command_f;
    int32_t command_raw;

    position_update_feedback(now_ms);
    if (position_state != PCU_POSITION_MOVING)
    {
        return;
    }

    if (move_start_ms == 0U)
    {
        move_start_ms = now_ms;
    }

    if ((g_pcu_status.status & PCU_POSITION_FAULT_MASK) != 0U)
    {
        position_fail(PCU_POSITION_FAULT);
        return;
    }
    if ((!feedback_seen_during_move &&
         ((uint32_t)(now_ms - move_start_ms) > POSITION_FEEDBACK_TIMEOUT_MS)) ||
        (feedback_seen_during_move &&
         ((uint32_t)(now_ms - last_feedback_ms) > POSITION_FEEDBACK_TIMEOUT_MS)))
    {
        position_fail(PCU_POSITION_TIMEOUT);
        return;
    }
    if ((uint32_t)(now_ms - move_start_ms) > POSITION_MOVE_TIMEOUT_MS)
    {
        position_fail(PCU_POSITION_TIMEOUT);
        return;
    }
    if ((uint32_t)(now_ms - last_command_ms) < POSITION_LOOP_PERIOD_MS)
    {
        return;
    }
    last_command_ms = now_ms;

    error_deg = target_angle_deg - current_angle_deg;
    position_publish(error_deg);

    if (control_previous_ms != 0U)
    {
        float dt_s = (float)(now_ms - control_previous_ms) * 0.001f;
        if ((dt_s > 0.0f) && (dt_s < 0.1f))
        {
            float measured_velocity =
                (current_angle_deg - control_previous_angle_deg) / dt_s;
            /* A small low-pass filter prevents encoder quantisation from
               creating a noisy braking current. */
            control_velocity_dps = (control_velocity_dps * 0.7f) +
                                   (measured_velocity * 0.3f);
        }
    }
    control_previous_angle_deg = current_angle_deg;
    control_previous_ms = now_ms;

    if (fabsf(error_deg) <= POSITION_TOLERANCE_DEG)
    {
        /* Keep a small static current so the external load cannot pull the
           shaft away immediately after reaching the target. */
        int32_t hold_command = POSITION_HOLD_CURRENT_RAW *
                               (int32_t)pcu_position_direction_sign;
        if (pcu_position_command_raw != hold_command)
        {
            pcu_position_command_raw = hold_command;
            if (pcu_send_command(PCU_OUTPUT_ENABLED,
                                 PCU_CONTROL_CURRENT_CLOSED_LOOP,
                                 (int16_t)hold_command) == 0U)
            {
                position_fail(PCU_POSITION_FAULT);
                return;
            }
        }
        if (settle_start_ms == 0U)
        {
            settle_start_ms = now_ms;
        }
        if ((uint32_t)(now_ms - settle_start_ms) >= POSITION_SETTLE_TIME_MS)
        {
            /* Keep the low holding current while the target is occupied.
               An explicit "position stop" command is required to release
               the shaft and disable the drive output. */
            position_state = PCU_POSITION_REACHED;
        }
        return;
    }

    settle_start_ms = 0U;

    command_f = (error_deg * POSITION_KP_RAW_PER_DEG -
                 control_velocity_dps * POSITION_KD_RAW_PER_DPS) *
                (float)pcu_position_direction_sign;
    if ((fabsf(error_deg) > POSITION_MIN_CURRENT_ERROR_DEG) &&
        (fabsf(command_f) < ((fabsf(error_deg) >
                               POSITION_START_CURRENT_ERROR_DEG) ?
                              (float)POSITION_START_CURRENT_RAW :
                              (float)POSITION_MIN_CURRENT_RAW)))
    {
        float minimum_current = (fabsf(error_deg) >
                                 POSITION_START_CURRENT_ERROR_DEG) ?
                                (float)POSITION_START_CURRENT_RAW :
                                (float)POSITION_MIN_CURRENT_RAW;
        command_f = (command_f < 0.0f) ?
                     -minimum_current : minimum_current;
    }
    if (command_f > (float)POSITION_MAX_CURRENT_RAW)
    {
        command_f = (float)POSITION_MAX_CURRENT_RAW;
    }
    else if (command_f < (float)-POSITION_MAX_CURRENT_RAW)
    {
        command_f = (float)-POSITION_MAX_CURRENT_RAW;
    }

    command_raw = (int32_t)(command_f >= 0.0f ? command_f + 0.5f : command_f - 0.5f);
    command_raw = position_slew_command(command_raw);
    pcu_position_command_raw = command_raw;

    if (pcu_send_command(PCU_OUTPUT_ENABLED,
                         PCU_CONTROL_CURRENT_CLOSED_LOOP,
                         (int16_t)command_raw) == 0U)
    {
        position_fail(PCU_POSITION_FAULT);
    }
}

void pcu_position_set_direction(int8_t direction)
{
    if (direction > 0)
    {
        pcu_position_direction_sign = 1;
    }
    else if (direction < 0)
    {
        pcu_position_direction_sign = -1;
    }
}

pcu_position_state_t pcu_position_get_state(void)
{
    return position_state;
}

uint8_t pcu_position_is_active(void)
{
    return (uint8_t)(position_state == PCU_POSITION_MOVING);
}
