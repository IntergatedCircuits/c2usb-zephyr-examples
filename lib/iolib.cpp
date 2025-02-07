#include <iolib.h>
#include <zephyr/drivers/gpio.h>

extern "C"
{

#define LEDS_NODE DT_PATH(leds)
#define GPIO_SPEC_AND_COMMA(param) GPIO_DT_SPEC_GET(param, gpios),

    static const struct gpio_dt_spec leds[] = {
#if DT_NODE_EXISTS(LEDS_NODE)
        DT_FOREACH_CHILD(LEDS_NODE, GPIO_SPEC_AND_COMMA)
#endif
    };

    struct leds_init
    {
        leds_init()
        {
            int err;
            for (size_t i = 0; i < ARRAY_SIZE(leds); i++)
            {
                err = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT);
                __ASSERT(err == 0, "Cannot configure LED %u gpio", i);
                gpio_pin_set_dt(&leds[i], false);
            }
        }
    };

    int iolib_set_led(uint8_t idx, bool on)
    {
        static leds_init init;
        if (idx >= ARRAY_SIZE(leds))
        {
            return -EINVAL;
        }
        return gpio_pin_set_dt(&leds[idx], on);
    }
}