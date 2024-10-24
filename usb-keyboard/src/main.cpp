#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <hid/app/keyboard.hpp>
#include <hid/application.hpp>
#include <hid/report_protocol.hpp>
#include <magic_enum.hpp>
#include <port/zephyr/message_queue.hpp>
#include <port/zephyr/udc_mac.hpp>
#include <usb/df/class/hid.hpp>
#include <usb/df/device.hpp>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const gpio_dt_spec kb_leds[3] = {
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0}),
};

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

static void input_cb(input_event* evt)
{
    kb_event kb_evt{evt->code, evt->value};
    kb_msgq().post(kb_evt);
}

INPUT_CALLBACK_DEFINE(NULL, input_cb);

class demo_keyboard : public hid::application
{
    using keys_report = hid::app::keyboard::keys_input_report<0>;
    using kb_leds_report = hid::app::keyboard::output_report<0>;

  public:
    static constexpr auto report_desc() { return hid::app::keyboard::app_report_descriptor<0>(); }
    static const hid::report_protocol& report_prot()
    {
        static constexpr const auto rd{report_desc()};
        static constexpr const hid::report_protocol rp{rd};
        return rp;
    }

    demo_keyboard(hid::page::keyboard_keypad key, hid::page::leds led)
        : hid::application(report_prot()), key_(key), led_(led), led_idx_(leds_idx++)
    {}
    auto send_key(bool set)
    {
        keys_buffer_.scancodes.set(key_, set);
        return send_report(&keys_buffer_);
    }
    void start(hid::protocol prot) override
    {
        prot_ = prot;
        LOG_INF("HID start with protocol %s", magic_enum::enum_name(prot).data());
        receive_report(&leds_buffer_);
    }
    void stop() override { LOG_INF("HID stop"); }
    void set_report(hid::report::type type, const std::span<const uint8_t>& data) override
    {
        auto* out_report = reinterpret_cast<const kb_leds_report*>(data.data());
        if (kb_leds[led_idx_].port != NULL)
        {
            gpio_pin_set_dt(&kb_leds[led_idx_], out_report->leds.test(led_));
        }
        receive_report(&leds_buffer_);
    }
    void get_report(hid::report::selector select, const std::span<uint8_t>& buffer) override
    {
        send_report(&keys_buffer_);
    }
    void in_report_sent(const std::span<const uint8_t>& data) override {}
    hid::protocol get_protocol() const override { return prot_; }

  private:
    C2USB_USB_TRANSFER_ALIGN(keys_report, keys_buffer_){};
    const hid::page::keyboard_keypad key_{};
    const hid::page::leds led_{};
    C2USB_USB_TRANSFER_ALIGN(kb_leds_report, leds_buffer_){};
    static inline unsigned leds_idx{0};
    const unsigned led_idx_;
    hid::protocol prot_{};
};

auto& caps_kb()
{
    static demo_keyboard caps_lock{hid::page::keyboard_keypad::KEYBOARD_CAPS_LOCK,
                                   hid::page::leds::CAPS_LOCK};
    return caps_lock;
}

static uint8_t serial_number[16]{};
constexpr usb::product_info product_info{0xffff,       "Zephyr", 0xffff, "HID", usb::version("1.0"),
                                         serial_number};

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
    // initializing LEDs
    for (unsigned int i = 0; i < ARRAY_SIZE(kb_leds); i++)
    {
        if (kb_leds[i].port == NULL)
        {
            continue;
        }
        if (!gpio_is_ready_dt(&kb_leds[i]))
        {
            LOG_ERR("LED device %s is not ready", kb_leds[i].port->name);
            return -EIO;
        }
        auto ret = gpio_pin_configure_dt(&kb_leds[i], GPIO_OUTPUT_INACTIVE);
        if (ret != 0)
        {
            LOG_ERR("Failed to configure the LED pin, %d", ret);
            return -EIO;
        }
    }

    // observing device state
    device().set_power_event_delegate(
        [](usb::df::device& dev, usb::df::device::event ev)
        {
            using event = enum usb::df::device::event;
            switch (ev)
            {
            case event::CONFIGURATION_CHANGE:
                printk("USB configured: %u, granted current: %uuA\n", dev.configured(),
                       dev.granted_bus_current_uA());
                break;
            case event::POWER_STATE_CHANGE:
                printk("USB power state: %s, granted current: %uuA\n",
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
