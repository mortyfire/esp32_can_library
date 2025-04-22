#include <Arduino.h>
#include "esp32_can_library.h"

// Nachrichtentypen definieren
DEFINE_CAN_MESSAGE(StatusMsg, 1, uint8_t state; uint8_t errorCode;);
DEFINE_CAN_MESSAGE(SensorData, 2, float temperature; float humidity;);

CANBus can(GPIO_NUM_5, GPIO_NUM_4);

void setup() {
    Serial.begin(115200);

    // CAN initialisieren
    if (can.init() != ESP_OK) {
        Serial.println("CAN Init fehlgeschlagen");
        while (true);
    }

    // ACK-Retries (je 100 ms)
    can.setRetryLimit(2);

    // Fehler-Callback
    can.onError([](uint8_t type, uint8_t addr) {
        Serial.printf("Sendefehler: Type=%u, Addr=%u\n", type, addr);
    });

    // Empfangs-Callbacks
    can.onReceive<StatusMsg>([](const StatusMsg& m) {
        Serial.printf("StatusMsg empfangen: state=%u, error=%u\n",
                      m.state, m.errorCode);
    });
    can.onReceive<SensorData>([](const SensorData& d) {
        Serial.printf("SensorData: T=%.2f, H=%.1f%%\n",
                      d.temperature, d.humidity);
    });
}

void loop() {
    // Eintreffende Nachrichten verarbeiten
    can.handleReceive();

    // StatusMsg senden
    StatusMsg st{1, 0};
    if (can.send<StatusMsg>(1, 3, st) != ESP_OK) {
        Serial.println("Fehler beim Senden von StatusMsg");
    }

    // SensorData senden
    SensorData sd{24.3f, 48.5f};
    can.send<SensorData>(0, 2, sd);

    delay(1000);
}
