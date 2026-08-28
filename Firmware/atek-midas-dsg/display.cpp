#include <TFT_eSPI.h>  // TFT display library
#include "img_logo.h"
#include "display.h"
#include "main.h"
#include <Wire.h>
#include  "Lmx2820.h"
#include <CST816_TouchLib.h>  // CST816S touch screen driver
#include "MCP23S17.h"
#include "ADC78H90.h"

extern void RC_HandleLine(char *line);

extern bool rfOutputEnabled;

#define PIN_I2C_SDA 18
#define PIN_I2C_SCL 17
#define DISPLAY_X 320
TFT_eSPI tft = TFT_eSPI();
using namespace MDO;
CST816Touch_SWMode oTouchController;  // Main touch screen controller class from the library used by this example.


String FreqValueForMainMenu = "";
String AmpValueForMainMenu = "";  
String FreqUnitForMainMenu = ""; 

MenuState currentMenu = MAIN_MENU;
MenuState prev_currentMenu = NONE;

String enteredFreqValue = "";
String prev_enteredFreqValue = "";

String enteredUnitValue = "";
String prev_enteredUnitValue = "";

String enteredAmpValue = "";
String prev_enteredAmpValue = "";

String enteredDecimalValue = "";
String prev_enteredDecimalValue = "";


String StartValueForSweepMenu = "300";
String prev_StartValueForSweepMenu = "";

String StopValueForSweepMenu = "22600";
String prev_StopValueForSweepMenu = "";

String StepValueForSweepMenu = "100";
String prev_StepValueForSweepMenu = "";

String DwellValueForSweepMenu = "1";
String prev_DwellValueForSweepMenu = "";

String AmpValueSweepForSweepMenu = "10";
String prev_AmpValueSweepForSweepMenu = "";

String CountValueForSweepMenu = "0"; // 0 = run forever (matches previous behavior, since Count had no effect before)
String prev_CountValueForSweepMenu = "";

String StepTypeValueForSweepMenu = "Lin";
String prev_StepTypeValueForSweepMenu = "";

String freqValue = "22.6 GHz";
String ampValue = "0 dBm";

String StartUnitForSweepMenu = "MHz";
String StopUnitForSweepMenu  = "MHz";
String StepUnitForSweepMenu  = "MHz";
void SetTypeOnSweepMenu(String type);
static void SetFilterOnSweepMenu(bool value);


void setupTDisplayS3() {
  // Since this controller was the original base for this library,
  // most default settings are already suitable.
  if (!oTouchController.begin(Wire)) {  // Initializes and uses the TouchScreenEventCache instead of a provided Observer.
    Serial.println("Touch screen initialization failed..");
    while (true) {
      delay(100);
    }
  }
  oTouchController.setSwapXY(true);
  oTouchController.setInvertY(true, 170);  // Comment this line when tft.setRotation(3) is used.
  // No GestureFactory is required for this controller/firmware setup.

  // Double-click factories are enabled here to support optional touch gestures.
  oTouchController.enableDoubleClickFactory_Quick();    // Fastest option with no delay; a double click still produces both a Touch event and a Gesture.
  oTouchController.enableDoubleClickFactory_Elegant();  // More refined option; slightly buffers and delays touch events.
}


int Xpos147 = 0;// Buttons 1, 4 and 7
int Xpos258 = 63;// Buttons 2, 5 and 8
int Xpos369 = 123;// Buttons 3, 6 and 9
int XposEBD0 = 190; // Buttons Enter, Backspace, Dot and 0
int XposXKMG = 253; // Buttons X, KHz, MHz and GHz

int ButtonWidth = 56;
int ButtonHeight = 36;

int YposEX = 0;
int Ypos123BK = 43; // Buttons 1, 2, 3, Backspace and KHz
int Ypos456DM = 86; // Buttons 4, 5, 6, Dot and MHz (or Minus)
int Ypos7890G = 127; // Buttons 7, 8, 9, 0 and GHz

bool isSweepRunning = false;
double currentHz = 0;  

// Counts how many full sweep cycles (start->stop wrap) have completed since
// the sweep was (re)started. Used by RunSweep() to stop automatically once
// CountValueForSweepMenu is reached (0 = run forever, unchanged behavior).
uint32_t sweepCycleCount = 0;

bool CurrentLockStatus = false;
bool CurrentBITStatus = true;
bool CurrentRFStatus = false;
WifiStatus CurrentWifiStatus = WIFI_STATUS_OFF;
String CurrentTempValue = "";
String CurrentUSBVoltageValue = "";
String CurrentConnectionStatus = "";

// -----------------------------------------------------------------------------
// Touch input dispatcher
// Maps TFT touch coordinates to the active menu action and updates the related
// UI state without changing the underlying device-control logic.
// -----------------------------------------------------------------------------
void GetTouchData(int x, int y) {



  if (currentMenu == MAIN_MENU) {
    if (x > 3 && x < 80 && y > 3 && y < 45) {  // Frequency button
      Serial.println("Frequency Button Pressed");
      prev_enteredFreqValue = enteredFreqValue;
      prev_enteredUnitValue = enteredUnitValue;
      drawFreqMenu(currentMenu);
    } else if (x > 3 && x < 80 && y > 53 && y < 95) {  // Amplitude button
      Serial.println("Amplitude Button Pressed");
      prev_enteredAmpValue = enteredAmpValue;
      drawAmpMenu(currentMenu);
    }else if (x > 3 && x < 80 && y > 100 && y < 142) {  // Filter button
      FilterStatus = !FilterStatus;
      SetFilter(FilterStatus);
      if (FilterStatus) 
      {
        Serial.println("Filter ON");
      } else {
        Serial.println("Filter OFF");
      }
      
      // --- Recalculate attenuator/power settings after the filter state changes ---
      char cmdBuf[32];
      sprintf(cmdBuf, "POW:LEV %s", AmpValueForMainMenu.c_str());
      RC_HandleLine(cmdBuf);
      // ---------------------------------------------------------------------------------
      
    }else if (x > 264 && x < 312 && y > 42 && y < 90) {  // RF On/Off button

    if (CurrentRFStatus)
    {
        Serial.println("RF Out OFF Button Pressed");
        SetRfOnOff(false);
    }
    else
    {
        checkenteredFreqValue(enteredFreqValue);  // first apply freq
        SetRfOnOff(true);                         // then turn RF on
        Serial.println("RF Out ON Button Pressed");
    }

    }else if (x > 268 && x < 308 && y > 95 && y < 135) {  // Save button

          Serial.println("Rf Settings Saved");
          SaveRfSettingsBtn();
 

    }
    
  
  }
   // **Frequency entry screen (Freq.png)**
  else if (currentMenu == FREQ_MENU || currentMenu == START_MENU || currentMenu == STOP_MENU  || currentMenu == STEP_MENU  ) {
    // **Numeric buttons**
    if (x > Xpos147 && x < Xpos147 + ButtonWidth) {
      if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: 1"); enteredFreqValue += "1"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Pressed: 4"); enteredFreqValue += "4"; }
      else if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 7"); enteredFreqValue += "7"; }
    } else if (x > Xpos258 && x < Xpos258 + ButtonWidth) {
      if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: 2"); enteredFreqValue += "2"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Pressed: 5"); enteredFreqValue += "5"; }
      else if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 8"); enteredFreqValue += "8"; }
    } else if (x > Xpos369 && x < Xpos369 + ButtonWidth) {
      if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: 3"); enteredFreqValue += "3"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Pressed: 6"); enteredFreqValue += "6"; }
      else if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 9"); enteredFreqValue += "9"; }
    }

    // **Enter, Backspace, Dot and 0 buttons**
    else if (x > XposEBD0 && x < XposEBD0 + ButtonWidth) {
      if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 0"); enteredFreqValue += "0"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { 
        Serial.println("Pressed: .");
        if (enteredFreqValue.indexOf('.') == -1) enteredFreqValue += "."; 
      }
      else if (y > YposEX && y < YposEX + ButtonHeight) { Serial.println("Pressed: Enter");   enteredFreqValue = enteredFreqValue;if ( checkenteredFreqValue(enteredFreqValue)) drawActiveMenu(); else return; }
      else if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { 
        Serial.println("Pressed: Backspace");
        if (!enteredFreqValue.isEmpty()) enteredFreqValue.remove(enteredFreqValue.length() - 1);
      }
    }

    // **X, KHz, MHz and GHz buttons**
    else if (x > XposXKMG && x < XposXKMG + ButtonWidth) {
      if (y > YposEX && y < YposEX + ButtonHeight) { Serial.println("Pressed: X (Cancel)"); enteredFreqValue = prev_enteredFreqValue; enteredUnitValue  = prev_enteredUnitValue; drawActiveMenu(); }
      else if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: KHz"); enteredUnitValue = "KHz"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Pressed: MHz"); enteredUnitValue = "MHz"; }
      else if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: GHz"); enteredUnitValue = "GHz"; }
    }
   
    updateFreqAreaOnFreqMenu(enteredFreqValue,enteredUnitValue); 

  }
  // **Amplitude entry screen (Amp.png)**
  else if (currentMenu == AMP_MENU) {
    // **Numeric buttons**
    if (x > Xpos147 && x < Xpos147 + ButtonWidth) {
      if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: 1"); enteredAmpValue += "1"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Pressed: 4"); enteredAmpValue += "4"; }
      else if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 7"); enteredAmpValue += "7"; }
    } else if (x > Xpos258 && x < Xpos258 + ButtonWidth) {
      if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: 2"); enteredAmpValue += "2"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Pressed: 5"); enteredAmpValue += "5"; }
      else if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 8"); enteredAmpValue += "8"; }
    } else if (x > Xpos369 && x < Xpos369 + ButtonWidth) {
      if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: 3"); enteredAmpValue += "3"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Pressed: 6"); enteredAmpValue += "6"; }
      else if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 9"); enteredAmpValue += "9"; }
    }

    // **Enter, Backspace, Dot and 0 buttons**
    else if (x > XposEBD0 && x < XposEBD0 + ButtonWidth) {
      if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 0"); enteredAmpValue += "0"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { 
          Serial.println("Pressed: . (Cancelled)"); 
          // Dot (.) input is intentionally ignored here.
      }
      else if (y > YposEX && y < YposEX + ButtonHeight) { Serial.println("Pressed: Enter"); enteredAmpValue = enteredAmpValue; if ( checkenteredAmpValue()) drawActiveMenu(); else return; }
      else if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: Backspace");      if (!enteredAmpValue.isEmpty()) enteredAmpValue.remove(enteredAmpValue.length() - 1);  }
    }

 

    // **X and Minus (-) buttons**
    else if (x > XposXKMG && x < XposXKMG + ButtonWidth) {
      if (y > YposEX && y < YposEX + ButtonHeight) { Serial.println("Pressed: X (Cancel)"); enteredAmpValue = prev_enteredAmpValue; drawActiveMenu(); }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Minus Pressed: .");    if (enteredAmpValue.length() < 1) enteredAmpValue += "-";   }
    }

   updateAmpAreaOnAmpMenu();
    
  }

    else if (currentMenu == SWP_COUNT_MENU || currentMenu == DWELL_MENU) {
    // **Numeric buttons**
    if (x > Xpos147 && x < Xpos147 + ButtonWidth) {
      if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: 1"); enteredDecimalValue += "1"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Pressed: 4"); enteredDecimalValue += "4"; }
      else if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 7"); enteredDecimalValue += "7"; }
    } else if (x > Xpos258 && x < Xpos258 + ButtonWidth) {
      if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: 2"); enteredDecimalValue += "2"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Pressed: 5"); enteredDecimalValue += "5"; }
      else if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 8"); enteredDecimalValue += "8"; }
    } else if (x > Xpos369 && x < Xpos369 + ButtonWidth) {
      if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: 3"); enteredDecimalValue += "3"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Pressed: 6"); enteredDecimalValue += "6"; }
      else if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 9"); enteredDecimalValue += "9"; }
    }

    // **Enter, Backspace, Dot and 0 buttons**
    else if (x > XposEBD0 && x < XposEBD0 + ButtonWidth) {
      if (y > Ypos7890G && y < Ypos7890G + ButtonHeight) { Serial.println("Pressed: 0"); enteredDecimalValue += "0"; }
      else if (y > Ypos456DM && y < Ypos456DM + ButtonHeight) { Serial.println("Pressed: .");    if (enteredDecimalValue.indexOf('.') == -1) enteredDecimalValue += ".";   }
      else if (y > YposEX && y < YposEX + ButtonHeight) { Serial.println("Pressed: Enter");  if (currentMenu == SWP_COUNT_MENU && enteredDecimalValue.length() == 0) {enteredDecimalValue = "0";}  if (currentMenu == DWELL_MENU && enteredDecimalValue.length() == 0) {enteredDecimalValue = "1";}  drawActiveMenu();  }
      else if (y > Ypos123BK && y < Ypos123BK + ButtonHeight) { Serial.println("Pressed: Backspace");      if (!enteredDecimalValue.isEmpty()) enteredDecimalValue.remove(enteredDecimalValue.length() - 1);  }
    }

 

    // **X button**
    else if (x > XposXKMG && x < XposXKMG + ButtonWidth) {
      if (y > YposEX && y < YposEX + ButtonHeight) { Serial.println("Pressed: X (Cancel)"); enteredDecimalValue = prev_enteredDecimalValue; drawActiveMenu(); }
    }

   updateDecimalArea();
    
  }


  else if (currentMenu == SWEEP_MENU) 
  {

    prev_currentMenu = SWEEP_MENU;

      if (x >= 0 && x <= 41 && y >= 0 && y <= 60) {  // Start Button
        Serial.println("Start Button Pressed");
        drawStartMenu();
      } 
      else if (x >= 0 && x <= 41 && y >= 61 && y <= 85)     {  // Stop Button
        Serial.println("Stop Button Pressed");
        drawStopMenu();
      }
      else if (x >= 0 && x <= 41 && y >= 86 && y <= 128)     {  // Step Button
        Serial.println("Step Button Pressed");
        drawStepMenu();
      }
      else if (x >= 0 && x <= 41 && y >= 129 && y <= 170)     {  // Dwell Button
        Serial.println("Dwell Button Pressed");
        drawDwellMenu();
      }
      else if (x >= 186 && x <= 252 && y >= 0 && y <= 42)     {  // Amp Button
        Serial.println("Amp Button Pressed");
        drawAmpMenu(currentMenu);
      }
      else if (x >= 186 && x <= 252 && y >= 42 && y <= 84)     {  // Count(Number) Button
        Serial.println("Count(Number) Button Pressed");
        drawCountMenu();
      }
      else if (x >= 186 && x <= 230 && y >= 85 && y <= 124)     
      {  // Linear/Logarithmic toggle button
          Serial.println("Lineer/Logaritmic Button Pressed");

          currentHz = StartValueForSweepMenu.toDouble();
          if (StartUnitForSweepMenu == "KHz") currentHz *= 1e3;
          else if (StartUnitForSweepMenu == "MHz") currentHz *= 1e6;
          else if (StartUnitForSweepMenu == "GHz") currentHz *= 1e9;

          if (StepTypeValueForSweepMenu == "Log") {
            StepTypeValueForSweepMenu = "Lin";
          } else {
            StepTypeValueForSweepMenu = "Log";
          }
          
          // Update the screen.
          SetTypeOnSweepMenu(StepTypeValueForSweepMenu);
      }
      else if (x >= 186 && x <= 235 && y >= 125 && y <= 169)
      {  // Sweep Filter toggle
          Serial.println("Sweep Filter Button Pressed");
          if (!isSweepRunning)
          {
            FilterStatus = !FilterStatus;
            SetFilter(FilterStatus);
            SetFilterOnSweepMenu(FilterStatus);
          }
          else
          {
             Serial.println("Sweep Filter change ignored while sweep is running");
          }
      }
      
      else if (x >= 260 && x <= 320 && y >= 120 && y <= 160)    
      {  // Sweep Start/Stop Button
        Serial.println("Sweep Start/Stop Button Pressed");
        isSweepRunning = !isSweepRunning;
        if (isSweepRunning)
        {
          sweepCycleCount = 0; // Reset cycle counter every time a sweep run is (re)started.
          tft.pushImage(270, 125, 36, 36, (uint16_t*)Pause);
          SetPLL1OnOff(true);
        }
        else
        {
          tft.pushImage(270, 125, 36, 36, (uint16_t*)Play);
          SetPLL1OnOff(false);
        }
      }
    } // End of SWEEP_MENU handling
  } // End of GetTouchData function




 

// -----------------------------------------------------------------------------
// Frequency input validation and commit logic
// Converts the selected unit to Hz, checks menu-specific limits, and applies or
// stores the value depending on the active menu.
// -----------------------------------------------------------------------------
bool checkenteredFreqValue(String FreqVal) {
    String tempFreq = FreqVal;

    // Convert current input (user just typed) to Hz
    double freqValue = tempFreq.toDouble();
    if (enteredUnitValue == "KHz") freqValue *= 1e3;
    else if (enteredUnitValue == "MHz") freqValue *= 1e6;
    else if (enteredUnitValue == "GHz") freqValue *= 1e9;

    bool outOfRange = false;

    switch (currentMenu) {

      // START: must be inside device range
      case START_MENU:
        if (freqValue < MIN_FREQ || freqValue > MAX_FREQ)
          outOfRange = true;
        break;

      // STOP: must be greater than START and ≤ MAX_FREQ
      case STOP_MENU: {
        double startHz = StartValueForSweepMenu.toDouble() * 1e6;  // Always MHz base
        if (freqValue < startHz || freqValue > MAX_FREQ)
          outOfRange = true;
        break;
      }

      // STEP: must be > 0 and smaller than (STOP - START), with proper unit conversion
      case STEP_MENU: {
        // --- Convert Start and Stop to Hz (use their own units!) ---
        double startHz = StartValueForSweepMenu.toDouble();
        if (StartUnitForSweepMenu == "KHz") startHz *= 1e3;
        else if (StartUnitForSweepMenu == "MHz") startHz *= 1e6;
        else if (StartUnitForSweepMenu == "GHz") startHz *= 1e9;

        double stopHz = StopValueForSweepMenu.toDouble();
        if (StopUnitForSweepMenu == "KHz") stopHz *= 1e3;
        else if (StopUnitForSweepMenu == "MHz") stopHz *= 1e6;
        else if (StopUnitForSweepMenu == "GHz") stopHz *= 1e9;

        double diffHz = stopHz - startHz;

        // --- Convert entered Step to Hz ---
        double stepHz = FreqVal.toDouble();
        if (enteredUnitValue == "KHz") stepHz *= 1e3;
        else if (enteredUnitValue == "MHz") stepHz *= 1e6;
        else if (enteredUnitValue == "GHz") stepHz *= 1e9;

        // --- Debug output ---
        Serial.printf("Start=%.3f Hz | Stop=%.3f Hz | Diff=%.3f Hz | Step=%.3f Hz | Unit=%s\n",
                      startHz, stopHz, diffHz, stepHz, enteredUnitValue.c_str());

        // --- Validation ---
        if (stepHz <= 0 || stepHz > diffHz)
            outOfRange = true;

        break;
    }



      // Default: check global limits
      default:
        if (freqValue < MIN_FREQ || freqValue > MAX_FREQ)
          outOfRange = true;
        break;
    }

    if (outOfRange) {
      // Show visual warning only on frequency entry screens
      if (currentMenu == FREQ_MENU || currentMenu == START_MENU ||
          currentMenu == STOP_MENU  || currentMenu == STEP_MENU) {
        tft.fillRect(10, 4, 161, 35, TFT_WHITE);
        tft.setTextColor(TFT_RED, TFT_WHITE);
        tft.setCursor(10, 32);
        tempFreq.replace(".", "");
        if (tempFreq.length() > 8)
          tft.setFreeFont(&FreeSansBold12pt7b);
        else
          tft.setFreeFont(&FreeSansBold18pt7b);
        tft.print(FreqVal);
      }
      return false;
    }


    // --- Apply or store frequency ---
    switch (currentMenu) {
      case MAIN_MENU:
      case FREQ_MENU:
        ApplyFrequency(freqValue);
        FreqValueForMainMenu = enteredFreqValue;
        FreqUnitForMainMenu  = enteredUnitValue;
        break;

      case START_MENU:
        StartValueForSweepMenu = enteredFreqValue; 
        StartUnitForSweepMenu  = enteredUnitValue;  
        break;

      case STOP_MENU:
        StopValueForSweepMenu = enteredFreqValue; 
        StopUnitForSweepMenu  = enteredUnitValue;   
        break;

      case STEP_MENU:
        StepValueForSweepMenu = enteredFreqValue; 
        StepUnitForSweepMenu  = enteredUnitValue;   
        break;

      default:
        break;
    }

    return true;
}


 

bool checkenteredAmpValue() {
   if (enteredAmpValue.length() == 0) { enteredAmpValue = "0"; }  
    String tempAmp = enteredAmpValue;
    double AmpValue = tempAmp.toDouble();

    // Target Power range has been extended to -30.0 to 30.0 dBm.
    if (AmpValue < -30.0 || AmpValue > 30.0) {
      tft.fillRect(10, 4, 108, 35, TFT_WHITE); // Clear old value
      tft.setTextColor(TFT_RED, TFT_WHITE);
      tft.setCursor(10, 32);
      tft.setFreeFont(&FreeSansBold18pt7b);
      tft.print(enteredAmpValue);
      return false;
    } 

    if (prev_currentMenu == SWEEP_MENU) {
      // Store only in the Sweep state; do not apply directly to RF.
      AmpValueSweepForSweepMenu = enteredAmpValue;
    } else {
      // Main context: forward the value to the power-control engine through SCPI.
      char cmdBuf[32];
      sprintf(cmdBuf, "POW:LEV %s", enteredAmpValue.c_str());
      RC_HandleLine(cmdBuf);
    }
    return true;
}

void drawUnderline(int x, int y) {
  tft.drawLine(x + 10, y + ButtonHeight*0.9, x + ButtonWidth-2, y + ButtonHeight*0.9, tft.color565(50, 50, 50));
  tft.drawLine(x + 10, y+1 + ButtonHeight*0.9, x  + ButtonWidth-2 , y+1 +  ButtonHeight*0.9, tft.color565(50, 50, 50));
  tft.drawLine(x + 10, y+2 + ButtonHeight*0.9, x  + ButtonWidth-2 , y+2 + ButtonHeight*0.9, tft.color565(50, 50, 50));
  tft.drawLine(x + 10, y+3 + ButtonHeight*0.9, x  + ButtonWidth-2 , y+3 + ButtonHeight*0.9, tft.color565(50, 50, 50));
}

void clearUnderline(int x, int y) {
  tft.drawLine(x + 10, y + ButtonHeight*0.9, x + ButtonWidth-2, y + ButtonHeight*0.9, tft.color565(225, 213, 231));
  tft.drawLine(x + 10, y+1 + ButtonHeight*0.9, x  + ButtonWidth-2 , y+1 +  ButtonHeight*0.9,  tft.color565(225, 213, 231));
  tft.drawLine(x + 10, y+2 + ButtonHeight*0.9, x  + ButtonWidth-2 , y+2 + ButtonHeight*0.9,  tft.color565(225, 213, 231));
  tft.drawLine(x + 10, y+3 + ButtonHeight*0.9, x  + ButtonWidth-2 , y+3 + ButtonHeight*0.9,  tft.color565(225, 213, 231));
}

void updateFreqAreaOnFreqMenu(String FreqVal , String Unit) {
  if (currentMenu == FREQ_MENU || currentMenu == START_MENU || currentMenu == STOP_MENU  || currentMenu == STEP_MENU  )
   {
    String tempValue = FreqVal;

    // --- Allow only limited decimals based on unit ---
    int dotIndex = tempValue.indexOf('.');
    if (dotIndex != -1) {
      int maxDecimals = 0;

      if (Unit == "KHz") {
        maxDecimals = 3;
      } else if (Unit == "MHz") {
        maxDecimals = 6;
      } else if (Unit == "GHz") {
        maxDecimals = 9;
      } else {
        maxDecimals = 0;
      }

      int decimalsCount = tempValue.length() - dotIndex - 1;
      if (decimalsCount > maxDecimals) {
        tempValue = tempValue.substring(0, dotIndex + 1 + maxDecimals);
      }
    }

    // --- Now check overall digit length (ignore the dot) ---
    String tempValueNoDot = tempValue;
    tempValueNoDot.replace(".", "");
    if (tempValueNoDot.length() > 11) {
      if (!tempValue.isEmpty())
        tempValue.remove(tempValue.length() - 1);
    }



    // --- Draw to TFT ---
    tft.fillRect(10, 4, 161, 35, TFT_WHITE);
    tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);
    tft.setCursor(10, 32);

    if (tempValue.length() > 8)
      tft.setFreeFont(&FreeSansBold12pt7b);
    else
      tft.setFreeFont(&FreeSansBold18pt7b);

    tft.print(tempValue);

    clearUnderline(XposXKMG, Ypos123BK);
    clearUnderline(XposXKMG, Ypos456DM);
    clearUnderline(XposXKMG, Ypos7890G);

    if (Unit  == "KHz")
      drawUnderline(XposXKMG, Ypos123BK);
    else if (Unit  == "MHz")
      drawUnderline(XposXKMG, Ypos456DM);
    else if (Unit  == "GHz")
      drawUnderline(XposXKMG, Ypos7890G);
  }
}


void updateDecimalArea() { // for Dwell and Sweep Count
   if (currentMenu == SWP_COUNT_MENU || currentMenu == DWELL_MENU) 
   {

    String tempValue = enteredDecimalValue; 
    tempValue.replace(".", ""); 

  if (currentMenu == SWP_COUNT_MENU) {
      if (tempValue.length() > 4) {
          if (!enteredDecimalValue.isEmpty())
              enteredDecimalValue.remove(enteredDecimalValue.length() - 1);
      }
  }
  else
  {
        if (tempValue.length() > 5)
      {
          if (!enteredDecimalValue.isEmpty()) enteredDecimalValue.remove(enteredDecimalValue.length() - 1); // remove last charcter if length is bigger than 8
      }
  }



    tft.fillRect(10, 4, 108, 35, TFT_WHITE); // Clear old value
    tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);
    tft.setCursor(10, 32);
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.print(enteredDecimalValue);
   }
}


void updateAmpAreaOnAmpMenu() {
   if (currentMenu == AMP_MENU) 
   {

    String tempValue = enteredAmpValue; 
    tempValue.replace(".", ""); 

    if (tempValue.length() > 3) {
        if (!enteredAmpValue.isEmpty()) enteredAmpValue.remove(enteredAmpValue.length() - 1); // Remove the last character if the length exceeds the allowed limit.
    }

    tft.fillRect(10, 4, 108, 35, TFT_WHITE); // Clear old value
    tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);
    tft.setCursor(10, 32);
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.print(enteredAmpValue);
   }

}


void SetFreqOnMainMenu(String value)
{
  tft.fillRect(82, 6, 164, 34, TFT_WHITE); // Clear old values
  FreqValueForMainMenu = value;// Reset the entered value in case it is updated from the web interface.
  tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);

  String tempValue = FreqValueForMainMenu; 
  tempValue.replace(".", ""); 

  if (tempValue.length() > 8) 
  {
    tft.setFreeFont(&FreeSansBold12pt7b);
  }
  else
  {
    tft.setFreeFont(&FreeSansBold18pt7b);
  }

  tft.setCursor(85, 34);
  tft.print(FreqValueForMainMenu);
}

void SetFreqUnitOnMainMenu(String value)
{
  tft.fillRect(255, 6, 60, 30,  tft.color565(151, 186, 218)); // Clear old values
  FreqUnitForMainMenu = value;// Reset the entered unit in case it is updated from the web interface.
  tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setCursor(255, 30);
  tft.print(FreqUnitForMainMenu);
}


void SetAmpUnitOnMainMenu()
{
  tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setCursor(190, 85);
  tft.print("dBm");
}
 

void SetAmpOnMainMenu(String value)
{
   tft.fillRect(82, 56, 95, 30,  TFT_WHITE); // Clear old values
  AmpValueForMainMenu = value; // Reset the entered value in case it is updated from the web interface.
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setCursor(85, 83);
  tft.print(AmpValueForMainMenu);
}

void SetAmp(String value)
{
  enteredAmpValue = value;
}
void SetFreqUnit(String value)
{
  enteredUnitValue = value;
}

void SetFreq(String value)
{
  FreqValueForMainMenu = value;
}

void drawActiveMenu() {

  
  if (prev_currentMenu == MAIN_MENU) 
  {
    drawMainMenu();
  } else if (prev_currentMenu == SWEEP_MENU)
   {
    drawSweepMenu();
  } else {
    // Fallback
    drawMainMenu();
  }
}


void drawMainMenu() {
 
if (prev_currentMenu == SWEEP_MENU) {
    if (isSweepRunning) {
        isSweepRunning = false;
        Serial.println("[DEBUG] Sweep stopped automatically when returning to Main Menu");
    }
}
  
     // --- Commit to the relevant variable ---
      if (currentMenu == FREQ_MENU )
    {
      FreqValueForMainMenu = enteredFreqValue;
      FreqUnitForMainMenu = enteredUnitValue;
    }
      if (currentMenu == AMP_MENU )
    {
      AmpValueForMainMenu = enteredAmpValue;
    }
 
  currentMenu = MAIN_MENU;
  tft.pushImage(0, 0, 320, 170, (uint16_t*)MainMenu);

  SetFreqOnMainMenu(FreqValueForMainMenu);
  SetFreqUnitOnMainMenu(FreqUnitForMainMenu);
  SetAmpOnMainMenu(AmpValueForMainMenu);
  SetAmpUnitOnMainMenu();

  SetFilter(FilterStatus);
  SetRfOnOff(CurrentRFStatus);
  SetSaveButton();
  SetBITStatus(CurrentBITStatus);
  SetLock(CurrentLockStatus);
  SetWifiStatus(CurrentWifiStatus);
  
  SetTemp(CurrentTempValue.c_str());
  SetUSBVoltge(CurrentUSBVoltageValue.c_str());
  ConnectionStatus(CurrentConnectionStatus.c_str(),true);
}


void drawSweepMenu()
{
  // Capture which sub-menu we are returning FROM before currentMenu gets
  // overwritten below. The commit checks further down need this old value -
  // previously they checked currentMenu AFTER it was already set to
  // SWEEP_MENU, so DWELL_MENU/SWP_COUNT_MENU (and the others) could never
  // match, and the entered Dwell/Count value was silently discarded.
  MenuState enteredFrom = currentMenu;

  currentMenu = SWEEP_MENU;
  currentHz = 0;

  tft.pushImage(0, 0, 320, 170, (uint16_t*)SweepMenu);

  if (isSweepRunning) {
    tft.pushImage(270, 125, 36, 36, (uint16_t*)Pause);
  } else {
    tft.pushImage(270, 125, 36, 36, (uint16_t*)Play);
  }

  // Display the Type (Lin/Log) information on the screen.
  SetTypeOnSweepMenu(StepTypeValueForSweepMenu);

  // Display current shared Filter status on Sweep screen.
  SetFilterOnSweepMenu(FilterStatus);

  if (enteredFrom == START_MENU) { StartValueForSweepMenu = enteredFreqValue; }
  else if (enteredFrom == STOP_MENU) { StopValueForSweepMenu = enteredFreqValue; }
  else if (enteredFrom == STEP_MENU) { StepValueForSweepMenu = enteredFreqValue; }      
  else if (enteredFrom == DWELL_MENU) { DwellValueForSweepMenu = enteredDecimalValue; }    
  else if (enteredFrom == AMP_MENU) { AmpValueSweepForSweepMenu = enteredAmpValue; }
  else if (enteredFrom == SWP_COUNT_MENU) { CountValueForSweepMenu = enteredDecimalValue; }

  SetStartFreqOnSweepMenu(StartValueForSweepMenu);
  SetStopFreqOnSweepMenu(StopValueForSweepMenu);
  SetStepFreqOnSweepMenu(StepValueForSweepMenu);
  SetDwellFreqOnSweepMenu(DwellValueForSweepMenu);
  SetAmpAmpOnSweepMenu(AmpValueSweepForSweepMenu);
  SetSCountOnSweepMenu(CountValueForSweepMenu);


}

static void SetFilterOnSweepMenu(bool value)
{
    if (currentMenu != SWEEP_MENU) return;

    if (value)
    {
        tft.pushImage(190, 128, 40, 35, (uint16_t*)FilterON);
    }
    else
    {
        tft.pushImage(190, 128, 40, 35, (uint16_t*)FilterOFF);
    }
}

void SetSaveButton()
{
  tft.pushImage(268, 95, 40, 40, (uint16_t*)Save);
}
void drawFreqMenu(MenuState menu) {
  currentMenu = FREQ_MENU;
  prev_currentMenu = menu; // store where it comes from
  tft.pushImage(0, 0, 320, 170, (uint16_t*)FreqSet);
  enteredFreqValue = FreqValueForMainMenu;
  enteredUnitValue = FreqUnitForMainMenu;
  updateFreqAreaOnFreqMenu(enteredFreqValue, enteredUnitValue);
}
void drawAmpMenu(MenuState menu) {
  currentMenu = AMP_MENU;
  prev_currentMenu = menu; // store where it comes from
  if (menu  == SWEEP_MENU)  
  {
    enteredAmpValue = AmpValueSweepForSweepMenu;
  }
  else
  {
    enteredAmpValue = AmpValueForMainMenu;
  }
  tft.pushImage(0, 0, 320, 170, (uint16_t*)AmpSet);
  updateAmpAreaOnAmpMenu();
}

void drawStartMenu() {
  prev_currentMenu = SWEEP_MENU;
  currentMenu = START_MENU;
  enteredFreqValue = StartValueForSweepMenu; 
  enteredUnitValue = StartUnitForSweepMenu; 
  prev_enteredFreqValue = StartValueForSweepMenu;
  prev_enteredUnitValue = enteredUnitValue;
  tft.pushImage(0, 0, 320, 170, (uint16_t*)FreqSet);
  updateFreqAreaOnFreqMenu(enteredFreqValue, enteredUnitValue);

}
void drawStepMenu() {
  prev_currentMenu = SWEEP_MENU;
  currentMenu = STEP_MENU;
  enteredFreqValue = StepValueForSweepMenu; 
  enteredUnitValue = StepUnitForSweepMenu; 
  prev_enteredFreqValue = StepValueForSweepMenu;
  prev_enteredUnitValue = enteredUnitValue;
  tft.pushImage(0, 0, 320, 170, (uint16_t*)FreqSet);
  updateFreqAreaOnFreqMenu(enteredFreqValue, enteredUnitValue);
}
 
void drawStopMenu() {
  prev_currentMenu = SWEEP_MENU;
  currentMenu = STOP_MENU;
  enteredFreqValue = StopValueForSweepMenu; 
  enteredUnitValue = StopUnitForSweepMenu;
  prev_enteredFreqValue = StopValueForSweepMenu;
  prev_enteredUnitValue = enteredUnitValue;
  tft.pushImage(0, 0, 320, 170, (uint16_t*)FreqSet); 
  updateFreqAreaOnFreqMenu(enteredFreqValue, enteredUnitValue);
}
void drawDwellMenu() {
  prev_currentMenu = SWEEP_MENU;
  currentMenu = DWELL_MENU;

  prev_enteredDecimalValue = DwellValueForSweepMenu;
  enteredDecimalValue      = DwellValueForSweepMenu;
  
  tft.pushImage(0, 0, 320, 170, (uint16_t*)MenuNumbers);
  updateDecimalArea();
}
void drawCountMenu() {
  prev_currentMenu = SWEEP_MENU;
  currentMenu = SWP_COUNT_MENU;

  prev_enteredDecimalValue = CountValueForSweepMenu;
  enteredDecimalValue      = CountValueForSweepMenu;

  tft.pushImage(0, 0, 320, 170, (uint16_t*)MenuNumbers);
  updateDecimalArea();
}

void initTouch() {

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);  // For reliable communication, use a maximum communication rate of 400 Kbps.

  setupTDisplayS3();

  MDO::CST816Touch::device_type_t eDeviceType;
  if (oTouchController.getDeviceType(eDeviceType)) {
    Serial.print("Found device of type: ");
    Serial.println(CST816Touch::deviceTypeToString(eDeviceType));
  }

  Serial.println("Touch screen initialization done");
}

void handleTouch() {
  oTouchController.control();
  TouchScreenEventCache* pTouchCache = TouchScreenEventCache::getInstance();
  if (pTouchCache->hadTouch()) {
    int x = 0;
    int y = 0;
    pTouchCache->getLastTouchPosition(x, y);  // This consumes the touch from the event cache.
    //x = DISPLAY_X - x; // Uncomment when tft.setRotation(3) is used.
    GetTouchData(x, y);
  }
  if (pTouchCache->hadGesture()) {
    TouchScreenController::gesture_t gesture = TouchScreenController::gesture_t::GESTURE_NONE;
    int x = 0;
    int y = 0;
    pTouchCache->getLastGesture(gesture, x, y);  // This consumes the gesture from the event cache.
    //x = DISPLAY_X - x; // Uncomment when tft.setRotation(3) is used.
    Serial.print("Gesture: ");
    Serial.print(TouchScreenController::gestureIdToString(gesture));


  switch (gesture) {
        case TouchScreenController::gesture_t::GESTURE_UP:
          // Add your action for swipe up here
          break;

        case TouchScreenController::gesture_t::GESTURE_DOWN:
          // Add your action for swipe down here
          break;

        case TouchScreenController::gesture_t::GESTURE_LEFT:
          // Add your action for swipe left here
          prev_currentMenu = currentMenu;  // Track where we came from
          drawMainMenu();
          break;

        case TouchScreenController::gesture_t::GESTURE_RIGHT:
          // Add your action for swipe right here
          drawSweepMenu();
          break;

        case TouchScreenController::gesture_t::GESTURE_DOUBLE_CLICK:
        case TouchScreenController::gesture_t::GESTURE_LONG_PRESS:
          GetTouchData(x, y);
          break;

        case TouchScreenController::gesture_t::GESTURE_TOUCH_BUTTON:
          if (currentMenu != INFO_MENU)
            drawInfoScreen();
          else
            drawMainMenu();
          break;

        default:
          break;
      }

 
    Serial.println("");
  }
}


 
 

 
int LastLine = 150;
int OffsetH1 = 5;

int FontHeight(int font) {
  switch (font) {
    case 1:
      return Font1Size;
    case 2:
      return Font2Size;
    case 4:
      return Font4Size;
    case 6:
      return Font6Size;
    case 8:
      return Font8Size;
    default:
      return 0;  // Return 0 for an invalid font number.
  }
}


int screenWidth;
int fontHeight;
void SetupDisplay() {
  tft.init();
  tft.setRotation(1);

  tft.pushImage(0, 0, 320, 170, (uint16_t*)img_logo);
  delay(1000);


  // Set the font
  tft.setFreeFont(&FreeSans12pt7b);
  fontHeight = tft.fontHeight();  // Get font height
  screenWidth = tft.width();      // Get screen width
}
 

int XOffset = 3;
int YOffset = 3;

void ConnectionStatus(const char* text, bool clearLine) {

  CurrentConnectionStatus = String(text);
  //Serial.println(text); 
 
    if (currentMenu == MAIN_MENU) 
    {

      static int cursorX = 191;  // Tracks the current cursor position.
      tft.setTextColor(TFT_WHITE);
      tft.setFreeFont(&FreeSans12pt7b);
      tft.setTextSize(1);
      if (clearLine) {
        // Clear the line and reset the cursor position.
        tft.fillRect(191, 151, 107, 18, tft.color565(51, 51, 51));
        cursorX = 191;  // Reset the cursor.
      }
      tft.setCursor(cursorX, 151, 2);  // Continue from the previous cursor position.
      tft.print(text);     // Print the text.
    
    
      cursorX = tft.getCursorX(); // Save the current cursor position.
      delay(500);
  }
}




bool getTouch(int& x, int& y) {
  TouchScreenEventCache* pTouchCache = TouchScreenEventCache::getInstance();
  if (pTouchCache->hadTouch()) {
    pTouchCache->getLastTouchPosition(x, y);
    Serial.printf("Touched at: %d, %d\n", x, y);
    return true;
  }
  return false;
}


void drawFreqB_AmpB(const char* freqB, const char* ampB) {
  // Not implemented
}

void drawChannelA_SetOnOff(bool onOff) {
  int fontHeight = tft.fontHeight();
  String onText = " ON";
  int onWidth = tft.textWidth(onText);  // Get width of "ON"
  // Draw a rectangle to clear the previous text.
  tft.fillRect(screenWidth - onWidth, fontHeight * 1 + YOffset, onWidth, fontHeight, TFT_BLUE);  // Use the background color.
  tft.setTextColor(TFT_GREEN);                                                                   // Set text color to green
  tft.drawString(onText, screenWidth - onWidth - XOffset, fontHeight * 1 + YOffset);
}

void drawChannelB_SetOnOff(bool onOff) {
  // Not implemented
}


void SetLock(bool value)
{
  CurrentLockStatus = value;
  if (value)
  {
    tft.pushImage(21, 151, 18, 18, (uint16_t*)Locked);
  }
  else
  {
    tft.pushImage(21, 151, 18, 18, (uint16_t*)UnLocked); 
  }
}

void SetFilter(bool FilState)
{

  // Since the LMX2820 uses OUTA when the filter is ON and OUTB when the filter is OFF, the output power levels for both OUTA and OUTB must be reconfigured whenever the filter state changes.
    SetRF_FLTRD_AMP_CTRL(FilState);

    String tempFreq = FreqValueForMainMenu;

    double freqValue = tempFreq.toDouble();

    if (enteredUnitValue == "KHz") {
        freqValue *= 1000.0;
    } else if (enteredUnitValue == "MHz") {
        freqValue *= 1000000.0;
    } else if (enteredUnitValue == "GHz") {
        freqValue *= 1000000000.0;
    }
    double fMHz = freqValue / 1e6;

    SetFilterState(FilState);

    if (FilState)
    {
      if (currentMenu == MAIN_MENU)
      {
        tft.pushImage(80, 100, 40, 35, (uint16_t*)FilterON);

        tft.fillRect(127, 100, 120, 31, tft.color565(46, 116, 181));
        tft.setTextColor(tft.color565(50, 50, 50), tft.color565(46, 116, 181));
        tft.setCursor(127, 124);
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.print("2-18 GHz");
      }

      SetFilterBand(freqValue / 1000000.0);

      Lmx2820SetFreqinMHz(fMHz, 10000000, true);
    }
    else
    {
      if (currentMenu == MAIN_MENU)
      {
        tft.pushImage(80, 100, 40, 35, (uint16_t*)FilterOFF);

        tft.fillRect(127, 100, 120, 31, tft.color565(46, 116, 181));
        tft.setTextColor(tft.color565(50, 50, 50), tft.color565(46, 116, 181));
        tft.setCursor(127, 124);
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.print("0.15-22.6 GHz");
      }
    }

  

}
void SetBITStatus(bool value)
{
  CurrentBITStatus = value;
  if (value)
  {
    tft.pushImage(168, 151, 18, 18, (uint16_t*)BIT_PASS);
  }
  else
  {
    tft.pushImage(168, 151, 18, 18, (uint16_t*)BIT_FAIL); 
  }
}

// Updated RF output control function.
void SetRfOnOff(bool value)
{
    CurrentRFStatus = value;
    SetPLL1OnOff(value); // Send the command directly to the hardware.

    // Always refresh the icon.
    if (currentMenu == MAIN_MENU) {
        if (value)
            tft.pushImage(264, 42, 48, 48, (uint16_t*)RF_ON);
        else
            tft.pushImage(264, 42, 48, 48, (uint16_t*)RF_OFF);
    }
}



void SaveRfSettingsBtn()
{
  tft.pushImage(268, 95, 40, 40, (uint16_t*)SaveOK);
  currentFrequency  = FreqValueForMainMenu;
  currentAmplitude = AmpValueForMainMenu;
  currentFreqUnit = FreqUnitForMainMenu;

  saveRFSettings();  // Save to credentials.
  delay(500);
  tft.pushImage(268, 95, 40, 40, (uint16_t*)Save);
}

void drawInfoScreen() {
  if (currentMenu != INFO_MENU) {
    tft.fillScreen(TFT_BLUE);  // Clear the full screen.
  }

  currentMenu = INFO_MENU;

  // Read real-time values.
  float temp = Read_Temp();
  float usb_voltage = Read_5V_Voltage();
  float dsg_current = Read_5V_Current();
  bool ld_result = isPLL_Locked();

  // Text settings.
  tft.setTextColor(TFT_WHITE);
  tft.setFreeFont(&FreeSans12pt7b);

  // Clear-area dimensions.
  int lineHeight = 25;
  int textX = 20;
  int width = 280; // Width
  int height = 24;

  // Clear and redraw each line.
  tft.fillRect(textX, 20, width, height, TFT_BLUE);
  tft.setCursor(textX, 40);
  tft.printf("5V Current: %.2f A", dsg_current);

  tft.fillRect(textX, 50, width, height, TFT_BLUE);
  tft.setCursor(textX, 70);
  tft.printf("Temperature: %.1f C", temp);

  tft.fillRect(textX, 80, width, height, TFT_BLUE);
  tft.setCursor(textX, 100);
  tft.printf("5V Voltage: %.2f V", usb_voltage);

  tft.fillRect(textX, 110, width, height, TFT_BLUE);
  tft.setCursor(textX, 130);
  tft.printf("LD Result: %s", ld_result ? "LOCKED" : "UNLOCKED");
}




void SetWifiStatus(WifiStatus status) {
  CurrentWifiStatus = status;

    if (currentMenu == MAIN_MENU) 
   {
      switch (status) {
          case WIFI_STATUS_ON:
              tft.pushImage(300, 151, 18, 18, (uint16_t*)WifiOn);
              break;
          case WIFI_STATUS_OFF:
              tft.pushImage(300, 151, 18, 18, (uint16_t*)WifiOff);
              break;
          case WIFI_STATUS_HOTSPOT:
              tft.pushImage(300, 151, 18, 18, (uint16_t*)WifiHotspot);
              break;
          default:
              break;
      }
   }
}



void SetTemp(const char* text)
{
  CurrentTempValue = String(text);
  tft.fillRect(50, 151, 32, 18, tft.color565(51, 51, 51));
  tft.setTextColor(TFT_WHITE);
  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  tft.setCursor(50, 151, 2);
  tft.print(String(text) + "C");   
}



void SetUSBVoltge(const char* text)
{
  CurrentUSBVoltageValue = String(text); 
  tft.fillRect(120, 151, 29, 18, tft.color565(51, 51, 51));
  tft.setTextColor(TFT_WHITE);
  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  tft.setCursor(120, 151, 2);
  tft.print(String(text) + "V");   

}

// Helper function: removes trailing zeros and unnecessary decimal point
String rtrimZeros(String val) {
    // Check if there's a decimal point; if not, return immediately
    if (val.indexOf('.') == -1) return val;

    // Remove trailing zeros
    while (val.endsWith("0")) {
        val.remove(val.length() - 1);
    }

    // Remove trailing decimal point if it remains alone
    if (val.endsWith(".")) {
        val.remove(val.length() - 1);
    }

    return val;
}



// Updated frequency application function: update visible fields only on the Main Menu.
void ApplyFrequency(double fHz) {
    double fMHz = fHz / 1e6;

    if (FilterStatus) {
        SetFilterBand(fMHz);
    }

    // Set PLL frequency.
    bool lock = Lmx2820SetFreqinMHz(fMHz, 10000000 , FilterStatus);

    // Update state only.
    if (fHz >= 1e9) {
        enteredFreqValue = rtrimZeros(String(fHz / 1e9, 9));
        enteredUnitValue = "GHz";
    } else if (fHz >= 1e6) {
        enteredFreqValue = rtrimZeros(String(fHz / 1e6, 9));
        enteredUnitValue = "MHz";
    } else if (fHz >= 1e3) {
        enteredFreqValue = rtrimZeros(String(fHz / 1e3, 9));
        enteredUnitValue = "KHz";
    } else {
        enteredFreqValue = String(fHz, 0);
        enteredUnitValue = "Hz";
    }

    // Visual update guard.
    if (currentMenu == MAIN_MENU) {
        SetFreqUnitOnMainMenu(enteredUnitValue);
        SetFreqOnMainMenu(enteredFreqValue);
    }
    SetLock(lock); // The lock icon is in the bottom bar, so it can always be redrawn.
}


void SetStartFreqOnSweepMenu(String value)
{
  tft.fillRect(66, 5, 115, 32, TFT_WHITE); // Clear old values
  tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setCursor(66, 30);
  tft.print(value + StartUnitForSweepMenu.substring(0, 1));

}
void SetStopFreqOnSweepMenu(String value)
{
  tft.fillRect(66, 47, 115, 32, TFT_WHITE); // Clear old values
  tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setCursor(66, 72);
  tft.print(value + StopUnitForSweepMenu.substring(0, 1));
}
void SetStepFreqOnSweepMenu(String value)
{
  tft.fillRect(66, 90, 115, 32, TFT_WHITE); // Clear old values
  tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setCursor(66, 115);
  tft.print(value + StepUnitForSweepMenu.substring(0, 1));
}
void SetDwellFreqOnSweepMenu(String value)
{
  tft.fillRect(66, 133, 115, 32, TFT_WHITE); // Clear old values
  tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setCursor(66, 158);
  tft.print(value);
}
void SetAmpAmpOnSweepMenu(String value)
{
  tft.fillRect(256, 5, 55, 32, TFT_WHITE); // Clear old values
  tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setCursor(256, 30);
  tft.print(value);
}
void SetSCountOnSweepMenu(String value)
{
  tft.fillRect(256, 47, 55, 32, TFT_WHITE); // Clear old values
  tft.setTextColor(tft.color565(50, 50, 50), TFT_WHITE);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setCursor(256, 72);
  tft.print(value);
}


void ApplyFilter(bool enable) {
    FilterStatus = enable;
    SetFilterState(FilterStatus);
    if (currentMenu == MAIN_MENU) {
        SetFilter(FilterStatus);
    }
}

#define ENABLE_SWEEP_PROFILING  1  // Enables or disables printing timing statistics

// ====================================================================
// --- External sweep dependencies used by the sweep engine ---
// ====================================================================
struct CalibData {
    uint16_t freq_MHz;
    int8_t att6_on;
    int8_t att3_on;
    int8_t att_n3_on;
    int8_t att6_off;
    int8_t att3_off;
    int8_t att_n3_off;
};
extern CalibData calibTable[];
extern uint16_t calibCount;
extern void Lmx2820SetOUTA_PWR(uint8_t OUTA_PWR_i);
extern void Lmx2820SetOUTB_PWR(uint8_t OUTB_PWR_i);

// ====================================================================
// Main sweep function
// ====================================================================

// -----------------------------------------------------------------------------
// Non-blocking sweep engine
// Advances the frequency according to dwell time, updates PLL/filter state, and
// calculates the attenuation value from calibration data.
// -----------------------------------------------------------------------------
// Called whenever a sweep reaches its stop frequency and is about to wrap
// back around to the start frequency (i.e. one full sweep cycle has just
// completed). Increments the completed-cycle counter and, if the user has
// configured a non-zero sweep count (CountValueForSweepMenu) that has now
// been reached, stops the sweep automatically - mirroring the same actions
// taken when the Start/Stop button is pressed on the touchscreen.
// CountValueForSweepMenu == "0" (the default) means "run forever", matching
// the sweep's previous (unlimited) behavior.
static bool SweepCycleCompleted_ShouldStop() {
    sweepCycleCount++;
    long targetCount = CountValueForSweepMenu.toInt(); // 0 = run forever
    if (targetCount > 0 && sweepCycleCount >= (uint32_t)targetCount) {
        isSweepRunning = false;
        tft.pushImage(270, 125, 36, 36, (uint16_t*)Play);
        SetPLL1OnOff(false);
        // Distinct, easy-to-match status line so a connected PC/GUI can tell
        // the sweep stopped by itself (Count reached) rather than because the
        // user pressed Stop - the GUI already reads every line sent over
        // serial during a sweep, so no new polling/query is required.
        SetRfOnOff(false);
        rfOutputEnabled = false;

        Serial.println("SWEEP:DONE");
        return true;
    }
    return false;
}

void RunSweep()
{
   static unsigned long lastUpdate = 0;

  // --- Exit conditions first ---
  if (!isSweepRunning) return;
  if (currentMenu != SWEEP_MENU) return;

  // --- Dwell timer check first (non-blocking) ---
  unsigned long dwellTime = DwellValueForSweepMenu.toInt()*1000;
  unsigned long now = micros();
  if (now - lastUpdate < dwellTime) return;  // exit early if dwell not expired
  lastUpdate = now;


  #if ENABLE_SWEEP_PROFILING
    static unsigned long lastMicros = micros();
    static unsigned long deltaArray[100];
    static int deltaIndex = 0;

    unsigned long thisMicros = micros();
    unsigned long delta = thisMicros - lastMicros;
    lastMicros = thisMicros;

    deltaArray[deltaIndex++] = delta;

    if (deltaIndex >= 100) {
      unsigned long minVal = 99999999, maxVal = 0, sum = 0;
      for (int i = 0; i < 100; i++) {
        if (deltaArray[i] < minVal) minVal = deltaArray[i];
        if (deltaArray[i] > maxVal) maxVal = deltaArray[i];
        sum += deltaArray[i];
      }
      float avg = sum / 100.0;
      // Serial.printf("\n[SWEEP TIMING] Min: %lu us | Max: %lu us | Avg: %.1f us\n", minVal, maxVal, avg);
      deltaIndex = 0;  // reset buffer
    }
  #endif

 
    // --- Sweep parameters are stored with units ---
    double startHz = StartValueForSweepMenu.toDouble();
    if (StartUnitForSweepMenu == "KHz") startHz *= 1e3;
    else if (StartUnitForSweepMenu == "MHz") startHz *= 1e6;
    else if (StartUnitForSweepMenu == "GHz") startHz *= 1e9;

    double stopHz = StopValueForSweepMenu.toDouble();
    if (StopUnitForSweepMenu == "KHz") stopHz *= 1e3;
    else if (StopUnitForSweepMenu == "MHz") stopHz *= 1e6;
    else if (StopUnitForSweepMenu == "GHz") stopHz *= 1e9;

    double stepHz = StepValueForSweepMenu.toDouble();
    if (StepUnitForSweepMenu == "KHz") stepHz *= 1e3;
    else if (StepUnitForSweepMenu == "MHz") stepHz *= 1e6;
    else if (StepUnitForSweepMenu == "GHz") stepHz *= 1e9;


  // --- Initialize if out of range ---
  if (currentHz == 0 || currentHz < startHz || currentHz > stopHz)
      currentHz = startHz;

  // --- Apply frequency ---
  Lmx2820SetFreqinMHz_Fast(currentHz / 1e6 , 10000000);

  // Update hardware filter bands in real time during sweep.
  extern bool FilterStatus; 
  if (FilterStatus) {
      SetFilterBand(currentHz / 1e6);
  }

  // ====================================================================
  // High-speed power and attenuation control engine.
  // ====================================================================
  float targetDBm = AmpValueSweepForSweepMenu.toFloat();
  uint8_t selectedLO;
  float refPower;

  // 1. LO level selection based on the defined power-control rules.
  if (targetDBm >= 6.0) {
      selectedLO = 7;
      refPower = 6.0;
  } else if (targetDBm >= -2.0) {
      selectedLO = 2;
      refPower = 3.0;
  } else {
      selectedLO = 0;
      refPower = -3.0;
  }

  Lmx2820SetOUTA_PWR(selectedLO);
  Lmx2820SetOUTB_PWR(selectedLO);

  // 2. Read calibration values from RAM and interpolate.
  uint16_t freqMHz = (uint16_t)(currentHz / 1e6);
  float finalAtt = 31.0; 

  if (calibCount > 0) {
      int idx_low = 0;
      int idx_high = calibCount - 1;
      
      for(int i = 0; i < calibCount - 1; i++) {
          if(freqMHz >= calibTable[i].freq_MHz && freqMHz <= calibTable[i+1].freq_MHz) {
              idx_low = i;
              idx_high = i + 1;
              break;
          }
      }

      auto getAtt = [&](int idx) -> float {
          float val = 31.0;
          if(refPower == 6.0) val = FilterStatus ? calibTable[idx].att6_on : calibTable[idx].att6_off;
          else if(refPower == 3.0) val = FilterStatus ? calibTable[idx].att3_on : calibTable[idx].att3_off;
          else val = FilterStatus ? calibTable[idx].att_n3_on : calibTable[idx].att_n3_off;
          return (val < 0) ? 31.0 : val;
      };

      float att_low = getAtt(idx_low);
      float att_high = getAtt(idx_high);

      float baseAtt = att_low;
      if (calibTable[idx_high].freq_MHz != calibTable[idx_low].freq_MHz) {
          float ratio = (float)(freqMHz - calibTable[idx_low].freq_MHz) / (calibTable[idx_high].freq_MHz - calibTable[idx_low].freq_MHz);
          baseAtt = att_low + ratio * (att_high - att_low);
      }

      // 3. Apply the target power difference.
      float powerDiff = targetDBm - refPower; 
      finalAtt = baseAtt - powerDiff;
  }

  // 4. Clamp the attenuation value and apply it to the hardware.
  if (finalAtt < 0.0) finalAtt = 0.0;
  if (finalAtt > 31.5) finalAtt = 31.5;
  finalAtt = (int)finalAtt; 
  
  SetAttenuator((uint8_t)finalAtt); 
  // ====================================================================

  // --- Log current sweep step ---
  // Serial.printf("Freq(MHz): %.3f MHz, Att: %d, LO: %d\n", currentHz / 1e6, (int)finalAtt, selectedLO);

  // --- Step logic ---
  if (StepTypeValueForSweepMenu == "Log") {
      // Logarithmic sweep (exponential increment)
      const double LogConstant = 2.0;  
      double ratio = 1.0 + (LogConstant * stepHz / startHz);  

      if (currentHz < stopHz) {
          currentHz *= ratio;  
      } 
      else {
          // Reached the stop frequency - one full sweep cycle just completed.
          currentHz = startHz;
          if (SweepCycleCompleted_ShouldStop()) return;
      }
  }
  else {  
      // Linear sweep (constant step increment)
      double nextHz = currentHz + stepHz;
      
      if (nextHz > stopHz) {
          // Reached the stop frequency - one full sweep cycle just completed.
          nextHz = startHz;
          if (SweepCycleCompleted_ShouldStop()) { currentHz = startHz; return; }
      }

      currentHz = nextHz;
  }
}


void SetTypeOnSweepMenu(String type) {
    if (currentMenu != SWEEP_MENU) return;
    
    if (type == "Lin") {
        tft.pushImage(190, 90, 32, 32, (uint16_t*)Lin);
        tft.fillRect(222, 90, 95, 32, tft.color565(151, 186, 218)); 
        tft.setTextColor(tft.color565(50, 50, 50), tft.color565(151, 186, 218));
        tft.setFreeFont(&FreeSansBold12pt7b);
        tft.setCursor(230, 115);
        tft.print("Linear");
    } else {
        tft.pushImage(190, 90, 32, 32, (uint16_t*)Log);
        tft.fillRect(222, 90, 95, 32, tft.color565(151, 186, 218)); 
        tft.setTextColor(tft.color565(50, 50, 50), tft.color565(151, 186, 218));
        tft.setFreeFont(&FreeSansBold12pt7b);
        tft.setCursor(230, 115);
        tft.print("Logm");
    }
}