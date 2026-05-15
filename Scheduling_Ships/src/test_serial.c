#include "hardware_serial.h"
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    HardwareSerial hw;

    if (hardware_serial_init(&hw, "/dev/ttyUSB0", 115200, 1) != 0) {
        printf("Error serial: %s\n", hw.last_error);
        return 1;
    }

    int leds[HW_LED_COUNT] = {
        1,2,0,0,4,0,0,3,0,0,0,0,0,0,0,4,1,0,2,0,6
    };

    if (hardware_serial_send_leds(&hw, leds) != 0) {
        printf("Error envio: %s\n", hw.last_error);
    } else {
        printf("Lista enviada al ESP32-D.\n");
    }

    sleep(1);
    hardware_serial_close(&hw);
    return 0;
}