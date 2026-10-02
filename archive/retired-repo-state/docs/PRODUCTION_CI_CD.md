# CI/CD AquaCYD

## Pull request i main

Workflow wykonuje skan sekretów i zależności, lint workflow, testy narzędzi,
testy WWW/API, budowę pakietu SD, testy domeny i kompilację obu wariantów CYD.
Nie ma zadań Flutter, Android, P4, C6 ani ESP-NOW.

## Wydanie

Tag `firmware-vX.Y.Z` musi wskazywać commit osiągalny z `main`. Następnie:

1. przechodzi fizyczny HIL dokładnego commitu;
2. wykonywane są testy hostowe;
3. budowane są ILI9341 i ST7789;
4. obrazy są podpisywane RSA-3072 i pakowane do `.aqfw`;
5. budowany jest deterministyczny pakiet WWW;
6. generowane są manifesty, SHA-256, SBOM i provenance;
7. GitHub Release jest tworzony tylko, jeśli tag jeszcze nie istnieje.

Brak sekretu podpisującego, HIL lub któregokolwiek artefaktu zatrzymuje
publikację.
