// MCP23S17 I/O expander control functions for SPI-based GPIO, filter, PLL, and attenuator control.

#include "MCP23S17.h"
#include "main.h"
#include <SPI.h>

// Initializes the MCP23S17 expander, verifies PORTA/PORTB direction configuration,
// clears GPIO outputs, and applies the default output state.
void IO_EXP1_Init ()
{
  SPI.begin(SCLK_, MISO_, MOSI_, IO1_CS);
  pinMode(IO1_CS, OUTPUT);
  digitalWrite(IO1_CS, HIGH); 

	// Initialize the first I/O expander.
	IO_EXP1_write(IODIRA,0xFF); // Set all pins to INPUT
	uint8_t dumy1FF = IO_EXP1_read(IODIRA);
	IO_EXP1_write(IODIRA,0x00);// Set all pins to OUTPUT 
	uint8_t dumy100 = IO_EXP1_read(IODIRA);

	IO_EXP1_write(IODIRB,0xFF); // Set all pins to INPUT
	uint8_t dumy2FF = IO_EXP1_read(IODIRB);
	IO_EXP1_write(IODIRB,0x00);// Set all pins to OUTPUT
	uint8_t dumy200 = IO_EXP1_read(IODIRB);

	IO_EXP1_write(GPIOA_,0x00); // Set all pins LOW
 	IO_EXP1_write(GPIOB_,0x00); // Set all pins LOW
	if (dumy1FF==0xFF && dumy100 ==0x00 && dumy2FF==0xFF && dumy200 ==0x00)
	{
		Serial.println("IO_EXP1 Initialization Successful.\r\n");
	}
	else
	{
		Serial.println("IO_EXP1 Initialization Failed!!!\r\n");
	}

  SetIOExpander();

}



// Prints all MCP23S17 register values for debug and hardware verification.
void IO_EXP1_DumpAllRegisters()
{
    // Register addresses.
    uint8_t registers[] = {IODIRA, IODIRB, IPOLA, IPOLB, GPINTENA, GPINTENB, DEFVALA, DEFVALB,
                           INTCONA, INTCONB, IOCONA, IOCONB, GPPUA, GPPUB, INTFA, INTFB,
                           INTCAPA, INTCAPB, GPIOA_, GPIOB_, OLATA, OLATB};

    char message[50];

    // Read each register and print its value to the serial port.
    for (int i = 0; i < sizeof(registers); i++)
    {
        uint8_t regValue = IO_EXP1_read(registers[i]);  // Read the register value
        sprintf(message, "Register 0x%02X: 0x%02X\r\n", registers[i], regValue);  // Format the value
        Serial.println(message);  // Print to the serial port
    }

    Serial.println("IO_EXP1 Register Dump Completed.\r\n");
}

// PORTA pin state variables.
uint8_t FLTRD_AMP_CTRL      = 0;   // GPA0 Out
uint8_t RF_DSA2_P5          = 0;   // GPA1 Out
uint8_t PLL1_CE             = 0;   // GPA2 Out
uint8_t RF_DSA2_P4          = 0;   // GPA3 Out
uint8_t RF_DSA1_P5          = 0;   // GPA4 Out
uint8_t RF_DSA1_P4          = 0;   // GPA5 Out
uint8_t RF_DSA1_P3          = 0;   // GPA6 Out
uint8_t RF_DSA1_P2          = 0;   // GPA7 Out

// PORTB pin state variables.
uint8_t RF_DSA1_P1          = 0;   // GPB0 Out
uint8_t RF_DSA2_P1          = 0;   // GPB1 Out
uint8_t IF1_SW1_C_INV       = 0;   // GPB2 Out
uint8_t LO1_SW1_A           = 0;   // GPB3 Out
uint8_t LO1_SW1_B           = 0;   // GPB4 Out
uint8_t LO1_SW1_C           = 0;   // GPB5 Out
uint8_t RF_DSA2_P2          = 0;   // GPB6 Out
uint8_t RF_DSA2_P3          = 0;   // GPB7 

uint8_t IF1_SW1_C           = 1;   // GPB1 Out (legacy signal placeholder; not used in the current hardware configuration)


uint8_t portA_byte = 0;
uint8_t portB_byte = 0;

// Updated PLL enable control helper.
void SetPLL1OnOff(bool enable)
{
    if (enable)
        portA_byte |=  (1 << 2);  // GPA2 = 1 → PLL ON
    else
        portA_byte &= ~(1 << 2);  // GPA2 = 0 → PLL OFF

    IO_EXP1_write(GPIOA_, portA_byte);
}

// Builds the 16-bit output image from individual signal variables and writes it to PORTA/PORTB.
void SetIOExpander()
{
  	 uint16_t IoExpData = 0;

  	 IoExpData =
							(FLTRD_AMP_CTRL <<0) +
							(RF_DSA2_P5 <<1) +
							(PLL1_CE <<2) +
							(RF_DSA2_P4 	<<3) +
							(RF_DSA1_P5 	<<4) +
							(RF_DSA1_P4 	<<5) +
							(RF_DSA1_P3 <<6) +
							(RF_DSA1_P2 <<7) +
							(RF_DSA1_P1 <<8) +
							(RF_DSA2_P1 	<<9) +
							(IF1_SW1_C_INV 	<<10) +
							(LO1_SW1_A 	<<11) +
							(LO1_SW1_B 	<<12) +
							(LO1_SW1_C 	<<13) +
							(RF_DSA2_P2 	<<14) +
              (RF_DSA2_P3 	<<15) ;

 	// Split the 16-bit expander state into PORTA and PORTB bytes.
  portA_byte = (IoExpData & 0xFF);
  portB_byte = (IoExpData >> 8) & 0xFF;

  IO_EXP1_write(GPIOA_, portA_byte);
  IO_EXP1_write(GPIOB_, portB_byte);

}
// Writes one byte to the selected MCP23S17 register over SPI.
void IO_EXP1_write (uint8_t address, uint8_t Data)
{
  uint8_t spi_buf[3];
  spi_buf[0] =  0x40;  // Write operation  (0 1 0 0 A2 A1 A0 R/W)
  spi_buf[1] =  address;
  spi_buf[2] =  Data;

  digitalWrite(IO1_CS, LOW);
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(spi_buf, 3);  // Send 3 bytes of data
  SPI.endTransaction();
  digitalWrite(IO1_CS, HIGH);

  /*
  // Debug print
    if (address == GPIOA_) {
      Serial.printf("Written 0x%02X to GPA\n", Data);
    } 
    else if (address == GPIOB_) {
      Serial.printf("Written 0x%02X to GPB\n", Data);
    }
  */

}


// OnOff == true  -> IF1_SW1_C_INV = 1
// OnOff == false -> IF1_SW1_C_INV = 0
// Controls the filter path by updating IF1_SW1_C_INV on PORTB.
void SetFilterState(bool OnOff)
{
    if (OnOff)
    {
        portB_byte |= (1 << 2); 
    }
    else
    {
        portB_byte &= ~(1 << 2);
    }

    IO_EXP1_write(GPIOB_, portB_byte);
}

void SetRF_FLTRD_AMP_CTRL(uint8_t state)
{
    portA_byte &= ~(1 << 0);
    if (state) 
    {
        portA_byte |= (1 << 0);
    }
    IO_EXP1_write(GPIOA_, portA_byte);
}


// Applies the same 5-bit attenuation value to both RF DSA attenuator banks.
void SetAttenuator(uint8_t value)
{
    if (value > 31)
    {
      value = 31; 
    }
         
    SetRF_DSA1(value);
    SetRF_DSA2(value);
}

// Frequency is provided in MHz. The correct band is selected and LO1_SW1 A, B, C pins are updated.
// Selects the RF filter band according to the input frequency and updates LO1 switch lines.
void SetFilterBand(double Freq)
{
    uint8_t A = 0, B = 0, C = 0;

    if (Freq >= 2000 && Freq <= 3000)       { A = 0; B = 0; C = 0; } // Band 1
    else if (Freq > 3000 && Freq <= 5000)   { A = 1; B = 0; C = 1; } // Band 2
    else if (Freq > 5000 && Freq <= 8300)   { A = 0; B = 1; C = 0; } // Band 3
    else if (Freq > 8300 && Freq <= 11700)  { A = 1; B = 1; C = 0; } // Band 4
    else if (Freq > 11700 && Freq <= 13300) { A = 1; B = 0; C = 0; } // Band 5
    else if (Freq > 13300 && Freq <= 18000) { A = 0; B = 0; C = 1; } // Band 6
    else
    {
        Serial.printf("Invalid frequency %.2f MHz. Must be between 2000 and 18000.\r\n", Freq);
        return;
    }

    uint8_t value = (A << 0) | (B << 1) | (C << 2);  // A, B, C -> bits 0, 1, 2
    SetLO1_SW1(value);
}

// Reads one byte from the selected MCP23S17 register over SPI.
uint8_t IO_EXP1_read (uint8_t address)
{
  uint8_t spi_buf[2];
  spi_buf[0] =  0x41;  // Read operation  (0 1 0 0 A2 A1 A0 R/W)
  spi_buf[1] =  address;
  uint8_t read_buf[1];

	digitalWrite(IO1_CS, LOW);      // pull the pin low
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(spi_buf, 2);  // Send address bytes

  read_buf[0] = SPI.transfer(0x00);  // Send a dummy byte to clock in the read value
  SPI.endTransaction();
	digitalWrite(IO1_CS, HIGH);  // pull the pin high

	return read_buf[0];
}
 

// Maps the 3-bit LO1 switch selection value to GPB3-GPB5.
void SetLO1_SW1(uint8_t value)
{
    portB_byte &= ~(0b00111000);            // Clear GPB3, GPB4, and GPB5
    portB_byte |=  (value & 0b00000111) << 3; // Map A, B, C to GPB3, GPB4, and GPB5
    IO_EXP1_write(GPIOB_, portB_byte);
}


// Maps the 5-bit DSA1 attenuation control value to its assigned expander pins.
void SetRF_DSA1(uint8_t value)
{
    // Clear previous values first.
    portA_byte &= ~( (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) ); // Clear GPA4-GPA7
    portB_byte &= ~(1 << 0); // Clear GPB0

    // Value bit order: [P5 P4 P3 P2 P1] -> [bit4 bit3 bit2 bit1 bit0]
    if (value & (1 << 4)) portA_byte |= (1 << 4); // P5 -> GPA4
    if (value & (1 << 3)) portA_byte |= (1 << 5); // P4 -> GPA5
    if (value & (1 << 2)) portA_byte |= (1 << 6); // P3 -> GPA6
    if (value & (1 << 1)) portA_byte |= (1 << 7); // P2 -> GPA7
    if (value & (1 << 0)) portB_byte |= (1 << 0); // P1 -> GPB0

    IO_EXP1_write(GPIOA_, portA_byte);
    IO_EXP1_write(GPIOB_, portB_byte);
}

// Maps the 5-bit DSA2 attenuation control value to its assigned expander pins.
void SetRF_DSA2(uint8_t value)
{
    // Clear previous values first.
    portA_byte &= ~( (1 << 1) | (1 << 3) ); // Clear GPA1 and GPA3
    portB_byte &= ~( (1 << 1) | (1 << 6) | (1 << 7) ); // Clear GPB1, GPB6, and GPB7

    // [P5 P4 P3 P2 P1] -> [bit4 bit3 bit2 bit1 bit0]
    // P1:GPB1, P2:GPB6, P3:GPB7, P4:GPA3, P5:GPA1
    
    if (value & (1 << 0)) portB_byte |= (1 << 1); // P1 -> GPB1
    if (value & (1 << 1)) portB_byte |= (1 << 6); // P2 -> GPB6
    if (value & (1 << 2)) portB_byte |= (1 << 7); // P3 -> GPB7
    if (value & (1 << 3)) portA_byte |= (1 << 3); // P4 -> GPA3
    if (value & (1 << 4)) portA_byte |= (1 << 1); // P5 -> GPA1

    IO_EXP1_write(GPIOA_, portA_byte);
    IO_EXP1_write(GPIOB_, portB_byte);
}

// Sets the PLL1 chip-enable line on GPA2.
void SetPLL1_CE(bool state)
{
    portA_byte &= ~(1 << 2);      // GPA2 = 0
    if (state)
        portA_byte |= (1 << 2);   // GPA2 = 1

    IO_EXP1_write(GPIOA_, portA_byte);
}

 


