//==============================================================================
// Includes
//==============================================================================

#include <stdint.h>

#include "gpio_types.h"

#include "i2c-conf.h"

#include "emu_esp.h"

//==============================================================================
// Functions
//==============================================================================

/**
 * @brief Do Nothing
 *
 * @param sda unused
 * @param scl unused
 * @param pullup unused
 * @param clkHz unused
 */
void i2c_master_init(gpio_num_t sda UNUSED, gpio_num_t scl UNUSED,
    gpio_pullup_t pullup UNUSED, uint32_t clkHz UNUSED)
{
    ; // Nothing to do
}
