#ifndef LMX2820_H
#define LMX2820_H


#define PLL_CS         3   // Chip Select Pin for PLL
#define SCLK_           12   // SPI Clock Pin
#define MOSI_           11   // SPI MOSI Pin
#define MISO_           13   // SPI MISO Pin

void Lmx2820SetOUTA_PWR(uint8_t OUTA_PWR);
void Lmx2820SetOUTB_PWR(uint8_t OUTB_PWR);
bool Lmx2820SetFreqinMHz(double target_freq , double ref_clock , bool isFilteredChA);
void Lmx2820SetFreqinMHz_Fast(double target_freq , double ref_clock);
void PLL_write(uint8_t address, uint16_t Data);
uint16_t PLL_read(uint8_t address);
void InitPLL();
bool isPLL_Locked();
void SetPLLToDefault();
void DumpPLLRegisters();
// VCO limits
#define VCO_MIN 5650000000  // 5.65 GHz
#define VCO_MAX 11300000000 // 11.3 GHz

#define  MIN_FREQ 150000000
#define  MAX_FREQ 22600000000

#endif
