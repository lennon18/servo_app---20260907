#ifndef PCU_POSITION_H
#define PCU_POSITION_H

#include <stdint.h>

typedef enum
{
    PCU_POSITION_IDLE = 0U,
    PCU_POSITION_MOVING,
    PCU_POSITION_REACHED,
    PCU_POSITION_FAULT,
    PCU_POSITION_TIMEOUT
} pcu_position_state_t;

void pcu_position_init(void);
void pcu_position_poll(uint32_t now_ms);
uint8_t pcu_position_start_relative(float delta_deg);
uint8_t pcu_position_start_absolute(float target_deg);
void pcu_position_stop(void);
void pcu_position_set_direction(int8_t direction);
pcu_position_state_t pcu_position_get_state(void);
uint8_t pcu_position_is_active(void);

extern volatile int32_t pcu_position_current_mdeg;
extern volatile int32_t pcu_position_target_mdeg;
extern volatile int32_t pcu_position_error_mdeg;
extern volatile int32_t pcu_position_command_raw;
extern volatile int32_t pcu_position_direction_sign;

#endif /* PCU_POSITION_H */
