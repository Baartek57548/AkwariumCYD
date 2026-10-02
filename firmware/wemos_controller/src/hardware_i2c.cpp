#include "hardware_i2c.h"
#include "wemos_config.h"
#include <Wire.h>

namespace {

// MCP23017 Rejestry
constexpr uint8_t REG_IODIRA = 0x00;
constexpr uint8_t REG_IODIRB = 0x01;
constexpr uint8_t REG_GPPUA  = 0x0C;
constexpr uint8_t REG_GPPUB  = 0x0D;
constexpr uint8_t REG_GPIOA  = 0x12;
constexpr uint8_t REG_GPIOB  = 0x13;
constexpr uint8_t REG_OLATA  = 0x14;
constexpr uint8_t REG_OLATB  = 0x15;

// ADS1115 Rejestry i konfiguracja
constexpr uint8_t ADS_REG_CONVERT = 0x00;
constexpr uint8_t ADS_REG_CONFIG  = 0x01;

// Konwersja BCD na binarny (dla RTC DS3231)
inline uint8_t bcd2bin(uint8_t val) {
    return val - 6 * (val >> 4);
}

inline uint8_t bin2bcd(uint8_t val) {
    return val + 6 * (val / 10);
}

bool is_digital_input_active(uint8_t raw_port,
                             uint8_t channel,
                             bool active_low) {
    const bool level_high =
        (raw_port & static_cast<uint8_t>(1U << channel)) != 0U;
    return active_low ? !level_high : level_high;
}

// Konwersja czasu do epoch (prosta kalkulacja UTC)
uint32_t to_epoch(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    // Liczba dni w miesiącach
    static const uint8_t days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) return 0;

    uint32_t days = 0;
    for (uint16_t y = 1970; y < year; ++y) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }
    for (uint8_t m = 1; m < month; ++m) {
        days += days_in_month[m - 1];
        if (m == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
            days += 1;
        }
    }
    days += (day - 1);
    return ((days * 24UL + hour) * 60UL + minute) * 60UL + second;
}

} // namespace

HardwareI2C g_hardware_i2c;

HardwareI2C::HardwareI2C()
    : relay_state_mask_(0),
      physical_shadow_a_(0xFF),
      water_level_low_(false),
      water_level_max_reached_(false),
      water_level_inputs_valid_(false),
      mcp_ok_(false),
      ads_ok_(false),
      rtc_ok_(false)
{
}

bool HardwareI2C::begin()
{
    Wire.begin(WemosConfig::PIN_I2C_SDA, WemosConfig::PIN_I2C_SCL);
    Wire.setClock(100000); // 100 kHz standard mode

    // 1. Inicjalizacja MCP23017
    Wire.beginTransmission(WemosConfig::ADDR_MCP23017);
    if (Wire.endTransmission() == 0) {
        mcp_ok_ = true;
        // Port A jako wyjścia (przekaźniki), Port B jako wejścia (czujniki)
        writeMcpRegister(REG_IODIRA, 0x00); // Wyjścia
        writeMcpRegister(REG_IODIRB, 0xFF); // Wejścia
        writeMcpRegister(REG_GPPUA, 0x00);
        writeMcpRegister(REG_GPPUB, 0xFF);  // Włączone pull-upy dla wejść
        // Stan bezpieczny przekaźników (active-low: 0xFF = wszystkie wyłączone)
        physical_shadow_a_ = 0xFF;
        writeMcpRegister(REG_OLATA, physical_shadow_a_);
        writeMcpRegister(REG_GPIOA, physical_shadow_a_);
        relay_state_mask_ = 0;
    } else {
        mcp_ok_ = false;
    }

    // 2. Inicjalizacja ADS1115
    Wire.beginTransmission(WemosConfig::ADDR_ADS1115);
    ads_ok_ = (Wire.endTransmission() == 0);

    // 3. Inicjalizacja DS3231 RTC
    Wire.beginTransmission(WemosConfig::ADDR_DS3231);
    rtc_ok_ = (Wire.endTransmission() == 0);

    return mcp_ok_;
}

bool HardwareI2C::writeMcpRegister(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(WemosConfig::ADDR_MCP23017);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

bool HardwareI2C::readMcpRegister(uint8_t reg, uint8_t &val)
{
    Wire.beginTransmission(WemosConfig::ADDR_MCP23017);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(static_cast<int>(WemosConfig::ADDR_MCP23017), 1) != 1) {
        return false;
    }
    val = Wire.read();
    return true;
}

bool HardwareI2C::setRelay(uint8_t channel, bool on)
{
    if (channel >= 8) return false;

    if (on) {
        relay_state_mask_ |= (1U << channel);
    } else {
        relay_state_mask_ &= ~(1U << channel);
    }

    // Active-low: 1 logiczne = 0 fizyczne na wyjściu MCP
    if (WemosConfig::RELAY_ACTIVE_LOW) {
        physical_shadow_a_ = ~relay_state_mask_;
    } else {
        physical_shadow_a_ = relay_state_mask_;
    }

    bool ok = writeMcpRegister(REG_OLATA, physical_shadow_a_);
    mcp_ok_ = ok;
    return ok;
}

bool HardwareI2C::setAllRelays(uint8_t bitmask)
{
    relay_state_mask_ = bitmask;
    if (WemosConfig::RELAY_ACTIVE_LOW) {
        physical_shadow_a_ = ~relay_state_mask_;
    } else {
        physical_shadow_a_ = relay_state_mask_;
    }
    bool ok = writeMcpRegister(REG_OLATA, physical_shadow_a_);
    mcp_ok_ = ok;
    return ok;
}

bool HardwareI2C::readDigitalInputs(uint8_t &inputs)
{
    uint8_t raw_b = 0;
    if (!readMcpRegister(REG_GPIOB, raw_b)) {
        mcp_ok_ = false;
        water_level_inputs_valid_ = false;
        return false;
    }
    mcp_ok_ = true;
    water_level_inputs_valid_ = true;
    inputs = raw_b;

    // Port B ma pull-upy, więc zwarcie kontaktu do masy jest stanem aktywnym.
    // MIN i MAX pozostają rozdzielne: nie można używać jednego pola dla obu
    // progów, bo górny pływak stanowi osobny interlock dolewki.
    water_level_low_ = is_digital_input_active(
        raw_b,
        WemosConfig::INPUT_CH_WATER_LEVEL_MIN,
        WemosConfig::WATER_LEVEL_MIN_ACTIVE_LOW);
    water_level_max_reached_ = is_digital_input_active(
        raw_b,
        WemosConfig::INPUT_CH_WATER_LEVEL_MAX,
        WemosConfig::WATER_LEVEL_MAX_ACTIVE_LOW);

    return true;
}

int16_t HardwareI2C::readAdsRaw(uint8_t channel)
{
    if (!ads_ok_ || channel > 3) return 0;

    // Konfiguracja ADS1115: Single shot, FSR +-4.096V, 128 SPS
    // MUX: 100=AIN0, 101=AIN1, 110=AIN2, 111=AIN3
    uint16_t config = 0x8000 | ((0x04 | channel) << 12) | (0x01 << 9) | (0x04 << 5) | 0x03;

    Wire.beginTransmission(WemosConfig::ADDR_ADS1115);
    Wire.write(ADS_REG_CONFIG);
    Wire.write(static_cast<uint8_t>(config >> 8));
    Wire.write(static_cast<uint8_t>(config & 0xFF));
    if (Wire.endTransmission() != 0) {
        ads_ok_ = false;
        return 0;
    }

    // Oczekiwanie na koniec konwersji (ok. 8 ms przy 128 SPS)
    delay(10);

    Wire.beginTransmission(WemosConfig::ADDR_ADS1115);
    Wire.write(ADS_REG_CONVERT);
    if (Wire.endTransmission(false) != 0) {
        ads_ok_ = false;
        return 0;
    }

    if (Wire.requestFrom(static_cast<int>(WemosConfig::ADDR_ADS1115), 2) != 2) {
        ads_ok_ = false;
        return 0;
    }

    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    ads_ok_ = true;
    return static_cast<int16_t>((msb << 8) | lsb);
}

float HardwareI2C::readPh(float cal_offset, float cal_slope)
{
    int16_t raw = readAdsRaw(0); // AIN0
    // FSR 4.096V -> 4096mV / 32768 = 0.125 mV per LSB
    float voltage = static_cast<float>(raw) * 0.000125f;
    // Standardowa liniowa formuła dla sondy pH: pH = 7.0 + (V_ref - V) * slope + offset
    float ph = 7.0f + (2.5f - voltage) * cal_slope + cal_offset;
    if (ph < 0.0f) ph = 0.0f;
    if (ph > 14.0f) ph = 14.0f;
    return ph;
}

float HardwareI2C::readEc()
{
    int16_t raw = readAdsRaw(1); // AIN1
    float voltage = static_cast<float>(raw) * 0.000125f;
    // Przeliczenie napięcia na przewodność us/cm (szacunkowo)
    float ec = voltage * 1000.0f;
    return (ec >= 0.0f) ? ec : 0.0f;
}

uint32_t HardwareI2C::getEpochTime()
{
    if (!rtc_ok_) {
        // Fallback jeśli RTC niedostępny - czas względny od startu WeMos
        return 1725960000UL + (millis() / 1000UL);
    }

    Wire.beginTransmission(WemosConfig::ADDR_DS3231);
    Wire.write(0x00); // Rejestr sekund
    if (Wire.endTransmission(false) != 0) {
        rtc_ok_ = false;
        return 1725960000UL + (millis() / 1000UL);
    }

    if (Wire.requestFrom(static_cast<int>(WemosConfig::ADDR_DS3231), 7) != 7) {
        rtc_ok_ = false;
        return 1725960000UL + (millis() / 1000UL);
    }

    uint8_t ss = bcd2bin(Wire.read() & 0x7F);
    uint8_t mm = bcd2bin(Wire.read());
    uint8_t hh = bcd2bin(Wire.read() & 0x3F);
    Wire.read(); // day of week (ignorujemy)
    uint8_t d  = bcd2bin(Wire.read());
    uint8_t m  = bcd2bin(Wire.read() & 0x1F);
    uint16_t y = 2000U + bcd2bin(Wire.read());

    return to_epoch(y, m, d, hh, mm, ss);
}

bool HardwareI2C::setEpochTime(uint32_t epoch)
{
    // Prosta konwersja epoch -> data i czas
    uint32_t raw_sec = epoch % 86400UL;
    uint8_t ss = raw_sec % 60UL;
    uint8_t mm = (raw_sec / 60UL) % 60UL;
    uint8_t hh = raw_sec / 3600UL;

    uint32_t days = epoch / 86400UL;
    uint16_t y = 1970;
    while (true) {
        bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        uint16_t y_days = leap ? 366 : 365;
        if (days < y_days) break;
        days -= y_days;
        ++y;
    }

    static const uint8_t days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    uint8_t m = 1;
    for (; m <= 12; ++m) {
        uint8_t d_in_m = days_in_month[m - 1];
        if (m == 2 && leap) d_in_m = 29;
        if (days < d_in_m) break;
        days -= d_in_m;
    }
    uint8_t d = days + 1;

    Wire.beginTransmission(WemosConfig::ADDR_DS3231);
    Wire.write(0x00); // Rejestr sekund
    Wire.write(bin2bcd(ss));
    Wire.write(bin2bcd(mm));
    Wire.write(bin2bcd(hh));
    Wire.write(bin2bcd(1)); // Dzień tygodnia (1..7)
    Wire.write(bin2bcd(d));
    Wire.write(bin2bcd(m));
    Wire.write(bin2bcd(static_cast<uint8_t>(y >= 2000 ? y - 2000 : 0)));
    bool ok = (Wire.endTransmission() == 0);
    rtc_ok_ = ok;
    return ok;
}

bool HardwareI2C::isRtcRunning()
{
    return rtc_ok_;
}
