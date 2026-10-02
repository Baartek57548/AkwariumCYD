---
aliases:
  - Pływak Poziomu Wody
tags:
  - hardware
  - sensors
  - safety
category: hardware
status: complete
---

# 💧 Hardware — Czujnik Poziomu Wody

W bieżącej konfiguracji AquaCYD dwa pływaki poziomu wody są podłączone do Portu B ekspandera [[Hardware - Ekspander MCP23017|MCP23017]]: MIN na `GPB2` i MAX na `GPB1`. Oba wejścia mają podciąganie; konfiguracja firmware zakłada, że aktywny kontakt zwiera wejście do masy.

Powrót do: [[MOC - Sprzęt i Peryferia]]

---

## Pływak Poziomu Wody MIN (Pin GPB2)

- **Konstrukcja**: kontaktronowy czujnik pływakowy w komorze pompy lub akwarium.
- **Stan prawidłowy**: obwód rozwarty, a wejście utrzymywane przez pull-up w stanie logicznym `1`.
- **Niski poziom**: opuszczony pływak zwiera wejście do masy, dając stan logiczny `0`.
- **Reakcja**: ustawienie `ALARM_WATER_LOW` i pokazanie komunikatu operatorowi.

## Pływak Poziomu Wody MAX (Pin GPB1)

- **Stan nieaktywny**: obwód rozwarty, wejście w stanie logicznym `1`.
- **Poziom MAX**: zwarty kontakt daje stan logiczny `0`.
- **Telemetria**: `w_max=true` oznacza osiągnięty poziom MAX; `w_max_valid=false` oznacza brak wiarygodnego odczytu, a nie poziom poniżej MAX.
- **Reakcja**: stan MAX jest normalną granicą dolewki, bez alarmu przelewu. Logiczny warunek ATO uwzględnia MAX, lecz bieżący sterownik wykonawczy nie ma jeszcze przypisanego kanału pompy dolewki ani fizycznego odcięcia tej pompy.

Przed uruchomieniem trzeba potwierdzić orientację obu pływaków na rzeczywistym zbiorniku. Przy obecnym połączeniu NO przerwa przewodu jest widziana jak nieaktywny pływak; zabezpieczenie przed przerwą wymaga osobno zaprojektowanego obwodu NC i zmiany polaryzacji w firmware.

---

## Powiązane Notatki

- [[Hardware - Ekspander MCP23017]]
- [[Firmware - WeMos Hardware I2C]]
- [[MOC - Bezpieczeństwo i Fail-Safe]]
