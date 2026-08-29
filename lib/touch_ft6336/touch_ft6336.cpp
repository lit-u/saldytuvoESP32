#include "touch_ft6336.h"
#include "io_extension.h"

static TwoWire *s_wire = nullptr;

void Touch_FT6336_Init(TwoWire &wire) {
    s_wire = &wire;
    pinMode(TOUCH_FT6336_INT_PIN, INPUT);

    // Reset seka (patikrinta: touch_driver.cpp -> Touch_Reset()):
    // P0 = 0 -> palaukti -> P0 = 1 -> palaukti.
    IO_EXTENSION_Output(IO_EXTENSION_TOUCH_RST_PIN, 0);
    delay(50);
    IO_EXTENSION_Output(IO_EXTENSION_TOUCH_RST_PIN, 1);
    delay(50);
}

TouchPoint Touch_FT6336_Read() {
    TouchPoint result = {false, 0, 0};

    s_wire->beginTransmission(TOUCH_FT6336_I2C_ADDR);
    s_wire->write((uint8_t)0x02);
    if (s_wire->endTransmission(false) != 0) return result;
    s_wire->requestFrom(TOUCH_FT6336_I2C_ADDR, (uint8_t)1);
    if (s_wire->available() < 1) return result;
    uint8_t points = s_wire->read() & 0x0F;
    if (points == 0) return result;

    s_wire->beginTransmission(TOUCH_FT6336_I2C_ADDR);
    s_wire->write((uint8_t)0x03);
    if (s_wire->endTransmission(false) != 0) return result;
    s_wire->requestFrom(TOUCH_FT6336_I2C_ADDR, (uint8_t)4);
    if (s_wire->available() < 4) return result;

    uint8_t xh = s_wire->read();
    uint8_t xl = s_wire->read();
    uint8_t yh = s_wire->read();
    uint8_t yl = s_wire->read();

    result.touched = true;
    result.x = ((uint16_t)(xh & 0x0F) << 8) | xl;
    result.y = ((uint16_t)(yh & 0x0F) << 8) | yl;
    return result;
}
