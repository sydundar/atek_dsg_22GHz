/*
 * LMX2820 PLL control implementation.
 * This file configures PLL output power, frequency synthesis, SPI register access,
 * default register loading, lock detection, and register dump diagnostics.
 */
#include "main.h"
#include "Lmx2820.h"

#define PLL_DEN 1000 
#define OSC_2X 2  
#define MULT 1
#define PLL_R_PRE 1
#include <SPI.h>

uint32_t OUTA_MUX, OUTB_MUX;
uint32_t OUTA_PD, OUTB_PD;
uint8_t OUTA_PWR=7,OUTB_PWR=7;

// R79: OUTA power control register.
void Lmx2820SetOUTA_PWR(uint8_t OUTA_PWR_i)
{
  OUTA_PWR = OUTA_PWR_i;
  if (OUTA_PWR > 7) OUTA_PWR = 7;              
  uint32_t R79 = (OUTB_PD << 8) | (OUTB_MUX << 4) | ((OUTA_PWR & 0x7u) << 1);
  PLL_write(0x4F, R79);                   
}

// R80: OUTB power control register.
void Lmx2820SetOUTB_PWR(uint8_t OUTB_PWR_i)
{
  OUTB_PWR = OUTB_PWR_i;
  if (OUTB_PWR > 7) OUTB_PWR = 7;                 
  uint32_t R80 =   ((OUTB_PWR & 0x7u) << 6);
  PLL_write(0x50, R80);     
}


/*
 * Configures the LMX2820 output frequency in MHz.
 * The function selects the required VCO path, output divider, channel mux,
 * and PLL integer/fractional divider registers, then checks PLL lock status.
 */
bool Lmx2820SetFreqinMHz(double target_freq , double ref_clock , bool isFilteredChA)  
{
	target_freq = target_freq*1000000;
  char buf[64];
  snprintf(buf, sizeof(buf), "Set Freq(Hz): %.0f", target_freq);
  Serial.println(buf);

	//char buf[32];
  snprintf(buf, sizeof(buf), "Freq: %.0f", target_freq);
  //Serial.println(buf);
	//Serial.println("\r\n");

	   uint8_t output_divider = 1;
	    double vco_frequency = target_freq * output_divider;
	    uint8_t DBLR_CAL_EN = 0;

	    // If the target frequency is above the VCO maximum, use the doubler.
	    if (target_freq > VCO_MAX) {
	        vco_frequency = target_freq / 2;
	        DBLR_CAL_EN = 1;
	    }

	    // If the target frequency is below the VCO minimum, find a suitable output divider.
	    if (target_freq < VCO_MIN) {
	        for (int i = 7; i >= 1; i--) {
	            if ((target_freq * pow(2, i)) < VCO_MAX) {
	                output_divider = (1 << i);
	                break;
	            }
	        }
	        vco_frequency = target_freq * output_divider;
	    }

	    // PFD frequency calculation.
	    double PFD_FREQUENCY = (ref_clock * OSC_2X * MULT) / (PLL_R_PRE);

	    // Calculate the N-divider integer and fractional components.
	    double N_divider_exact = vco_frequency / PFD_FREQUENCY;
	    uint32_t PLL_N = (uint32_t)N_divider_exact;  // Integer component.

	    // Precise fractional calculation.
	    double N_fractional = round((N_divider_exact - PLL_N) * PLL_DEN) / PLL_DEN;
	    uint32_t PLL_NUM = (uint32_t)(N_fractional * PLL_DEN);
	    double N_fractional_final = (double)PLL_NUM / PLL_DEN;

 
    uint32_t CHDIVA,CHDIVB;

    switch (output_divider) {
        case 2:   CHDIVA = 0; break;
        case 4:   CHDIVA = 1; break;
        case 8:   CHDIVA = 2; break;
        case 16:  CHDIVA = 3; break;
        case 32:  CHDIVA = 4; break;
        case 64:  CHDIVA = 5; break;
        case 128: CHDIVA = 6; break;
        default:  CHDIVA = 7;  
                  break;
    }

  CHDIVB = CHDIVA;

    if (target_freq < VCO_MIN) 
    {
        OUTA_MUX = 0x0; // Channel divider
      
    }
    else if (target_freq >= VCO_MIN && target_freq <= VCO_MAX)
    {
        OUTA_MUX = 0x1;// VCO
    }
    else
    {
        OUTA_MUX = 0x2;// Doubler
    }

    OUTB_MUX = OUTA_MUX;


    if (isFilteredChA)
    {
      OUTA_PD = 0; // 0 means Powered UP
      OUTB_PD = 1; 
    } 
    else
    {
      OUTA_PD = 1; // 0 means Powered UP
      OUTB_PD = 0; 
    }

    uint32_t R78 = (OUTA_PD << 4) | (OUTA_MUX);
    uint32_t R79 = (OUTB_PD << 8) | (OUTB_MUX << 4) | ((OUTA_PWR & 0x7u) << 1);
    uint32_t R80 = ((OUTB_PWR & 0x7u) << 6);

    // Register values
    uint32_t reg_N = PLL_N;
    uint32_t reg_NUM1 = (PLL_NUM >> 16) & 0xFFFF;
    uint32_t reg_NUM2 = PLL_NUM & 0xFFFF;
    uint32_t reg_DEN1 = (PLL_DEN >> 16) & 0xFFFF;
    uint32_t reg_DEN2 = PLL_DEN & 0xFFFF;

    uint32_t reg_R_DIV = ((CHDIVA) << 6) | ((CHDIVB) << 9) | 0x1001;
    uint16_t read_val;

    PLL_write(0x2C, 0x8000);

    PLL_write(0x24, reg_N);

    PLL_write(0x2B, reg_NUM2);

    PLL_write(0x50, R80); 
    PLL_write(0x4F, R79);
    PLL_write(0x4E, R78);
    

    PLL_write(0x2A, reg_NUM1);

    PLL_write(0x27, reg_DEN2);

    PLL_write(0x26, reg_DEN1);

    PLL_write(0x20, reg_R_DIV);

    PLL_write(0x00, 0x6070);
 
    delay(50);
    // Check Lock Detect
    return isPLL_Locked();
  
}

/*
 * Fast frequency update path.
 * This function updates only the frequency-related PLL registers and skips
 * the full lock-check flow used by the standard frequency configuration function.
 */
void Lmx2820SetFreqinMHz_Fast(double target_freq , double ref_clock)  
{
	target_freq = target_freq*1000000;


	   uint8_t output_divider = 1;
	    double vco_frequency = target_freq * output_divider;
	    uint8_t DBLR_CAL_EN = 0;

	    // If the target frequency is above the VCO maximum, use the doubler.
	    if (target_freq > VCO_MAX) {
	        vco_frequency = target_freq / 2;
	        DBLR_CAL_EN = 1;
	    }

	    // If the target frequency is below the VCO minimum, find a suitable output divider.
	    if (target_freq < VCO_MIN) {
	        for (int i = 7; i >= 1; i--) {
	            if ((target_freq * pow(2, i)) < VCO_MAX) {
	                output_divider = (1 << i);
	                break;
	            }
	        }
	        vco_frequency = target_freq * output_divider;
	    }

	    // PFD frequency calculation.
	    double PFD_FREQUENCY = (ref_clock * OSC_2X * MULT) / (PLL_R_PRE);

	    // Calculate the N-divider integer and fractional components.
	    double N_divider_exact = vco_frequency / PFD_FREQUENCY;
	    uint32_t PLL_N = (uint32_t)N_divider_exact;  // Integer component.

	    // Precise fractional calculation.
	    double N_fractional = round((N_divider_exact - PLL_N) * PLL_DEN) / PLL_DEN;
	    uint32_t PLL_NUM = (uint32_t)(N_fractional * PLL_DEN);
	    double N_fractional_final = (double)PLL_NUM / PLL_DEN;

 
    uint32_t CHDIVA, CHDIVB;

    switch (output_divider) {
        case 2:   CHDIVA = 0; break;
        case 4:   CHDIVA = 1; break;
        case 8:   CHDIVA = 2; break;
        case 16:  CHDIVA = 3; break;
        case 32:  CHDIVA = 4; break;
        case 64:  CHDIVA = 5; break;
        case 128: CHDIVA = 6; break;
        default:  CHDIVA = 7;  
                  break;
    }
    CHDIVB = CHDIVA;

    if (target_freq < VCO_MIN) 
    {
        OUTA_MUX = 0x0; // Channel divider
      
    }
    else if (target_freq >= VCO_MIN && target_freq <= VCO_MAX)
    {
        OUTA_MUX = 0x1;// VCO
    }
    else
    {
        OUTA_MUX = 0x2;// Doubler
    }
    
    OUTB_MUX = OUTA_MUX;

    uint32_t R78 = (OUTA_PD << 4) | OUTA_MUX;
    uint32_t R79 = (OUTB_PD << 8) | (OUTB_MUX << 4) | ((OUTA_PWR & 0x7u) << 1);
    uint32_t R80 = ((OUTB_PWR & 0x7u) << 6);
    
	    // Register values.
	    uint32_t reg_N = PLL_N;
	    uint32_t reg_NUM1 = (PLL_NUM >> 16) & 0xFFFF;
	    uint32_t reg_NUM2 = PLL_NUM & 0xFFFF;
	    uint32_t reg_DEN1 = (PLL_DEN >> 16) & 0xFFFF;
	    uint32_t reg_DEN2 = PLL_DEN & 0xFFFF;

	    uint32_t reg_R_DIV = ((CHDIVA) << 6) | ((CHDIVB) << 9) | 0x1001;
      uint16_t read_val;

      PLL_write(0x2C, 0x8000);
      PLL_write(0x24, reg_N);
      PLL_write(0x2B, reg_NUM2);

      PLL_write(0x50, R80);
      PLL_write(0x4F, R79);
      PLL_write(0x4E, R78);

      PLL_write(0x2A, reg_NUM1);
      PLL_write(0x27, reg_DEN2);
      PLL_write(0x26, reg_DEN1);
      PLL_write(0x20, reg_R_DIV);
      PLL_write(0x00, 0x6070);

  
}

/*
 * Reads the PLL lock-detect status and returns true when the PLL is locked.
 */
bool isPLL_Locked() {
    delay(1);
    uint16_t read_val = PLL_read(0x4A);
    //printf("Reg 0x4A: Read back 0x%04X\r\n", read_val);
    uint8_t rb_LD = (read_val >> 14) & 0x03;
    
    if (rb_LD == 0b10) {
        //printf("PLL LOCKED\n");
        return true;
    } else {
        //printf("PLL NOT LOCKED (rb_LD = %u)\n", rb_LD);
        return false;
    }
    
}



/*
 * Reads a 16-bit value from the selected PLL register over SPI.
 */
uint16_t PLL_read(uint8_t address) {
    address |= 0x80;  // read operation
    uint8_t spi_buf[2];

    digitalWrite(PLL_CS, LOW);  // pull the pin low
    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    SPI.transfer(&address, 1);       // send address
    SPI.transfer(spi_buf, 2);        // receive 2 bytes data
    SPI.endTransaction();
    digitalWrite(PLL_CS, HIGH);  // pull the pin high

    return (uint16_t)((spi_buf[0] << 8) | spi_buf[1]);
}

// PLL write function.
void PLL_write(uint8_t address, uint16_t Data) {
    address &= 0x7F;  // write operation
    uint8_t spi_buf[2];
    spi_buf[1] = lowByte(Data);
    spi_buf[0] = highByte(Data);

    digitalWrite(PLL_CS, LOW);  // pull the pin low
    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    SPI.transfer(&address, 1);       // send address
    SPI.transfer(spi_buf, 2);        // send data
    SPI.endTransaction();
    digitalWrite(PLL_CS, HIGH);  // pull the pin high
}



/*
 * Initializes the PLL SPI interface, verifies basic SPI read/write access,
 * and loads the default PLL register map.
 */
void InitPLL()
{

  SPI.begin(SCLK_, MISO_, MOSI_, PLL_CS);
  pinMode(PLL_CS, OUTPUT);
  digitalWrite(PLL_CS, HIGH); 

	// Test SPI accessibility using register address 63.
	PLL_write(63,0x0000); 				// Test PLL write operation.
	uint16_t dumy1_0x0000 = PLL_read(63); 	// Test PLL read operation.
	SetPLLToDefault(); // Set all registers to their default values.

	uint16_t dumy1_0xC350 = PLL_read(63); 	// Test PLL read operation.
	//
	if (dumy1_0xC350==0xC350 && dumy1_0x0000 ==0x0000)
	{
		Serial.println("PLL Initialization Successful.\r\n");
	}
	else
	{
		Serial.println("PLL Initialization Failed!!!\r\n");
	}

}

/*
 * Loads the default PLL register values in descending register order.
 */
void SetPLLToDefault()
{
	  uint16_t Regs1[123];

    Regs1[112]  = 0xFFFF;
    Regs1[110]  = 0x001F;
    Regs1[105]  = 0x000A;
    Regs1[104]  = 0x0014;
    Regs1[103]  = 0x0014;
    Regs1[102]  = 0x0028;
    Regs1[101]  = 0x03E8;
    Regs1[100]  = 0x0533;
    Regs1[99]  = 0x1989;
    Regs1[98]  = 0x1C80;
    Regs1[96]  = 0x17F8;
    Regs1[93]  = 0x1000;
    Regs1[88]  = 0x03FF;
    Regs1[87]  = 0xFF00;
    Regs1[86]  = 0x0040;
    Regs1[84]  = 0x0040;
    Regs1[83]  = 0x0F00;


	  Regs1[80]  = 0x01C0;
	  Regs1[79]  = 0x0110;
	  Regs1[78]  = 0x0000;
	  Regs1[77]  = 0x0608;
	  Regs1[76]  = 0x0000;
	  Regs1[75]  = 0x0000; // 
	  Regs1[74]  = 0x0000; // 
	  Regs1[73]  = 0x0000; //
	  Regs1[72]  = 0x0000; //
	  Regs1[71]  = 0x0000; //
	  Regs1[70]  = 0x000E;
	  Regs1[69]  = 0x0011;
	  Regs1[68]  = 0x0020;
	  Regs1[67]  = 0x1000;
	  Regs1[66]  = 0x003F;
	  Regs1[65]  = 0x0000;
	  Regs1[64]  = 0x0080;
	  Regs1[63]  = 0xC350;
	  Regs1[62]  = 0x0000;
	  Regs1[61]  = 0x03E8;
	  Regs1[60]  = 0x01F4;
	  Regs1[59]  = 0x1388;
	  Regs1[58]  = 0x0000;
	  Regs1[57]  = 0x0001;
	  Regs1[56]  = 0x0001;
	  Regs1[55]  = 0x0002;
	  Regs1[54]  = 0x0000;
	  Regs1[53]  = 0x0000;
	  Regs1[52]  = 0x0000;
	  Regs1[51]  = 0x203F;
	  Regs1[50]  = 0x0080;
	  Regs1[49]  = 0x0000;
	  Regs1[48]  = 0x4180;
	  Regs1[47]  = 0x0300;
	  Regs1[46]  = 0x0300;
	  Regs1[45]  = 0x0000;
	  Regs1[44]  = 0x0000;  // 
	  Regs1[43]  = 0x01F4;
	  Regs1[42]  = 0x0000;
	  Regs1[41]  = 0x0000;
	  Regs1[40]  = 0x0000;
	  Regs1[39]  = 0x03E8;
	  Regs1[38]  = 0x0000;
	  Regs1[37]  = 0x0500;
	  Regs1[36]  = 0x0034;
	  Regs1[35]  = 0x3100;
	  Regs1[34]  = 0x0010;
	  Regs1[33]  = 0x0000;
	  Regs1[32]  = 0x1001;
	  Regs1[31]  = 0x0401;
	  Regs1[30]  = 0xB18C;
	  Regs1[29]  = 0x318C;
	  Regs1[28]  = 0x0639;
	  Regs1[27]  = 0x8001;
	  Regs1[26]  = 0x0DB0;
	  Regs1[25]  = 0x0624;
	  Regs1[24]  = 0x0E34;
	  Regs1[23]  = 0x1102;
	  Regs1[22]  = 0xE2BF;
	  Regs1[21]  = 0x1C64;
	  Regs1[20]  = 0x272C;
	  Regs1[19]  = 0x2120;
	  Regs1[18]  = 0x0000;
	  Regs1[17]  = 0x15C0;
	  Regs1[16]  = 0x171C;
	  Regs1[15]  = 0x2001;
	  Regs1[14]  = 0x3001;
	  Regs1[13]  = 0x0038;
	  Regs1[12]  = 0x0408;
	  Regs1[11]  = 0x0612;
	  Regs1[10]  = 0x0800;
	  Regs1[9]   = 0x0005;
	  Regs1[8]   = 0xC802;
	  Regs1[7]   = 0x0000;
	  Regs1[6]   = 0x0A43;
	  Regs1[5]   = 0x0032;
	  Regs1[4]   = 0x4204;
	  Regs1[3]   = 0x0041;
	  Regs1[2]   = 0x81F4;
	  Regs1[1]   = 0x57A0;
	  Regs1[0]   = 0x6470;


	  for (int i = 122; i >= 0; --i) {

			PLL_write(i,Regs1[i]); // Descending register order.
	    delay(1);
	  }

}

 

/*
 * Prints all PLL register values for diagnostics and verification.
 */
void DumpPLLRegisters() {
  Serial.println("------ PLL Register Dump ------");
  for (int i = 122; i >= 0; --i) {
    uint16_t value = PLL_read(i);
    Serial.print("Reg[");
    if (i < 10) Serial.print("0");  // Leading zero for single-digit register addresses.
    Serial.print(i);
    Serial.print("] = 0x");
    if (value < 0x1000) Serial.print("0"); // Used for visual alignment.
    if (value < 0x100) Serial.print("0");
    if (value < 0x10) Serial.print("0");
    Serial.println(value, HEX);
  }
  Serial.println("-------------------------------");
}