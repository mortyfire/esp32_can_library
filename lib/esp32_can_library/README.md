/**
 * ESP32 CAN-Bus Library – Anleitung für Einsteiger (vollständig erklärt)
 * ======================================================================
 *
 * Diese Library vereinfacht die Kommunikation über den CAN-Bus mit dem ESP32.
 * Sie übernimmt automatisch:
 *  - das Aufteilen von großen Daten in 8-Byte-Blöcke
 *  - die Wieder-Zusammensetzung (Reassemblierung) beim Empfänger
 *  - die Adressierung, Priorisierung und Typ-Erkennung
 *  - Fehlerprüfung mit CRC
 *  - optionales Wiederholen bei fehlender Bestätigung (ACK)
 *
 * ------------------------------------------------------------------------
 * TEIL 1: Wie funktioniert CAN eigentlich?
 * ------------------------------------------------------------------------
 * Beim CAN-Bus wird nicht wie bei I2C oder UART an konkrete Geräteadressen gesendet,
 * sondern man sendet eine „Nachricht mit Bedeutung“. Jedes Datenpaket hat:
 *
 *    → eine **Identifier**-Nummer (11 Bit)
 *    → bis zu 8 Bytes Nutzdaten
 *
 * Geräte entscheiden **selbst**, ob sie auf eine Nachricht reagieren – abhängig vom Identifier.
 *
 * ------------------------------------------------------------------------
 * TEIL 2: Wie sieht der Identifier aus in dieser Library?
 * ------------------------------------------------------------------------
 * Wir haben den 11-Bit-Identifier so aufgeteilt:
 *
 *    Bits 10–9   → 2 Bit: Priorität (00 = niedrig, 11 = hoch)
 *    Bits  8–5   → 4 Bit: Adresse des Empfängers (0–14, 15 = Broadcast)
 *    Bits  4–3   → 2 Bit: Position im Paket (Sequenz)
 *                  - 00 = Startfragment
 *                  - 01 = Zwischenfragment
 *                  - 10 = Endfragment
 *                  - 11 = Einzelpaket (nicht fragmentiert)
 *    Bits  2–0   → 3 Bit: Nachrichtentyp (Type-ID, siehe DEFINE_CAN_MESSAGE)
 *
 * Beispiel:
 *   Identifier 0b10_0110_11_001  →
 *     - Priorität: 2 (10)
 *     - Adresse:   6  (0110)
 *     - Sequenz:   3  (SINGLE)
 *     - Typ:       1  (z. B. StatusMsg)
 *
 * ------------------------------------------------------------------------
 * TEIL 3: So nutzt du die Library Schritt für Schritt
 * ------------------------------------------------------------------------
 *
 * 1. Nachricht definieren (z. B. Sensor-Daten)
 * --------------------------------------------
 * DEFINE_CAN_MESSAGE(SensorData, 1,
 *     float temperature;
 *     float humidity;
 * );
 *
 * → `SensorData` ist dein Datentyp
 * → `1` ist die eindeutige Type-ID (max. 0–7 möglich)
 *
 *
 * 2. CAN initialisieren und Callback setzen
 * --------------------------------------------
 * CANBus can(GPIO_NUM_5, GPIO_NUM_4);
 * can.init();
 *
 * can.onReceive<SensorData>([](const SensorData& s) {
 *     Serial.printf("T=%.1f, H=%.1f\n", s.temperature, s.humidity);
 * });
 *
 *
 * 3. Nachricht senden
 * --------------------------------------------
 * SensorData data = {24.1f, 61.5f};
 * can.send<SensorData>(2, 4, data);  // Priorität 2, an Node 4
 *
 *
 * 4. Wiederholung & Fehler behandeln
 * --------------------------------------------
 * can.setRetryLimit(2); // Versuche max. 2× bei fehlendem ACK
 * can.onError([](uint8_t type, uint8_t addr) {
 *     Serial.printf("Fehler: Typ %u an Node %u nicht erfolgreich\n", type, addr);
 * });
 *
 *
 * ------------------------------------------------------------------------
 * TEIL 4: Wie läuft ein Datentransfer genau ab?
 * ------------------------------------------------------------------------
 * 1. Du sendest ein Struct (z. B. 20 Bytes groß)
 * 2. Die Library fragmentiert es automatisch in 8-Byte-Blöcke → max. 3 Frames
 * 3. Jeder Frame erhält eine passende Sequenzkennung (START, MIDDLE, END)
 * 4. Im letzten Frame steckt eine Prüfsumme (CRC)
 * 5. Der Empfänger setzt alle Fragmente korrekt zusammen (Reassemblierung)
 * 6. Falls CRC passt → deine Callback-Funktion wird aufgerufen
 * 7. Der Empfänger sendet automatisch ein ACK zurück
 * 8. Der Sender wartet (max. RetryLimit mal), ob ACK eintrifft
 *
 * ------------------------------------------------------------------------
 * TEIL 5: Welche Adresse soll ich nehmen?
 * ------------------------------------------------------------------------
 * Adressen (Bits 8–5) = 4 Bit → 0 bis 15 möglich
 *    - 0–14 = normale Geräteadresse
 *    - 15 (0b1111) = Broadcast (an alle)
 *
 * Jeder Node im Netzwerk bekommt eine eigene Adresse (z. B. 1 = Sensor, 2 = Controller, ...)
 * Diese Adresse gibst du beim `send()`-Aufruf an.
 *
 * ------------------------------------------------------------------------
 * TEIL 6: Warum ist das besser als manuell?
 * ------------------------------------------------------------------------
 * Mit dieser Library musst du dich **nicht selbst** um diese Punkte kümmern:
 *    - Bytes splitten in 8-Byte-Pakete
 *    - Prüfsummen prüfen
 *    - Nachricht wieder zusammensetzen
 *    - ACK erkennen und ggf. wiederholen
 *    - Nachrichten per Typ-ID aufteilen und verarbeiten
 *
 * Das spart dir als Einsteiger **Tage an Entwicklungszeit** und **vermeidet Fehler**.
 *
 * ------------------------------------------------------------------------
 * TEIL 7: Debug-Tipp
 * ------------------------------------------------------------------------
 * Du kannst im Callback einfach `Serial.print()` verwenden, um zu prüfen, was empfangen wurde.
 * Stelle sicher, dass du auch wirklich `can.handleReceive()` im `loop()` aufrufst.
 *
 * ------------------------------------------------------------------------
 * TEIL 8: Für Fortgeschrittene
 * ------------------------------------------------------------------------
 * Möchtest du:
 *    - einen eigenen Logger aufbauen?
 *    - CAN-Daten auf SD speichern?
 *    - Messdaten mit Zeitstempel synchronisieren?
 *    - eine eigene Diagnose über CAN realisieren?
 *    - deinen Quadrocopter per CAN debuggen?
 *
 * → Die Library lässt sich problemlos in jedes dieser Szenarien einbauen!
 */
