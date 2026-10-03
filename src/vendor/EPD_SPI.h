#ifndef _EPD_SPI_H_
#define _EPD_SPI_H_

#include <Arduino.h>
#include <soc/gpio_reg.h>

//项目板子
#define SCK 12
#define MOSI 11
#define RES 47
#define DC 46
#define CS 45
#define BUSY 48

//#define SCK 12
//#define MOSI 11
//#define RES 21
//#define DC 9
//#define CS 10
//#define BUSY 48

// SCK/MOSI/DC/CS are toggled ~26 times per byte, so they write the GPIO
// set/clear registers directly instead of going through digitalWrite().
// GPIO 0-31 live in GPIO_OUT, 32-48 in GPIO_OUT1.
#define EPD_PIN_SET(pin) REG_WRITE((pin) < 32 ? GPIO_OUT_W1TS_REG : GPIO_OUT1_W1TS_REG, 1UL << ((pin) & 31))
#define EPD_PIN_CLR(pin) REG_WRITE((pin) < 32 ? GPIO_OUT_W1TC_REG : GPIO_OUT1_W1TC_REG, 1UL << ((pin) & 31))

#define EPD_SCK_Clr() EPD_PIN_CLR(SCK)
#define EPD_SCK_Set() EPD_PIN_SET(SCK)

#define EPD_MOSI_Clr() EPD_PIN_CLR(MOSI)
#define EPD_MOSI_Set() EPD_PIN_SET(MOSI)

#define EPD_RES_Clr() digitalWrite(RES, LOW)
#define EPD_RES_Set() digitalWrite(RES, HIGH)

#define EPD_DC_Clr() EPD_PIN_CLR(DC)
#define EPD_DC_Set() EPD_PIN_SET(DC)

#define EPD_CS_Clr() EPD_PIN_CLR(CS)
#define EPD_CS_Set() EPD_PIN_SET(CS)

#define EPD_ReadBUSY digitalRead(BUSY)

void EPD_GPIOInit(void);
void EPD_WR_Bus(uint8_t dat);
void EPD_WR_REG(uint8_t reg);
void EPD_WR_DATA8(uint8_t dat);
void EPD_WR_DATA(const uint8_t *buf, uint32_t len);
void SPI_Write(unsigned char value);

#endif
