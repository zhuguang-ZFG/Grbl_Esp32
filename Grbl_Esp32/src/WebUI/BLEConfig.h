#pragma once

/*
  BLEConfig.h - BLE Nordic UART Service (GATT) alongside classic Bluetooth SPP

  Service / characteristics use the Nordic UART UUIDs so common BLE serial apps
  and WeChat mini programs can use a known profile.
*/

#ifdef ENABLE_BLE

namespace WebUI {
    class BLEUartConfig {
    public:
        void begin();
        void end();
        bool hasClient() const;
        int  read();
        void print(const char* text);
        int  rx_ring_bytes_free() const;
        void handle();

    private:
        bool _started = false;
    };

    extern BLEUartConfig ble_uart_config;
}

#endif  // ENABLE_BLE
