# PRD — Modbus RS-485 Scanner (Waveshare ESP32-S3-Touch-LCD-4) + AI Companion

## Problem statement (verbatim)
> mam płytke deweloperską Waveshare esp32-s3-Touch-LCD-4 i chciał bym oprogramowanie
> do odczytywania (skanowania) sieci modbus i can oraz możliwość odczytu urządzeń
> na tych sieciach. na początek skupmy sie tylko na modbus rs-485 który jest
> zaimplementowany w tej płytce
>
> Follow-up: Add the ChatGPT AI Models integration to my app.

## Target user
Automatyk / inżynier instalacji, który dostaje w teren nieznaną szynę Modbus RTU
i chce szybko sprawdzić: które adresy odpowiadają, przy jakim baudrate, oraz co może
oznaczać wartość rejestru.

## Hardware
- Waveshare ESP32-S3-Touch-LCD-4 (ESP32-S3-N16R8, 16 MB flash, 8 MB PSRAM)
- Panel: ST7701 RGB 480x480
- Touch: GT911 I2C (SDA=15, SCL=7, INT=4)
- RS-485: SP3485 na UART2, RX=43, TX=44 (auto direction)
- MicroSD slot (SPI: CS=10, MOSI=11, CLK=12, MISO=13)
- CAN: TJA1051T (poza zakresem fazy 1)

## Architecture
```
ESP32-S3 (firmware /app/firmware — PlatformIO + Arduino)
├── UART2 (SP3485) ── Modbus RTU master (emelianov/modbus-esp8266)
├── LVGL 8.3 na RGB panel + GT911 (ESP32_Display_Panel)
├── WiFi AP+STA + ESPAsyncWebServer (REST + LittleFS static WebUI)
├── microSD (CSV log + eksport skanów)
└── Preferences (NVS) do persystencji ustawień

Backend FastAPI (/app/backend — chmura Emergent, publiczny)
├── /api/ai/chat        (SSE streaming)
├── /api/ai/analyze-scan (SSE streaming)
├── /api/ai/nl-command  (JSON) — NL → ramka Modbus
└── emergentintegrations + EMERGENT_LLM_KEY → gpt-5.6-terra
```

## Core requirements (static)
1. Skan Modbus RTU zakres 1-247, konfigurowalne baud/parity/stop
2. Auto-detect baudrate (iteracja przez 4800..115200 8N1)
3. Odczyt rejestrów typu Holding/Input/Coil/Discrete, start/count konfigurowalne
4. LVGL UI z 3 zakładkami (Skaner / Ustawienia / Rejestry)
5. WiFi WebUI (AP fallback), responsywny HTML
6. SD logowanie wyników skanów (CSV)
7. AI integration: streaming assistant + auto-analyze + NL→Modbus command

## Implemented (2026-01)
- [x] Pełny firmware PlatformIO (/app/firmware/) kompilowalny dla waveshare board
- [x] Modbus RTU master z probingiem slave po holding reg 0, treats Modbus exception as "present"
- [x] LVGL 3-zakładkowy UI, dark theme, polskie etykiety
- [x] AP + STA WiFi, REST API: /api/status, /api/scan, /api/settings (GET/POST), /api/read
- [x] WebUI (HTML/JS/CSS) serwowany z LittleFS
- [x] Persystencja ustawień w Preferences (NVS)
- [x] Logowanie na microSD (/logs/*.csv)
- [x] Backend FastAPI z 3 endpointami AI (gpt-5.6-terra przez Emergent Universal Key)
- [x] WebUI zakładka "AI Asystent" ze streamingiem SSE
- [x] Autoanaliza po skanie (checkbox w WebUI)
- [x] NL command → auto-wypełnienie zakładki Rejestry + Wykonaj
- [x] Smoke test wszystkich AI endpointów (200 OK, streaming OK)

## Prioritized backlog
- [ ] **P1** CAN bus skaner (TWAI driver + TJA1051T), analogiczna zakładka LCD/WebUI
- [ ] **P1** Zapis rejestrów (FC 0x06, 0x10) z confirm dialog
- [ ] **P2** Harmonogram cyklicznych odczytów + wykres LVGL (chart widget)
- [ ] **P2** Eksport danych MQTT lub InfluxDB
- [ ] **P2** Baza znanych map rejestrów (SDM630, Growatt, SMA, etc.) używana przez AI do konkretniejszych odpowiedzi
- [ ] **P2** Firmware OTA przez WebUI

## Testing
- Backend AI endpoints: curl smoke test (passed)
- Firmware: wymaga fizycznej płytki Waveshare do testów end-to-end — brak możliwości
  testowania w środowisku Emergent. Dokumentacja w `/app/firmware/README.md`.

## Credentials
EMERGENT_LLM_KEY w /app/backend/.env (ustawiony).
WiFi SSID/hasło konfigurowane przez użytkownika w WebUI po pierwszym bootcie.
