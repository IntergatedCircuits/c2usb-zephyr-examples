#include "iolib.h"
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include <high_resolution_mouse.hpp>
#include <magic_enum.hpp>
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
    static os::zephyr::message_queue_instance<kb_event, 2> msgq;
    return msgq;
}

static void input_cb(input_event* evt, void*)
{
    kb_event kb_evt{evt->code, evt->value};
    kb_msgq().post(kb_evt);
}

INPUT_CALLBACK_DEFINE(nullptr, input_cb, nullptr);

auto& mouse()
{
    static high_resolution_mouse<> m(
        [](const high_resolution_mouse<>::resolution_multiplier_report& report)
        {
            iolib_set_led(0, report.resolutions != 0);
            LOG_INF("multiplier report: %x", report.resolutions);
        });
    return m;
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
            using namespace std::literals;
            static high_resolution_mouse<>::resolution_multiplier_report report_backup{};
            static os::zephyr::tick_timer::time_point last_reset_time{};

            /* Linux hosts produce erroneous behavior when waking up (on a subset of USB ports):
             * 1. L2 -> L0
             * 2. USB reset
             * 3. L0 -> L2
             * 4. L2 -> L0
             * 5. USB re-enumeration, this time without negotiating high-resolution scrolling
             */
            if (ev == event::CONFIGURATION_CHANGE)
            {
                LOG_INF("USB configured: %u, granted current: %uuA", (unsigned)dev.configured(),
                        dev.granted_bus_current_uA());
                if (!dev.configured())
                {
                    last_reset_time = os::zephyr::tick_timer::now();
                }
            }
            else
            {
                LOG_INF("USB power state: %s, granted current: %uuA",
                        magic_enum::enum_name(dev.power_state()).data(),
                        dev.granted_bus_current_uA());
                if (dev.power_state() == usb::power::state::L2_SUSPEND)
                {
                    if (dev.configured())
                    {
                        report_backup = mouse().multiplier_report();
                    }
                    else if (std::chrono::duration_cast<std::chrono::milliseconds>(
                                 os::zephyr::tick_timer::now() - last_reset_time) < 20ms)
                    {
                        // reset happened recently, restore the last known multiplier
                        mouse().set_report(
                            high_resolution_mouse<>::resolution_multiplier_report::type(),
                            std::span<const uint8_t>(
                                const_cast<const uint8_t*>(report_backup.data()),
                                sizeof(report_backup)));
                        LOG_INF("restored multiplier: %x", report_backup.resolutions);
                    }
                }
            }
        });

    // use HW info as serial number
    if (IS_ENABLED(CONFIG_HWINFO))
    {
        hwinfo_get_device_id(serial_number, sizeof(serial_number));
    }
    // define configuration and start device
    {
        constexpr auto speed = usb::speed::FULL;
        constexpr auto config_header =
            usb::df::config::header(usb::df::config::power::bus(500, true), "base config");

        static usb::df::hid::function usb_mouse{mouse(), "mouse",
                                                usb::hid::boot_protocol_mode::NONE};

        static const auto base_config = usb::df::config::make_config(
            config_header, usb::df::hid::config(usb_mouse, speed, usb::endpoint::address(0x81), 1));
        device().set_config(base_config);
        device().open();
    }

    // button 3 is the left mouse button
    // buttons 1 and 2 are either scrolling, or moving the cursor horizontally - depending on button
    // 4
    high_resolution_mouse<>::mouse_report report{};
    bool horizontal = false;
    while (true)
    {
        using namespace std::chrono_literals;
        auto wait_time = 100ms;
        if (horizontal or mouse().multiplier_report().high_resolution())
        {
            wait_time = 10ms;
        }
        auto msg = kb_msgq().try_get_for(wait_time);
        if (device().power_state() == usb::power::state::L2_SUSPEND)
        {
            if (msg && (msg->value))
            {
                device().remote_wakeup();
            }
            continue;
        }
        if (!msg)
        {
            if (!report.steady())
            {
                mouse().send(report);
            }
            continue;
        }
        switch (msg->code)
        {
        case INPUT_KEY_0:
        case INPUT_KEY_1:
            if (msg->value)
            {
                if (horizontal)
                {
                    report.x = 1 * (msg->code == INPUT_KEY_0 ? -1 : 1);
                }
                else
                {
                    report.wheel_y = 1 * (msg->code == INPUT_KEY_0 ? -1 : 1);
                }
            }
            else
            {
                report.x = 0;
                report.wheel_y = 0;
            }
            mouse().send(report);
            break;
        case INPUT_KEY_2:
            report.buttons.set(hid::page::button(1), msg->value);
            mouse().send(report);
            break;
        case INPUT_KEY_3:
            horizontal = msg->value;
            break;
        default:
            break;
        }
    }
}
