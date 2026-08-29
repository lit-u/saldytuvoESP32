#include "io_extension.h"
#include <Wire.h>

static TwoWire *s_wire = nullptr;
static uint8_t s_last_output = 0;

static void writeReg(uint8_t reg, uint8_t value) {
    s_wire->beginTransmission(IO_EXTENSION_I2C_ADDR);
    s_wire->write(reg);
    s_wire->write(value);
    s_wire->endTransmission(true);
}

static uint8_t readReg(uint8_t reg) {
    s_wire->beginTransmission(IO_EXTENSION_I2C_ADDR);
    s_wire->write(reg);
    if (s_wire->endTransmission(false) != 0) return 0;
    s_wire->requestFrom(IO_EXTENSION_I2C_ADDR, (uint8_t)1);
    return s_wire->available() ? s_wire->read() : 0;
}

void IO_EXTENSION_Init(TwoWire &wire) {
    s_wire = &wire;
    // Visi 8 kanalai kaip OUTPUT — atitinka patikrinta Waveshare pavyzdi
    // (IO_EXTENSION_IO_Mode(0xff) ju "01_lvgl_example/io_extension.cpp").
    writeReg(IO_EXTENSION_REG_MODE, 0xFF);
    s_last_output = 0;
}

void IO_EXTENSION_Output(uint8_t pin, uint8_t value) {
    if (value) s_last_output |= (1 << pin);
    else       s_last_output &= ~(1 << pin);
    writeReg(IO_EXTENSION_REG_OUTPUT, s_last_output);
}

uint8_t IO_EXTENSION_Input(uint8_t pin) {
    uint8_t value = readReg(IO_EXTENSION_REG_INPUT);
    return (value & (1 << pin)) ? 1 : 0;
}

void IO_EXTENSION_Pwm_Output(uint8_t percent) {
    // Waveshare pavyzdys apriboja iki 97%, kad ekranas visiskai neuztemtu
    // (galimas draiverio/aparaturos ypatumas — palikta kaip patikrinta).
    if (percent >= 97) percent = 97;
    uint8_t duty = (uint8_t)(percent * (255 / 100.0));
    writeReg(IO_EXTENSION_REG_PWM, duty);
}

uint16_t IO_EXTENSION_Adc_Input() {
    s_wire->beginTransmission(IO_EXTENSION_I2C_ADDR);
    s_wire->write(IO_EXTENSION_REG_ADC);
    if (s_wire->endTransmission(false) != 0) return 0;
    s_wire->requestFrom(IO_EXTENSION_I2C_ADDR, (uint8_t)2);
    if (s_wire->available() < 2) return 0;
    uint8_t lo = s_wire->read();
    uint8_t hi = s_wire->read();
    return (uint16_t)(hi << 8 | lo);
}
