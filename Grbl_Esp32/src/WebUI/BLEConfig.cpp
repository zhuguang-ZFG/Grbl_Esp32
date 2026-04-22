/*
  BLEConfig.cpp - BLE GATT server (Nordic UART) for Grbl command stream

  begin() runs after BluetoothSerial::begin() (see Grbl.cpp): BTConfig::begin()
  ends with SerialBT.end() then SerialBT.begin(), which enables the controller;
  BLE is stacked on that dual-mode controller.
*/

#include "../Grbl.h"

#ifdef ENABLE_BLE

#    include <string>
#    include "BLEConfig.h"
#    include "WebSettings.h"
#    include <BLE2902.h>
#    include <BLEDevice.h>
#    include <BLEServer.h>
#    include <BLEUtils.h>

namespace WebUI {

// Nordic UART Service (central writes RX, peripheral notifies TX)
static const char* kNusServiceUuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* kNusRxUuid      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* kNusTxUuid      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

static constexpr size_t kRxRingCap = 512;
static constexpr size_t kDefaultTxChunk = 20;
static constexpr size_t kMaxTxChunk     = 244;  // 247 (common max MTU) - 3 ATT header

static uint8_t     s_rx_ring[kRxRingCap];
static volatile size_t s_rx_head = 0;
static volatile size_t s_rx_tail = 0;
static portMUX_TYPE    s_rx_mux  = portMUX_INITIALIZER_UNLOCKED;
static volatile size_t s_tx_chunk = kDefaultTxChunk;

static bool                 s_connected = false;
static BLEServer*           s_server    = nullptr;
static BLECharacteristic*   s_tx_char   = nullptr;
static BLECharacteristic*   s_rx_char   = nullptr;

class GrblBLEServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override { s_connected = true; }

    void onDisconnect(BLEServer* pServer) override {
        s_connected = false;
        BLEDevice::startAdvertising();
    }
};

class GrblBLERxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string v = pCharacteristic->getValue();
        if (v.length() == 0) {
            return;
        }
        // Adaptive TX chunk: if client starts writing packets larger than 20 bytes,
        // it likely completed MTU negotiation; mirror that payload size for notify.
        if (v.length() > kDefaultTxChunk) {
            size_t hinted = v.length();
            if (hinted > kMaxTxChunk) {
                hinted = kMaxTxChunk;
            }
            s_tx_chunk = hinted;
        }
        taskENTER_CRITICAL(&s_rx_mux);
        for (size_t i = 0; i < v.length(); ++i) {
            size_t next = (s_rx_head + 1) % kRxRingCap;
            if (next == s_rx_tail) {
                break;
            }
            s_rx_ring[s_rx_head] = (uint8_t)v[i];
            s_rx_head           = next;
        }
        taskEXIT_CRITICAL(&s_rx_mux);
    }
};

static GrblBLEServerCallbacks s_srv_cb;
static GrblBLERxCallbacks     s_rx_cb;

BLEUartConfig ble_uart_config;

void BLEUartConfig::begin() {
    if (_started) {
        return;
    }
    s_rx_head = s_rx_tail = 0;
    s_tx_chunk            = kDefaultTxChunk;

    String name = (bt_name != nullptr) ? bt_name->get() : String("Grbl");
    if (name.length() == 0) {
        name = "Grbl";
    }

    BLEDevice::init(name.c_str());
    s_server = BLEDevice::createServer();
    s_server->setCallbacks(&s_srv_cb);

    BLEService* svc = s_server->createService(kNusServiceUuid);

    s_tx_char = svc->createCharacteristic(kNusTxUuid, BLECharacteristic::PROPERTY_NOTIFY);
    s_tx_char->addDescriptor(new BLE2902());

    s_rx_char = svc->createCharacteristic(
        kNusRxUuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    s_rx_char->setCallbacks(&s_rx_cb);

    svc->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(kNusServiceUuid);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    _started = true;
    grbl_sendf(CLIENT_ALL, "[MSG:BLE UART NUS advertising as %s]\r\n", name.c_str());
}

void BLEUartConfig::end() {
    // Do not call BLEDevice::deinit() here: classic BluetoothSerial shares the controller.
    s_connected = false;
    s_rx_head = s_rx_tail = 0;
    s_server  = nullptr;
    s_tx_char = nullptr;
    s_rx_char = nullptr;
    _started  = false;
}

bool BLEUartConfig::hasClient() const { return s_connected; }

int BLEUartConfig::read() {
    taskENTER_CRITICAL(&s_rx_mux);
    if (s_rx_head == s_rx_tail) {
        taskEXIT_CRITICAL(&s_rx_mux);
        return -1;
    }
    uint8_t c = s_rx_ring[s_rx_tail];
    s_rx_tail  = (s_rx_tail + 1) % kRxRingCap;
    taskEXIT_CRITICAL(&s_rx_mux);
    return c;
}

void BLEUartConfig::print(const char* text) {
    if (!s_connected || s_tx_char == nullptr || text == nullptr) {
        return;
    }
    const size_t len = strlen(text);
    size_t       off = 0;
    size_t chunk = s_tx_chunk;
    if (chunk < kDefaultTxChunk || chunk > kMaxTxChunk) {
        chunk = kDefaultTxChunk;
    }
    while (off < len) {
        size_t n = len - off;
        if (n > chunk) {
            n = chunk;
        }
        s_tx_char->setValue((uint8_t*)(text + off), n);
        s_tx_char->notify();
        off += n;
    }
}

int BLEUartConfig::rx_ring_bytes_free() const {
    taskENTER_CRITICAL(&s_rx_mux);
    size_t used = (kRxRingCap + s_rx_head - s_rx_tail) % kRxRingCap;
    taskEXIT_CRITICAL(&s_rx_mux);
    if (used >= kRxRingCap - 1) {
        return 0;
    }
    return (int)(kRxRingCap - 1 - used);
}

void BLEUartConfig::handle() {}

}  // namespace WebUI

#endif  // ENABLE_BLE
