#include <etl/delegate.h>
#include <hid/app/keyboard.hpp>
#include <hid/application.hpp>
#include <magic_enum.hpp>

class demo_keyboard : public hid::application
{
  public:
    using keys_report = hid::app::keyboard::keys_input_report<0>;
    using kb_leds_report = hid::app::keyboard::output_report<0>;

    static constexpr auto report_desc() { return hid::app::keyboard::app_report_descriptor<0>(); }
    static const hid::report_protocol& report_prot()
    {
        static constexpr const auto rd{report_desc()};
        static constexpr const hid::report_protocol rp{rd};
        return rp;
    }
    using leds_callback = etl::delegate<void(const kb_leds_report&)>;

    demo_keyboard(hid::page::keyboard_keypad key, leds_callback cbk)
        : hid::application(report_prot()), key_(key), cbk_(cbk)
    {}
    auto send_key(bool set)
    {
        keys_buffer_.scancodes.set(key_, set);
        return send_report(&keys_buffer_);
    }
    auto send_key(hid::page::keyboard_keypad key, bool set)
    {
        keys_buffer_.scancodes.set(key, set);
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
        cbk_(*out_report);
        LOG_INF("LEDs report: %x", data.data()[kb_leds_report::has_id()]);
        receive_report(&leds_buffer_);
    }
    void get_report(hid::report::selector select, const std::span<uint8_t>& buffer) override
    {
        send_report(&keys_buffer_);
    }
    void in_report_sent(const std::span<const uint8_t>& data) override {}
    hid::protocol get_protocol() const override { return prot_; }

  private:
    alignas(std::uintptr_t) keys_report keys_buffer_{};
    const hid::page::keyboard_keypad key_{};
    alignas(std::uintptr_t) kb_leds_report leds_buffer_{};
    leds_callback cbk_;
    hid::protocol prot_{};
};
