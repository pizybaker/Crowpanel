#include "EPD_SPI.h"
#include <SPI.h>
#include <esp_cpu.h>

namespace {

// Minimum SCK half-period: register writes alone could clock far faster than
// the controller accepts, so cap SCK near 5 MHz. Sized for the S3's top
// 240 MHz clock; a slower CPU clock only lengthens it.
constexpr uint32_t HALF_PERIOD_CYCLES = 240 * 100 / 1000;  // 100 ns

inline void halfPeriod() {
  const uint32_t start = esp_cpu_get_cycle_count();
  while (esp_cpu_get_cycle_count() - start < HALF_PERIOD_CYCLES) {
  }
}

}  // namespace

void EPD_GPIOInit(void)
{
  pinMode(SCK, OUTPUT);
  pinMode(MOSI, OUTPUT);
  pinMode(RES, OUTPUT);
  pinMode(DC, OUTPUT);
  pinMode(CS, OUTPUT);
  pinMode(BUSY, INPUT);
  digitalWrite(CS, HIGH);
  digitalWrite(DC, HIGH);
}

/**
   @brief       IO模拟SPI发送一个字节数据
   @param       dat: 需要发送的字节数据
   @retval      无
*/
void EPD_WR_Bus(uint8_t dat)
{
  uint8_t i;
  EPD_CS_Clr();
  for (i = 0; i < 8; i++)
  {
    EPD_SCK_Clr();
    if (dat & 0x80)
    {
      EPD_MOSI_Set();
    }
    else
    {
      EPD_MOSI_Clr();
    }
    halfPeriod();
    EPD_SCK_Set();
    halfPeriod();
    dat <<= 1;
  }
  EPD_CS_Set();
}

void SPI_Write(unsigned char value)
{
  SPI.transfer(value);
}

/**
   @brief       向液晶写寄存器命令
   @param       reg: 要写的命令
   @retval      无
*/
void EPD_WR_REG(uint8_t reg)
{
  EPD_DC_Clr();
  EPD_WR_Bus(reg);
//  SPI_Write(reg);
  EPD_DC_Set();
}

/**
   @brief       向液晶写一个字节数据
   @param       dat: 要写的数据
   @retval      无
*/
void EPD_WR_DATA8(uint8_t dat)
{
  EPD_DC_Set();
  EPD_WR_Bus(dat);
//  SPI_Write(dat);
  EPD_DC_Set();
}

// DC stays HIGH after every command, so a data burst only clocks bytes.
void EPD_WR_DATA(const uint8_t *buf, uint32_t len)
{
  EPD_DC_Set();
  for (uint32_t i = 0; i < len; i++) EPD_WR_Bus(buf[i]);
}
