#include "hal_sd.h"

#include "config.h"

#include <SD.h>
#include <SPI.h>

namespace {

SPIClass sd_spi(VSPI);
bool sd_mounted = false;

} // namespace

bool hal_sd_init(void)
{
    if (sd_mounted) {
        return true;
    }

    pinMode(HwConfig::SdCard::CS_PIN, OUTPUT);
    digitalWrite(HwConfig::SdCard::CS_PIN, HIGH);

    sd_spi.begin(HwConfig::SdCard::SCLK_PIN,
                 HwConfig::SdCard::MISO_PIN,
                 HwConfig::SdCard::MOSI_PIN,
                 HwConfig::SdCard::CS_PIN);

    sd_mounted = SD.begin(HwConfig::SdCard::CS_PIN,
                          sd_spi,
                          HwConfig::SdCard::SPI_FREQUENCY_HZ,
                          "/sd",
                          5,
                          false);

    if (!sd_mounted) {
        Serial.println("SD: mount failed; boot will continue without SD assets.");
        return false;
    }

    const uint8_t card_type = SD.cardType();
    if (card_type == CARD_NONE) {
        sd_mounted = false;
        Serial.println("SD: no card detected after mount.");
        SD.end();
        return false;
    }

    Serial.printf("SD: mounted, type=%u, size=%llu MB\n",
                  static_cast<unsigned>(card_type),
                  static_cast<unsigned long long>(SD.cardSize() / (1024ULL * 1024ULL)));
    return true;
}

bool hal_sd_is_mounted(void)
{
    return sd_mounted;
}

uint64_t hal_sd_card_size_bytes(void)
{
    return sd_mounted ? SD.cardSize() : 0ULL;
}
