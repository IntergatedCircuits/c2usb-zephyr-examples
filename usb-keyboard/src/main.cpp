#include <iolib.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include <demo_keyboard.hpp>
#include <port/zephyr/message_queue.hpp>
#include <port/zephyr/udc_mac.hpp>
#include <usb/df/class/hid.hpp>
#include <usb/df/device.hpp>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

struct kb_event
{
    uint16_t code;
    int32_t value;
};

auto& kb_msgq()
{
    static os::zephyr::message_queue<kb_event, 2> msgq;
    return msgq;
}

static void input_cb(input_event* evt, void*)
{
    kb_event kb_evt{evt->code, evt->value};
    kb_msgq().post(kb_evt);
}

INPUT_CALLBACK_DEFINE(nullptr, input_cb, nullptr);

auto& caps_kb()
{
    static demo_keyboard caps_lock{hid::page::keyboard_keypad::KEYBOARD_CAPS_LOCK,
                                   [](const demo_keyboard::kb_leds_report& report)
                                   {
                                       iolib_set_led(0,
                                                     report.leds.test(hid::page::leds::CAPS_LOCK));
                                   }};
    return caps_lock;
}

static uint8_t serial_number[16]{};
constexpr usb::product_info product_info{CONFIG_DEMO_MANUFACTURER_ID, CONFIG_DEMO_MANUFACTURER,
                                         CONFIG_DEMO_PRODUCT_ID,      CONFIG_DEMO_PRODUCT,
                                         usb::version("1.0"),         serial_number};

auto& mac()
{
    static usb::zephyr::udc_mac mac{DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0))};
    return mac;
}

auto& device()
{
    static usb::df::device_instance<usb::speed::FULL> device{mac(), product_info};
    return device;
}

//[[noreturn]]
int main(void)
{
    // observing device state
    device().set_power_event_delegate(
        [](usb::df::device& dev, usb::df::device::event ev)
        {
            using event = enum usb::df::device::event;
            switch (ev)
            {
            case event::CONFIGURATION_CHANGE:
                LOG_INF("USB configured: %u, granted current: %uuA\n", dev.configured(),
                        dev.granted_bus_current_uA());
                break;
            case event::POWER_STATE_CHANGE:
                LOG_INF("USB power state: %s, granted current: %uuA\n",
                        magic_enum::enum_name(dev.power_state()).data(),
                        dev.granted_bus_current_uA());
                switch (dev.power_state())
                {
                case usb::power::state::L2_SUSPEND:
                    break;
                case usb::power::state::L0_ON:
                    break;
                default:
                    break;
                }
                break;
            }
        });

    // use HW info as serial number
    if (IS_ENABLED(CONFIG_HWINFO))
    {
        hwinfo_get_device_id(serial_number, sizeof(serial_number));
    }
    // define configuration and start device
    const auto usb_init = []()
    {
        constexpr auto speed = usb::speed::FULL;
        constexpr auto config_header =
            usb::df::config::header(usb::df::config::power::bus(500, true), "base config");

        static usb::df::hid::function usb_kb{caps_kb(), "keyboard",
                                             usb::hid::boot_protocol_mode::KEYBOARD};

        static const auto base_config = usb::df::config::make_config(
            config_header, usb::df::hid::config(usb_kb, speed, usb::endpoint::address(0x81), 1));
        device().set_config(base_config);
        device().open();
    };
    mac().queue_task(usb_init);

    while (true)
    {
        auto msg = kb_msgq().get();
        if ((msg.value) && device().power_state() == usb::power::state::L2_SUSPEND)
        {
            mac().queue_task([]() { device().remote_wakeup(); });
        }
        switch (msg.code)
        {
        case INPUT_KEY_0:
            caps_kb().send_key(msg.value);
            break;
        default:
            break;
        }
    }
}
