#include <Arduino.h>               // Basis-Funktionen für Serial, pinMode, etc.
#include "esp32_can_library.h"     // Deine kompakte ESP32-CAN‑Library

// -----------------------------
// 1) Definiere “kleine” Pakete
// -----------------------------
// StatusMsg: nur 2 Byte Payload (state + errorCode)
DEFINE_CAN_MESSAGE(StatusMsg,    1,
    uint8_t state;       // Systemzustand (z. B. 0=OK, 1=Warnung, 2=Fehler)
    uint8_t errorCode;   // Fehlercode im Detail
);
// HeartbeatMsg: 2 Byte Payload (nur nodeId)
DEFINE_CAN_MESSAGE(HeartbeatMsg, 2,
    uint16_t nodeId;     // Kennung der sendenden Einheit
);

// -----------------------------
// 2) Definiere “mittlere” Pakete
// -----------------------------
// TempHumMsg: 8 Byte Payload (float temperature + float humidity)
DEFINE_CAN_MESSAGE(TempHumMsg,  10,
    float temperature;   // Temperatur in °C
    float humidity;      // Relative Luftfeuchte in %
);
// PressureMsg: 5 Byte Payload (float pressure + uint8_t unit)
DEFINE_CAN_MESSAGE(PressureMsg, 11,
    float pressure;      // Druckwert (z. B. in Pascal)
    uint8_t unit;        // Einheitscode (z. B. 0=Pa, 1=bar, 2=psi)
);

// -----------------------------
// 3) Definiere “große” Pakete
// -----------------------------
// ConfigBlock: bis zu 61 Byte (1 Byte ID + 60 Bytes Daten)
struct ConfigBlock {
    uint8_t  id;         // Konfigurations­block‑ID
    uint8_t  data[60];   // Nutzdaten (z. B. Parameter‑Array)
};
// ConfigMsg nutzt ConfigBlock und wird automatisch fragmentiert
DEFINE_CAN_MESSAGE(ConfigMsg, 20,
    ConfigBlock cfg;
);

// -----------------------------
// CANBus Instanz anlegen
// -----------------------------
// TX-Pin = GPIO5, RX-Pin = GPIO4, 500 kb/s, Normal‑Modus
CANBus can(GPIO_NUM_5, GPIO_NUM_4);

void setup() {
    // -------------------------
    // 1) Seriellen Monitor starten
    // -------------------------
    Serial.begin(115200);                         // Baudrate 115200 bps
    while (!Serial) { /* warten, bis Serial bereit ist */ }

    // ----------------------------------
    // 2) CAN‑Bus initialisieren & konfigurieren
    // ----------------------------------
    if (can.init() != ESP_OK) {
        // Endlosschleife bei Init‑Fehler
        Serial.println("CAN Init fehlgeschlagen");
        while (true) { delay(100); }
    }
    can.setRetryLimit(2);                         // 2 ACK‑Retries (je 100 ms)

    // ----------------------
    // 3) Globaler Fehler‑Callback
    // ----------------------
    can.onError([](uint8_t type, uint8_t addr) {
        // type = TypeID der Nachricht, addr = Adresse des Empfängers
        Serial.printf("CAN-Error: Type=%u, Addr=%u\n", type, addr);
    });

    // ---------------------------
    // 4) Empfangs‑Callbacks registrieren
    // ---------------------------
    // Callback für StatusMsg
    can.onReceive<StatusMsg>([](const StatusMsg& m) {
        Serial.printf("[Small] Status=%u, Error=%u\n",
                      m.state, m.errorCode);
    });
    // Callback für TempHumMsg
    can.onReceive<TempHumMsg>([](const TempHumMsg& m) {
        Serial.printf("[Medium] T=%.2f°C, H=%.1f%%\n",
                      m.temperature, m.humidity);
    });
    // Callback für ConfigMsg (großes Paket)
    can.onReceive<ConfigMsg>([](const ConfigMsg& m) {
        // Zeige Block‑ID und erstes Datenbyte
        Serial.printf("[Large] Config-ID=%u, FirstByte=%u\n",
                      m.cfg.id, m.cfg.data[0]);
    });
}

void loop() {
    // -----------------------------
    // 1) Eingehende CAN‑Nachrichten verarbeiten
    // -----------------------------
    can.handleReceive();  // Fragment‑Reassembly & Dispatch

    // -----------------------------
    // 2) StatusMsg senden (klein)
    // -----------------------------
    StatusMsg st{ 
        1,      // state = 1 (z. B. “Bereit”)
        0       // errorCode = 0 (kein Fehler)
    };
    can.send<StatusMsg>(0,  // Priorität: 0 (niedrig)
                        3,  // Adresse: 3 (Empfänger‑Node)
                        st);

    // -----------------------------
    // 3) TempHumMsg senden (mittel)
    // -----------------------------
    TempHumMsg th{
        23.7f,  // Temperatur in °C
        51.2f   // Luftfeuchte in %
    };
    can.send<TempHumMsg>(1,  // Priorität: 1 (mittel)
                         4,  // Adresse: 4
                         th);

    // -----------------------------
    // 4) ConfigMsg senden (groß)
    // -----------------------------
    ConfigBlock cb;
    cb.id = 42;                         // ID = 42
    memset(cb.data, 0xFF, sizeof(cb.data));  // Fülle Daten mit 0xFF
    ConfigMsg cfg{ cb };
    can.send<ConfigMsg>(3,  // Priorität: 3 (hoch)
                        5,  // Adresse: 5
                        cfg);

    // -----------------------------
    // 5) Eine Sekunde warten
    // -----------------------------
    delay(1000);
}
