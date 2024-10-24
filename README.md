# c2usb Zephyr Example Application

This repository contains a c2usb Zephyr example application.

## Initialization

The first step is to initialize the workspace folder (``my-workspace``) where
the ``example-application`` and all Zephyr modules will be cloned. Run the following
command:

```shell
# initialize my-workspace for the example-application (main branch)
west init -m https://github.com/IntergatedCircuits/c2usb-zephyr-examples.git my-workspace
# update Zephyr modules
cd my-workspace
west update
```

## Building and running

To build the application, run the following command:

```shell
cd c2usb-zephyr-examples
west build -b nrf52840dk/nrf52840 usb-keyboard
```

Once you have built the application, run the following command to flash it:

```shell
west flash
```
