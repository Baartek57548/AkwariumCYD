# Historyczne artefakty binarne

Ten katalog przechowuje wyłącznie metadane wydań, instrukcje i sumy SHA-256. Pliki APK oraz obrazy firmware zostały usunięte z bieżącego drzewa Git 2026-08-12, ponieważ są odtwarzalnymi wynikami kompilacji, a nie kodem źródłowym.

## Odzyskanie aplikacji

| Plik | SHA-256 | Źródło odzyskania |
| --- | --- | --- |
| `AquaCYD-Control-3.4.0-current.apk` | `04d15c04c89421226968210b679d3f693a53de41b4c5625d72115f5e440a3a40` | GitHub Release `mobile-v3.4.0` |
| `AquaCYD-DEV-3.4.0.apk` | `6d8bc9447db46dc58fe38e7fab472a54c94a0fa3b2d7fb2d53b3d7cdd3f0ceb1` | GitHub Release `mobile-v3.4.0` |
| `AquaCYD-Full-3.4.0.apk` | `aa77bf320ec4bcdcf28b98a22ebef14e1265d0505239b36425ce8f51fdb389cb` | GitHub Release `mobile-v3.4.0` |
| `AquaCYD-Control-4.0.0-current.apk` | `6400127dd73e86cc0a14101a368203979b01f6e5fe7c3b9a3044db6c8657b7c7` | GitHub Release `mobile-v4.0.0` |
| `AquaCYD-Control-4.0.1-current.apk` | `591f2c3008302291cc8a467aa8e99b1d32f877e495774e3152cdca57ec3991a0` | GitHub Release `mobile-v4.0.1` |

Po pobraniu pliku należy zweryfikować jego SHA-256 względem śledzonych plików `.sha256`. Link do odpowiedniego release jest źródłem dystrybucyjnym; repozytorium nie przechowuje ponownie tego samego binarium.

## Odzyskanie firmware 2026.07.26

Obrazy `cydAquarium-CYD-2026.07.26-ILI9341.bin` i `cydAquarium-CYD-2026.07.26-ST7789.bin` nie występują jako osobne zasoby aktualnych GitHub Releases. Pozostają dostępne w historii Git, a zalecaną metodą jest odtworzenie ich z właściwego historycznego commita i przypiętego toolchainu PlatformIO. Oczekiwane sumy znajdują się w sąsiednich plikach `.sha256`.

Aktualnych obrazów nie należy kopiować do tego katalogu. Pipeline release publikuje binaria jako zasoby GitHub Release razem z sumami kontrolnymi i SBOM.

