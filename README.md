# c2usb Zephyr sample applications

This repository contains c2usb Zephyr sample applications.

## Initialization

The first step is to initialize the workspace folder (``c2usb-workspace``) where
this repository and all Zephyr modules will be cloned. Run the following
command:

```shell
# initialize my-workspace for the usb-keyboard (main branch)
west init -m https://github.com/IntergatedCircuits/c2usb-zephyr-examples c2usb-workspace
# update Zephyr modules
cd c2usb-workspace
west update
```

## Application Index

### ble-keyboard

A straightforward BLE HID keyboard. UART shell access is needed to complete BLE pairing:
`bt passkey XXXXXX`
Use the button on the board to trigger a caps lock press,
and observe as the host changes the caps lock state on the board's LED.

### usb-keyboard

A straightforward USB HID keyboard. Use the button on the board to trigger a caps lock press,
and observe as the host changes the caps lock state on the board's LED.

### usb-mouse

A USB HID mouse, with high-resolution scrolling support.
The LED lights up if the host enables this feature.
The board's buttons 1 and 2 either scroll up and down, or move the pointer left and right,
depending on the state of button 4. Button 3 controls the left mouse button.

### usb-shell

Demonstrating USB serial port functionality with shell access to the zephyr OS.

## Building and running

To build an application, run the following command:

```shell
cd c2usb-zephyr-examples
west build -b nrf52840dk/nrf52840 usb-keyboard
```

Once you have built the application, run the following command to flash it:

```shell
west flash
```
