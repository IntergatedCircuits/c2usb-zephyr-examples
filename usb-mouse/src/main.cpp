#include "iolib.h"
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include <hid/app/mouse.hpp>
#include <hid/application.hpp>
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

class demo_mouse : public hid::application
{
    static constexpr auto LAST_BUTTON = hid::page::button(3);
    static constexpr int16_t AXIS_LIMIT = 127;
    static constexpr int16_t MAX_SCROLL_RESOLUTION = 120;
    static constexpr int16_t WHEEL_LIMIT = 32767;

    // Linux only works if a report ID is used
    static constexpr uint8_t MOUSE_REPORT_ID = 1;

  public:
    template <uint8_t REPORT_ID = 0>
    struct mouse_report_base : public hid::report::base<hid::report::type::INPUT, REPORT_ID>
    {
        hid::report_bitset<hid::page::button, hid::page::button(1), LAST_BUTTON> buttons{};
        std::conditional_t<(AXIS_LIMIT > std::numeric_limits<int8_t>::max()), hid::le_int16_t,
                           int8_t>
            x{};
        std::conditional_t<(AXIS_LIMIT > std::numeric_limits<int8_t>::max()), hid::le_int16_t,
                           int8_t>
            y{};
        std::conditional_t<(WHEEL_LIMIT > std::numeric_limits<int8_t>::max()), hid::le_int16_t,
                           int8_t>
            wheel_y{};
        std::conditional_t<(WHEEL_LIMIT > std::numeric_limits<int8_t>::max()), hid::le_int16_t,
                           int8_t>
            wheel_x{};

        constexpr mouse_report_base() = default;

        bool operator==(const mouse_report_base& other) const = default;
        bool operator!=(const mouse_report_base& other) const = default;

        bool steady() const { return (x == 0) && (y == 0) && (wheel_y == 0) && (wheel_x == 0); }
    };
    using mouse_report = mouse_report_base<MOUSE_REPORT_ID>;
    using resolution_multiplier_report =
        hid::app::mouse::resolution_multiplier_report<MAX_SCROLL_RESOLUTION, MOUSE_REPORT_ID>;

  private:
    alignas(std::uintptr_t) mouse_report in_report_{};
    alignas(std::uintptr_t) resolution_multiplier_report multiplier_report_{};
    hid::protocol prot_{};

  public:
    static constexpr auto report_desc()
    {
        using namespace hid::page;
        using namespace hid::rdf;

        // clang-format off
        return descriptor(
            usage_page<generic_desktop>(),
            usage(generic_desktop::MOUSE),
            collection::application(
                conditional_report_id<MOUSE_REPORT_ID>(),
                usage(generic_desktop::POINTER),
                collection::physical(
                    // buttons
                    usage_extended_limits(button(1), LAST_BUTTON),
                    logical_limits<1, 1>(0, 1),
                    report_count(static_cast<uint8_t>(LAST_BUTTON)),
                    report_size(1),
                    input::absolute_variable(),
                    input::byte_padding<static_cast<uint8_t>(LAST_BUTTON)>(),

                    // relative X,Y directions
                    usage(generic_desktop::X),
                    usage(generic_desktop::Y),
                    logical_limits<(AXIS_LIMIT > std::numeric_limits<int8_t>::max() ? 2 : 1)>(-AXIS_LIMIT, AXIS_LIMIT),
                    report_count(2),
                    report_size(AXIS_LIMIT > std::numeric_limits<int8_t>::max() ? 16 : 8),
                    input::relative_variable(),

                    hid::app::mouse::high_resolution_scrolling<WHEEL_LIMIT, MAX_SCROLL_RESOLUTION>()
                )
            )
        );
        // clang-format on
    }
    static const hid::report_protocol& report_prot()
    {
        static constexpr const auto rd{report_desc()};
        static constexpr const hid::report_protocol rp{rd};
        return rp;
    }

    demo_mouse()
        : hid::application(report_prot())
    {}
    auto send(const mouse_report& report)
    {
        in_report_ = report;
        return send_report(&in_report_);
    }
    void start(hid::protocol prot) override
    {
        prot_ = prot;
        multiplier_report_ = {};
        receive_report(&multiplier_report_);
        LOG_INF("HID start with protocol %s", magic_enum::enum_name(prot).data());
    }
    void stop() override
    {
        iolib_set_led(0, false);
        LOG_INF("HID stop");
    }
    void set_report(hid::report::type type, const std::span<const uint8_t>& data) override
    {
        if (type != resolution_multiplier_report::type())
        {
            return;
        }
        if (data.size() == sizeof(resolution_multiplier_report))
        {
            multiplier_report_ =
                *reinterpret_cast<const resolution_multiplier_report*>(data.data());
        }
        else
        {
            multiplier_report_.resolutions = 0;
        }
        iolib_set_led(0, multiplier_report_.resolutions != 0);
        LOG_INF("multiplier report: %x (%d)", multiplier_report_.resolutions, data.size());
        receive_report(&multiplier_report_);
    }
    void get_report(hid::report::selector select, const std::span<uint8_t>& buffer) override
    {
        if (select == mouse_report::selector())
        {
            send_report(&in_report_);
            return;
        }
        if (select == resolution_multiplier_report::selector())
        {
            send_report(&multiplier_report_);
            return;
        }
        // assert(false);
    }
    // void in_report_sent(const std::span<const uint8_t>& data) override {}
    hid::protocol get_protocol() const override { return prot_; }
    const auto& multiplier_report() const { return multiplier_report_; }
};

auto& mouse()
{
    static demo_mouse m{};
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
            if (ev == event::CONFIGURATION_CHANGE)
            {
                LOG_INF("USB configured: %u, granted current: %uuA", dev.configured(),
                        dev.granted_bus_current_uA());
            }
            else
            {
                LOG_INF("USB power state: %s, granted current: %uuA",
                        magic_enum::enum_name(dev.power_state()).data(),
                        dev.granted_bus_current_uA());
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
    demo_mouse::mouse_report report{};
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
