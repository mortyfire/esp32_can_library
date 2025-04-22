/**
ESP32 CAN Bus Library mit TJA1050 – kompakt und konfigurierbar
=================================================================

Identifier-Schema (11 Bit):
[10..9] 2 Bit Priorität (00: niedrig ... 11: hoch)
[8..5] 4 Bit Adresse (0000–1110: Node-ID, 1111: Broadcast)
[4..3] 2 Bit Sequenz-Status (00: Start, 01: Middle, 10: End, 11: Single)
[2..0] 3 Bit Payload-Type ID (Makro-Definition)

Usage:
setRetryLimit(n): Anzahl ACK-Retries (0 = kein ACK)
onError(cb): Callback bei Sendefehler (Typ, Adresse)
Default: RetryLimit=3 */
#ifndef ESP32_CAN_LIBRARY_H
#define ESP32_CAN_LIBRARY_H

#include <driver/twai.h>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <chrono>

class CANBus {
public:
    enum Sequence : uint8_t { START=0, MIDDLE=1, END=2, SINGLE=3 };
    static constexpr uint32_t REASSEMBLY_TIMEOUT = 500;
    static constexpr uint8_t  ACK_TYPE_ID = 0x7;

    using ErrorCallback = std::function<void(uint8_t type, uint8_t address)>;

    template<typename T, uint8_t TYPE_ID>
    struct MsgTraits { using type = T; static constexpr uint8_t TypeID = TYPE_ID; };

    // Konstruktion: TX/RX Pins, Bus-Modus, Baudrate
    CANBus(gpio_num_t tx_pin, gpio_num_t rx_pin,
           twai_mode_t mode = TWAI_MODE_NORMAL, uint32_t baud = 500000)
      : retryLimit_(3), errorCb_(nullptr)
    {
        config_.mode = mode;
        config_.tx_io = tx_pin;
        config_.rx_io = rx_pin;
        config_.clkout_io = GPIO_NUM_NC;
        config_.bus_off_io = GPIO_NUM_NC;
        config_.tx_queue_len = 10;
        config_.rx_queue_len = 10;
        config_.alerts_enabled = TWAI_ALERT_NONE;
        config_.clkout_divider = 0;
        timing_ = TWAI_TIMING_CONFIG_500KBITS();
        filter_.acceptance_code = 0;
        filter_.acceptance_mask = 0;
        filter_.single_filter = true;
    }

    // Driver installieren und starten
    esp_err_t init() {
        esp_err_t err = twai_driver_install(&config_, &timing_, &filter_);
        if (err != ESP_OK) return err;
        return twai_start();
    }

    // Anzahl der ACK-Retries setzen (0 = kein ACK erwartet)
    void setRetryLimit(uint8_t n) { retryLimit_ = n; }
    // Callback bei Sendefehler
    void onError(ErrorCallback cb) { errorCb_ = cb; }

    // Nachricht senden (Struktur muss POD sein)
    template<typename T>
    esp_err_t send(uint8_t prio, uint8_t addr, const T& msg) {
        static_assert(std::is_standard_layout<T>::value, "T must be POD");
        constexpr uint8_t type = MsgTraits<T, 0>::TypeID;
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(&msg);
        size_t len = sizeof(T);
        std::vector<uint8_t> buf(raw, raw + len);
        bool fragmented = (len > 8);
        if (fragmented) buf.push_back(crc8(raw, len));

        uint8_t attempts = 0;
        while (true) {
            // Fragmente senden
            size_t offset = 0;
            while (offset < buf.size()) {
                size_t chunk = std::min<size_t>(8, buf.size() - offset);
                Sequence seq = !fragmented ? SINGLE :
                    (offset == 0 ? START :
                     (offset + chunk >= buf.size() ? END : MIDDLE));
                twai_message_t m{};
                m.identifier = buildId(prio, addr, seq, type);
                m.extd = 0;
                m.data_length_code = chunk;
                memcpy(m.data, buf.data() + offset, chunk);
                esp_err_t e = twai_transmit(&m, pdMS_TO_TICKS(100));
                if (e != ESP_OK) return e;
                offset += chunk;
            }
            // Bei nicht fragmentierten Nachrichten kein ACK
            if (retryLimit_ == 0 || !fragmented) return ESP_OK;
            // Auf ACK warten
            if (waitAck(type, addr)) return ESP_OK;
            // Retry-Limit erreicht?
            if (++attempts > retryLimit_) break;
        }
        // Fehler-Callback
        if (errorCb_) errorCb_(type, addr);
        return ESP_FAIL;
    }

    // Callback für empfangene Nachricht T
    template<typename T>
    void onReceive(std::function<void(const T&)> cb) {
        constexpr uint8_t type = MsgTraits<T, 0>::TypeID;
        handlers_[type] = [cb](const std::vector<uint8_t>& data) {
            if (data.size() < sizeof(T)) return;
            T msg;
            memcpy(&msg, data.data(), sizeof(T));
            cb(msg);
        };
    }

    // Im Loop oder Task aufrufen
    void handleReceive() {
        twai_message_t m;
        if (twai_receive(&m, pdMS_TO_TICKS(10)) != ESP_OK) return;
        uint32_t id = m.identifier;
        uint8_t seq = (id >> 3) & 0x03;
        uint8_t type = id & 0x07;
        uint8_t from = (id >> 5) & 0x0F;
        // ACK-Frame
        if (type == ACK_TYPE_ID) {
            pendingAck_ = m.data_length_code>0 ? m.data[0] : 0;
            return;
        }
        uint32_t baseId = id & ~static_cast<uint32_t>(0x18);
        auto now = std::chrono::steady_clock::now();
        FragEntry& entry = fragMap_[baseId];
        if (seq == START) {
            entry.data.clear();
            entry.timestamp = now;
            append(entry, m);
        } else if (seq == MIDDLE) {
            if (expired(entry.timestamp, now)) { fragMap_.erase(baseId); return; }
            append(entry, m);
        } else if (seq == END) {
            if (expired(entry.timestamp, now)) { fragMap_.erase(baseId); return; }
            append(entry, m);
            if (entry.data.size() < 1) { fragMap_.erase(baseId); return; }
            uint8_t recvCrc = entry.data.back();
            entry.data.pop_back();
            if (recvCrc == crc8(entry.data.data(), entry.data.size())) {
                dispatch(type, entry.data);
                sendAck(from, type);
            }
            fragMap_.erase(baseId);
        } else { // SINGLE
            std::vector<uint8_t> d(m.data, m.data + m.data_length_code);
            dispatch(type, d);
        }
    }

private:
    struct FragEntry {
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point timestamp;
    };
    twai_general_config_t config_{};
    twai_timing_config_t timing_{};
    twai_filter_config_t filter_{};
    uint8_t retryLimit_;
    ErrorCallback errorCb_;
    uint8_t pendingAck_ = 0;
    std::unordered_map<uint32_t, FragEntry> fragMap_;
    std::unordered_map<uint8_t, std::function<void(const std::vector<uint8_t>&)>> handlers_;

    void append(FragEntry& entry, const twai_message_t& msg) {
        entry.data.insert(entry.data.end(), msg.data, msg.data + msg.data_length_code);
    }

    bool waitAck(uint8_t type, uint8_t addr) {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start).count() < 100) {
            if (pendingAck_ == type) {
                pendingAck_ = 0;
                return true;
            }
        }
        return false;
    }

    void sendAck(uint8_t to, uint8_t type) {
        twai_message_t a{};
        a.identifier = buildId(3, to, SINGLE, ACK_TYPE_ID);
        a.extd = 0;
        a.data_length_code = 1;
        a.data[0] = type;
        twai_transmit(&a, pdMS_TO_TICKS(20));
    }

    static bool expired(
        const std::chrono::steady_clock::time_point& t0,
        const std::chrono::steady_clock::time_point& now) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count() > REASSEMBLY_TIMEOUT;
    }

    static uint8_t crc8(const uint8_t* data, size_t len) {
        uint8_t crc = 0;
        for (size_t i = 0; i < len; ++i) {
            crc ^= data[i];
            for (int j = 0; j < 8; ++j) {
                crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
            }
        }
        return crc;
    }

    static uint32_t buildId(uint8_t prio, uint8_t addr, uint8_t seq, uint8_t type) {
        return ((static_cast<uint32_t>(prio & 0x03) << 9) |
                (static_cast<uint32_t>(addr & 0x0F) << 5) |
                (static_cast<uint32_t>(seq & 0x03) << 3) |
                (type & 0x07));
    }

    void dispatch(uint8_t type, const std::vector<uint8_t>& data) {
        auto it = handlers_.find(type);
        if (it != handlers_.end()) it->second(data);
    }
};

#define DEFINE_CAN_MESSAGE(Name, ID, ...) \
    struct Name { __VA_ARGS__ }; \
    template<> struct CANBus::MsgTraits<Name, ID> { using type = Name; static constexpr uint8_t TypeID = ID; };

#endif // ESP32_CAN_LIBRARY_H
