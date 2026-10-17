# Modbus RS-485 Scanner — Waveshare ESP32-S3-Touch-LCD-4

Firmware w C++/Arduino (PlatformIO) do skanowania magistrali Modbus RTU
na płytce Waveshare ESP32-S3-Touch-LCD-4 (480×480 IPS + GT911 touch + SP3485).
Oferuje:

- **Skaner Modbus RTU** – przeszukuje adresy slave 1–247 na dowolnym baudrate/parity
- **Auto-detect baudrate** – próbuje po kolei 4800/9600/19200/38400/57600/115200 i wybiera konfigurację, w której wykryto urządzenia
- **Odczyt rejestrów** – Holding (0x03), Input (0x04), Coil (0x01), Discrete (0x02)
- **Interfejs LVGL** – trzy zakładki na ekranie dotykowym (Skaner / Ustawienia / Rejestry)
- **WiFi WebUI** – ten sam zestaw funkcji z poziomu przeglądarki na telefonie/laptopie
- **Logowanie na microSD** – lista znalezionych urządzeń + log zdarzeń CSV
- **AI Asystent (GPT-5.6 Terra przez Emergent Universal Key)** – 3 funkcje w WebUI:
  1. **Asystent diagnostyczny** – zadajesz pytanie, model otrzymuje kontekst z ostatniego skanu + odczytanych rejestrów
  2. **Autoanaliza po skanie** – po zakończeniu skanowania automatycznie generuje raport (opcjonalnie, włącz checkbox)
  3. **Komenda w języku naturalnym** – wpisujesz np. „odczytaj temperaturę z urządzenia 5", AI zwraca gotowy ramkę Modbus (slave/typ/start/count) z jednym klikiem do wykonania

Architektura AI:
```
Przeglądarka (telefon)
      │  HTTPS SSE
      ▼
Backend FastAPI (Emergent)         ◄── EMERGENT_LLM_KEY
      │  emergentintegrations
      ▼
       OpenAI gpt-5.6-terra
```
ESP32 nie łączy się bezpośrednio z OpenAI – klucz nigdy nie opuszcza chmury.

---

## Pinout (Waveshare ESP32-S3-Touch-LCD-4)

| Peryferia | Pin                                   |
|-----------|---------------------------------------|
| RS-485 TX | GPIO44                                |
| RS-485 RX | GPIO43                                |
| RS-485 DIR| auto (SP3485, brak linii DE/RE)       |
| CAN TX    | GPIO6 (TJA1051T)                      |
| CAN RX    | GPIO0 (TJA1051T)                      |
| I2C SDA (GT911 + CH32V003) | GPIO15               |
| I2C SCL (GT911 + CH32V003) | GPIO7                |
| SD_MMC CLK / CMD / D0 (1-bit) | GPIO2 / GPIO1 / GPIO4 |

Piny zgodne z oficjalnymi przykładami Waveshare (`examples/arduino`). Sterownik ekranu, dotyku
i CH32V003 (`GFX_Library_for_Arduino`, `SensorLib`, `WS_CH32_IO`) są dołączone w `lib/`.

## Skaner CAN

Zakładka **CAN** (LCD i WebUI) nasłuchuje magistrali w trybie *listen-only* (nie zakłóca sieci,
nie wysyła ACK) i zbiera unikalne identyfikatory 11/29-bit: liczba ramek, DLC, ostatnie dane.
- Prędkości: 50k / 100k / 125k / 250k / 500k / 1M, lub **auto-detekcja** (próbuje kolejno do wykrycia ruchu)
- Czas nasłuchu konfigurowalny (domyślnie 10 s); wynik zapisywany na SD jako CSV
- WebUI: dodatkowo wysyłanie pojedynczej ramki testowej (tryb normalny, np. zapytanie OBD-II `7DF 02 01 00`)
- Uwaga: CAN nie ma "adresów slave" jak Modbus — widoczne są tylko węzły, które same nadają.
  Auto-detekcja w trybie listen-only wymaga co najmniej dwóch aktywnych węzłów na magistrali
  (ktoś musi potwierdzać ramki ACK).

---

## Budowanie firmware

```bash
# 1. Zainstaluj PlatformIO
pip install -U platformio

# 2. Wejdź do folderu firmware
cd firmware

# 3. Zbuduj i wgraj przez USB-C
pio run -t upload

# 4. Wgraj web UI na LittleFS (index.html / app.js / style.css)
pio run -t uploadfs

# 5. Monitor szeregowy
pio device monitor
```

Pierwsze uruchomienie:
1. Firmware startuje w trybie AP: `ModbusScanner-XXXX`, hasło `modbus1234`
2. Łączysz się telefonem → otwierasz `http://192.168.4.1`
3. W zakładce **Ustawienia** wpisujesz SSID/hasło domowej sieci + zapisujesz
4. Po reboocie firmware łączy się do WiFi; IP widać na ekranie LCD oraz w zakładce "Skaner"
5. W zakładce **AI Asystent** URL backendu jest już ustawiony na publiczny Emergent URL — gotowe

## Podłączenie Modbus

Podłącz pod kostkę RS-485 na płytce:
- **A** → A/D+ urządzenia
- **B** → B/D- urządzenia
- **GND** → GND (zalecane dla długich kabli)
- Terminacja 120 Ω na końcach magistrali
- Płytka ma już wbudowany SP3485 z automatycznym przełączaniem kierunku

## Użycie AI Asystenta

W zakładce "AI Asystent":
- **Autoanaliza po skanie** – zaznacz checkbox, firmware po każdym skanie
  automatycznie wysyła listę ID + prędkość do GPT i pokazuje raport
- **Komenda NL** – wpisz np.:
  - „Odczytaj temperaturę z urządzenia 5"
  - „Pokaż napięcie fazowe z licznika energii o ID 10"
  - „Status cewek pierwszych 16 dla PLC pod adresem 3"
  Kliknij "Zbuduj zapytanie" → "Wykonaj". AI zwraca JSON z gotowymi parametrami
- **Czat** – zadaj dowolne pytanie; AI dostaje w kontekście ostatnie wyniki skanu
  i odczytane rejestry, więc odpowiada konkretnie

## Przyszłe rozszerzenia
- Zapis rejestrów (Function Code 0x06 / 0x10)
- Harmonogram cyklicznych odczytów
- Eksport długoterminowy do InfluxDB / MQTT

## Licencja
MIT
