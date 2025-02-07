#include "iolib.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>

#include "demo_keyboard.hpp"
#include <port/zephyr/bluetooth/hid.hpp>
#include <port/zephyr/bluetooth/le.hpp>
#include <port/zephyr/message_queue.hpp>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const uint8_t adv_led = 1;

static int advertise(void)
{
    using namespace bluetooth::zephyr;

    static constexpr auto ad_data =
        join(ad_struct<std::uint16_t>(BT_DATA_GAP_APPEARANCE, CONFIG_BT_DEVICE_APPEARANCE),
             ad_struct<std::uint8_t>(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
             ad_struct<std::uint16_t>(BT_DATA_UUID16_ALL, {BT_UUID_HIDS_VAL, BT_UUID_BAS_VAL}));
    static constexpr auto ad = to_adv_data<ad_struct_count(ad_data)>(ad_data);
    static constexpr auto sd_data = join(ad_struct(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME));
    static constexpr auto sd = to_adv_data<ad_struct_count(sd_data)>(sd_data);
    const bt_le_adv_param* adv_param =
        BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_ONE_TIME,
                        BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL);

    auto err = bt_le_adv_start(adv_param, ad.data(), ad.size(), sd.data(), sd.size());
    if (err == 0)
    {
        iolib_set_led(adv_led, true);
        LOG_INF("Advertising successfully started\n");
    }
    else if (err == -EALREADY)
    {
        LOG_INF("Advertising continued\n");
    }
    else
    {
        LOG_WRN("Advertising failed to start (err %d)\n", err);
    }
    return err;
}

auto& pairing_msgq()
{
    static os::zephyr::message_queue<::bt_conn*, CONFIG_BT_MAX_CONN> msgq;
    return msgq;
}

static void connected(bt_conn* conn, uint8_t err)
{
    bluetooth::zephyr::address_str addr{conn};

    if (err)
    {
        LOG_WRN("Failed to connect to %s 0x%02x %s\n", addr.data(), err, bt_hci_err_to_str(err));
        return;
    }
    LOG_INF("Connected %s\n", addr.data());

    if (!advertise())
    {
        iolib_set_led(adv_led, false);
    }
}

static void disconnected(bt_conn* conn, uint8_t reason)
{
    bluetooth::zephyr::address_str addr{conn};

    LOG_INF("Disconnected from %s, reason 0x%02x %s\n", addr.data(), reason,
            bt_hci_err_to_str(reason));

    advertise();
}

static void security_changed(bt_conn* conn, bt_security_t level, bt_security_err err)
{
    bluetooth::zephyr::address_str addr{conn};

    if (!err)
    {
        LOG_INF("Security changed: %s level %u\n", addr.data(), level);
    }
    else
    {
        LOG_WRN("Security failed: %s level %u err %d %s\n", addr.data(), level, err,
                bt_security_err_to_str(err));
    }
}

static void le_param_updated(bt_conn* conn, uint16_t interval, uint16_t latency, uint16_t timeout);

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .le_param_updated = le_param_updated,
    .security_changed = security_changed,
};

static void auth_passkey_entry(bt_conn* conn)
{
    bluetooth::zephyr::address_str addr{conn};
    LOG_INF("Passkey requested for %s, type `bt passkey XXXXXX` to complete\n", addr.data());
    if (!pairing_msgq().try_post(bt_conn_ref(conn)))
    {
        LOG_WRN("Pairing queue full\n");
    }
}

static void auth_cancel(bt_conn* conn)
{
    bluetooth::zephyr::address_str addr{conn};
    LOG_INF("Pairing cancelled: %s\n", addr.data());
}

static const bt_conn_auth_cb conn_auth_callbacks = {.passkey_entry = auth_passkey_entry,
                                                    .cancel = auth_cancel};

static void pairing_complete(bt_conn* conn, bool bonded)
{
    bluetooth::zephyr::address_str addr{conn};
    LOG_INF("Pairing completed: %s, bonded: %d\n", addr.data(), bonded);
}

static void pairing_failed(bt_conn* conn, bt_security_err reason)
{
    bluetooth::zephyr::address_str addr{conn};
    LOG_INF("Pairing failed conn: %s, reason %d %s\n", addr.data(), reason,
            bt_security_err_to_str(reason));

    auto pairing = pairing_msgq().peek();
    if (pairing && pairing.value() == conn)
    {
        bt_conn_unref(conn);
        pairing_msgq().try_get();
    }
}

static bt_conn_auth_info_cb conn_auth_info_callbacks = {.pairing_complete = pairing_complete,
                                                        .pairing_failed = pairing_failed};

static void pairing_reply(int passkey)
{
    auto pairing = pairing_msgq().try_get();
    if (!pairing)
    {
        return;
    }
    if (passkey < 0)
    {
        bt_conn_auth_cancel(pairing.value());
    }
    else
    {
        bt_conn_auth_passkey_entry(pairing.value(), passkey);
    }

    bt_conn_unref(pairing.value());
}

static int cmd_bt_passkey(const shell* sh, size_t argc, char** argv)
{
    int err = 0;
    int passkey = shell_strtol(argv[1], 10, &err);
    if (err)
    {
        shell_error(sh, "Invalid passkey %d", err);
    }
    else
    {
        pairing_reply(passkey);
    }
    return 0;
}

static int cmd_bt_battery(const shell* sh, size_t argc, char** argv)
{
    int err{};
    auto battery_level = shell_strtoul(argv[1], 10, &err);
    if (err)
    {
        shell_error(sh, "Invalid battery level %d", err);
    }
    else
    {
        bt_bas_set_battery_level(battery_level);
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_bt, SHELL_CMD_ARG(passkey, NULL, "Send BT pairing passkey", cmd_bt_passkey, 2, 0),
    SHELL_CMD_ARG(battery, NULL, "Update battery level to BT central", cmd_bt_battery, 2, 0),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(bt, &sub_bt, "BT", NULL);

auto& keyboard_app()
{
    static demo_keyboard keyb{hid::page::keyboard_keypad::KEYBOARD_CAPS_LOCK,
                              [](const demo_keyboard::kb_leds_report& report)
                              {
                                  iolib_set_led(0, report.leds.test(hid::page::leds::CAPS_LOCK));
                              }};
    return keyb;
}

static auto& hog_service()
{
    using namespace magic_enum::bitwise_operators;
    using namespace bluetooth::zephyr::hid;

    // TODO: set values as needed
    static const auto security = security::ENCRYPT;
    static const auto features = flags::NORMALLY_CONNECTABLE | flags::REMOTE_WAKE;

    static service_instance<hid::report_protocol_properties(demo_keyboard::report_desc()),
                            boot_protocol_mode::KEYBOARD>
        hog{keyboard_app(), security, features};
    return hog;
}

auto& kb_msgq()
{
    static os::zephyr::message_queue<input_event, 2> msgq;
    return msgq;
}

static void input_cb(input_event* evt, void*)
{
    kb_msgq().post(*evt);
}

INPUT_CALLBACK_DEFINE(nullptr, input_cb, nullptr);

#if CONFIG_BT_GAP_PERIPHERAL_PREF_PARAMS
static_assert((10 * CONFIG_BT_PERIPHERAL_PREF_TIMEOUT) >
                  ((1 + CONFIG_BT_PERIPHERAL_PREF_LATENCY) * 2 *
                   (CONFIG_BT_PERIPHERAL_PREF_MAX_INT * 5 / 4)),
              "The peripheral preferred connection parameters are not valid");
#endif
static void le_param_updated(bt_conn* conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
    if (conn != hog_service().peer())
    {
        return;
    }
    LOG_INF("BLE HID conn params: interval=%u ms, latency=%u, timeout=%u ms\n", interval * 5 / 4,
            latency, timeout * 10);
}

int main(void)
{
    if (auto err = bt_conn_auth_cb_register(&conn_auth_callbacks); err)
    {
        LOG_ERR("Failed to register authorization callbacks.\n");
        return 0;
    }
    if (auto err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks); err)
    {
        LOG_ERR("Failed to register authorization info callbacks.\n");
        return 0;
    }
    if (!hog_service().start())
    {
        LOG_ERR("HID service initialization failed\n");
        return 0;
    }
    if (auto err = bt_enable(nullptr); err)
    {
        LOG_ERR("Bluetooth init failed (err %d)\n", err);
        return 0;
    }
    if (IS_ENABLED(CONFIG_SETTINGS))
    {
        settings_load();
    }
    advertise();

    while (true)
    {
        auto msg = kb_msgq().get();
        switch (msg.code)
        {
        case INPUT_KEY_0:
            keyboard_app().send_key(msg.value);
            break;
        default:
            break;
        }
    }
}
