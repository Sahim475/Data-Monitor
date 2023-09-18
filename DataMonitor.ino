#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>

//To execute with python script (still requires Arduino Uno)
/*
  PythonVenv\Scripts\activate.bat
  py PythonVenv\PythonHost.py COM3
*/

Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();

//Channel struct data type
typedef struct
{
  char Name;
  byte Value;
  byte Min;
  byte Max;
  byte AverageValue;
  int AverageIndex;
  byte *Pointer;
  char Description[15];
  boolean AboveBounds;
  boolean BelowBounds;
  boolean HasValue;
}  Channel;

//Array if 26 channels
Channel Channels[26];

byte AboveBoundsCount = 0;
byte BelowBoundsCount = 0;
//Search critera
boolean SearchBelowBounds = false;
boolean SearchAboveBounds = false;

//FSM states
enum state_e {SYNCHRONISATION, INITIALISATION, AWAITING_MESSAGE, PROCESSING_BUTTONS, PROCESSING_PROTOCOL};

void setup() {
  //Initialise Serial monitor and LCD
  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.clear();

  //ClearAllEEPROM();
}

#include <EEPROM.h>

void ClearAllEEPROM() {
  //Wipe EEPROM memory, sets every value to 255 - indicating it has never been written to
  for (int i = 0; i < 1024; i++) {
    EEPROM.update(i, 255);
  }
}

void CheckEEPROM() {

  const String StudentID = F("F128562");

  //Reads first 7 bytes from EEPROM
  String EEPROM_ID = "";
  for (byte i = 0; i < 7; i++) {
    EEPROM_ID += (char)  EEPROM.read(i);
  }
  //If this read string does not matche my student ID
  if (!EEPROM_ID.equals(StudentID)) {
    //Wipe the eprom memory
    ClearAllEEPROM();
    //Then sets first 7 bytes to my student ID
    for (byte i = 0; i < 7; i++) {
      EEPROM.update(i, StudentID.charAt(i));
    }
  }
}

void WriteToEEPROM(byte ByteData, String StrData, byte DataType, char ChannelName) {
  //Partitions EEPROM into 26 segments of 39 bytes each
  //For each partition:
  //1st byte assinged to channel Name                 DataType = 0
  //2st byte assinged to channel Min                  DataType = 1
  //3st byte assinged to channel Max                  DataType = 2
  //Next 15 bytes assinged to channel description     DataType = 3

  int index = 39 * ((byte) ChannelName - 65) + DataType + 7;
  //If not channel description
  if (DataType != 3) {
    //Writes byte data to EEPROM
    EEPROM.update(index , ByteData);
  }//If channel description
  else {
    //Writes string data to EEPROM
    for (byte i = 0; i < StrData.length(); i++) {
      EEPROM.update(index + i, StrData.charAt(i));
    }
    for (byte i = StrData.length(); i < 15; i++) {
      EEPROM.update(index + i, ' ');
    }
  }
}

void ReadFromEEPROM() {
  //Initialise 26 channels
  for (byte i = 0; i < 26; i++) {
    int index = 39 * i + 7;
    //Gets channel name from EEPROM
    char PrevName = EEPROM.read(index);
    //If name does not exist
    if (PrevName == (char) 255) {
      //Sets name to nothing (meaning it wont be displayed)
      PrevName = ' ';
    }
    //Gets channel min from EEPROM
    byte PrevMin = EEPROM.read(index + 1);
    //If min does not exist
    if (PrevMin ==  255) {
      //Sets min to 0 (defualt min)
      PrevMin = 0;
    }
    //Gets channel max from EEPROM
    byte PrevMax = EEPROM.read(index  + 2);
    //If max does not exist
    if (PrevMax == 255) {
      //Sets max to 255 (defualt max)
      PrevMax = 255;
    }

    //' ' indicates that channel has not been created
    //defualt values:  value = 0, min = 0, max = 255, AvgValue = 0
    Channels[i] = {PrevName, 0, PrevMin, PrevMax, 0, -1, (byte*)0, false, false, false};

    //Reads description from EEPROM
    byte k = 3;
    while (k < 18 && EEPROM.read(index + k) != 255) {
      Channels[i].Description[k - 3] = (char) EEPROM.read(index + k);
      k++;
    }
    //Fills in remaining descriptin with blank spaces (until 15 characters long)
    while (k < 18) {
      Channels[i].Description[k - 3] = ' ';
      k++;
    }
  }
}

char FindNextChannelAbove(char CurrentName) {
  //Returns nothing if name is nothing
  if (CurrentName == ' ') {
    return ' ';
  }
  //Find index of channel in channels array based on channel name
  byte index = (byte) CurrentName - 65;
  char NextName = ' ';
  //Return nothing if top channel (A channel)
  if (index > 0) {
    index--;
  }
  else {
    return ' ';
  }
  //If searching for above bounds
  if (SearchAboveBounds) {
    //Finds next channel above current channel that is above bounds
    while (index > 0 && (Channels[index].Name == ' ' || !Channels[index].AboveBounds)) {
      index--;
    }
    if (Channels[index].Name != ' ' && Channels[index].AboveBounds) {
      NextName = Channels[index].Name;
    }
  } //If searching for below bounds
  else if (SearchBelowBounds) {
    //Finds next channel above current channel that is below bounds
    while (index > 0 && (Channels[index].Name == ' ' || !Channels[index].BelowBounds)) {
      index--;
    }
    if (Channels[index].Name != ' ' && Channels[index].BelowBounds) {
      NextName = Channels[index].Name;
    }
  } //If searching for next
  else {
    //Finds next channel above current channel
    while (index > 0 && Channels[index].Name == ' ') {
      index--;
    }
    if (Channels[index].Name != ' ') {
      NextName = Channels[index].Name;
    }
  }
  //Returns next channel name
  return NextName;
}

char FindNextChannelBelow(char CurrentName) {
  //Returns nothing if channel name is nothing
  if (CurrentName == ' ') {
    return ' ';
  }
  //Find index of channel in channels array based on channel name
  byte index = (byte) CurrentName - 65;
  char NextName = ' ';
  //Return nothing if bottom channel (Z channel)
  if (index < 25) {
    index++;
  } else {
    return ' ';
  }
  //If searching for above bounds
  if (SearchAboveBounds) {
    //Finds next channel below current channel this is above bounds
    while (index < 25 && (Channels[index].Name == ' ' || !Channels[index].AboveBounds)) {
      index++;
    }
    if (Channels[index].Name != ' ' && Channels[index].AboveBounds) {
      NextName = Channels[index].Name;
    }
  } //If searching for below bounds
  else if (SearchBelowBounds) {
    //Finds next channel below current channel this is below bounds
    while (index < 25 && (Channels[index].Name == ' ' || !Channels[index].BelowBounds)) {
      index++;
    }
    if (Channels[index].Name != ' ' && Channels[index].BelowBounds) {
      NextName = Channels[index].Name;
    }
  } //If searching for next
  else {
    //Finds next channel below current channel
    while (index < 25 && Channels[index].Name == ' ') {
      index++;
    }
    if (Channels[index].Name != ' ') {
      NextName = Channels[index].Name;
    }
  }
  //Returns next channel name
  return NextName;
}

int CalculateChannelAverage(char Name) {
  //Finds index of channel in channels array based on channel name
  byte index = (byte) Name - 65;
  //Sums previous values (up to 64 values)
  float sum = 0;
  int n = Channels[index].AverageIndex;
  if (n == 0) {
    return 0;
  }
  if (n > 64) {
    n = 64;
  }
  for (int i = 0; i < n; i++) {
    sum += *(Channels[index].Pointer + i);
  }
  //Calculates mean value
  float mean = sum / n;
  //Returns mean value rounded to nearest integer
  return round(mean);
}

String RightJustify(int value, boolean HasValue) {
  //Returns empty space if channel does not have a value
  if (!HasValue) {
    const String EmptyValue = F("   ");
    return EmptyValue;
  }
  String RightJustifedString = "   ";
  //Right justifies value
  String ValueString = String(value);
  byte Length = min(3, ValueString.length());
  for (int i = 0; i < Length; i++) {
    RightJustifedString.setCharAt(3 - Length + i, ValueString.charAt(i));
  }
  //Returns right justified value
  return RightJustifedString;
}

void CreateChannel(char NewName, String Message) {
  //Finds index of channel in channels array based on channel name
  byte index = (byte) NewName - 65;
  //Set name and description of channel at that index
  Channels[index].Name = NewName;
  for (byte i = 0; i < min(15, Message.length()); i++) {
    Channels[index].Description[i] = Message.charAt(i);
  }
  for (byte i = min(15, Message.length()); i < 15; i++) {
    Channels[index].Description[i] = ' ';
  }
  //Save channel name and description to EEPROM
  WriteToEEPROM(NewName, "", 0, NewName);
  WriteToEEPROM(255, Message.substring(0, min(15, Message.length())), 3, NewName);
}

void SetChannelValue(char Name, byte NewValue) {
  //Finds index of channel in channels array based on channel name
  byte index = (byte) Name - 65;
  //Set value of channel at that index
  Channels[index].Value = NewValue;
  if (!Channels[index].HasValue) {
    Channels[index].HasValue = true;
  }
  //If there is enough memory and the channel does not currently store 64 previous values
  if (Channels[index].AverageIndex == -1 && MemoryGap() >= 400) {
    //Frees previous pointer
    free(Channels[index].Pointer);
    //Allocates 64 bytes for this channel to store the 64 previous values
    byte *p = (byte*)malloc(sizeof(byte) * 64);
    Channels[index].Pointer = p;
    Channels[index].AverageIndex = 0;
  } //Otherwise if the channel does not currently store 64 previous values and there is not enough memory
  else if (Channels[index].AverageIndex == -1 && MemoryGap() <  400) {
    //Prints lack of memory error message
    const String MemoryErrorMessage = F("Not enough memory, previous values will not be stored and the avarage will not be displayed for this channel");
    Serial.println(MemoryErrorMessage);
  }
  //If the channel currently store 64 previous values
  if (Channels[index].AverageIndex != -1) {
    int n = Channels[index].AverageIndex;
    Channels[index].AverageIndex = n + 1;
    //If there are more than 64 values stored then overwrite oldest values first
    n %= 64;
    //Stores new value
    *(Channels[index].Pointer + n) = NewValue;
    //Calculates mean value for this channel
    Channels[index].AverageValue = CalculateChannelAverage(Name);
  }
}

void SetChannelMin(char Name, byte NewMin) {
  //Finds index of channel in channels array based on channel name
  byte index = (byte) Name - 65;
  //Set min of channel at that index
  Channels[index].Min = NewMin;
  //Save channel min to EEPROM
  WriteToEEPROM(NewMin, "", 1, Name);
}

void SetChannelMax(char Name, byte NewMax) {
  //Finds index of channel in channels array based on channel name
  byte index = (byte) Name - 65;
  //Set max of channel at that index
  Channels[index].Max = NewMax;
  //Save channel max to EEPROM
  WriteToEEPROM(NewMax, "", 2, Name);
}

boolean ValidProtocolInput(String Message) {
  //Returns invalid if message is below 2 characters
  if (Message.length() < 2) {
    return false;
  }
  //Gets protocol type and channel name
  char ProtocolType = Message.charAt(0);
  char ChannelName = Message.charAt(1);
  Message = Message.substring(2, Message.length());
  //Finds index of channel in channels array based on channel name
  byte LetterValue = (byte) ChannelName - 65;
  //If the channel name is not between A-Z the return invalid
  if (LetterValue > 25) {
    return false;
  }
  //Return invalid if protocol type not C, V, X, or N
  switch (ProtocolType) {
    case 'C':
      break;
    case 'V':
    case 'X':
    case 'N': {
        //Return invalid if chanel has not been created yet
        if (Channels[LetterValue].Name == ' ') {
          return false;
        }
        //Returns false if message does not contain any data other than channel name and protcol type
        if (Message.length() == 0) {
          return false;
        }
        //Return invalid if remainder of message is not an integer
        for (int i = 0; i < Message.length(); i++) {
          if (!isdigit(Message.charAt(i))) {
            return false;
          }
        }
        int value = Message.toInt();
        //Returns invalid if integer value if outside of byte bounds
        if (value > 255 || value < 0) {
          return false;
        }
        break;
      }
    //if not C, V, X, or N
    default:
      return false;
      break;
  }
  //Returns valid
  return true;
}

extern char *__brkval;
int MemoryGap() {
  char top;
  //Top of stack - top of heap = memory available
  //Returns number of unallocated bytes in memory
  return (int)&top - (int)__brkval;
}

void UpdateDisplay(char FirstName, char SecondName, boolean SelectDisplay, byte TopLineScroll, byte BottomLineScroll) {
  const String StudentID = F("F128562                ");
  const String RamMessage = F("Free SRAM: ");
  const String OneLineSpace = F("                ");
  //If select has been held for more than 1 second
  if (SelectDisplay) {
    //Displays student ID and amount of free SRAM
    lcd.setCursor(0, 0);
    lcd.print(StudentID);
    lcd.setCursor(0, 1);
    lcd.print(RamMessage);
    lcd.print(MemoryGap());
    lcd.print('B');
    lcd.print(OneLineSpace);
  } else {
    //Finds indexes of channels in channels array based on channel names
    byte FirstIndex = (byte) FirstName - 65;
    byte LastIndex = (byte) SecondName - 65;

    byte UpArrow[] = {B00000, B00000, B00100, B01010, B10001, B00000, B00000, B00000};
    byte DownArrow[] = {B00000, B00000, B00000, B10001, B01010, B00100, B00000, B00000};

    //If top line channel has been created
    if (FirstName != ' ') {
      //If there is a channel above the top line channel
      if (FindNextChannelAbove(FirstName) != ' ') {
        //Displays up arrow
        lcd.createChar(0, UpArrow);
        lcd.setCursor(0, 0);
        lcd.write(0);
      } //Otherwise (if there is not a channel above the top line channel)
      else {
        //Displays no arrow
        lcd.setCursor(0, 0);
        lcd.print(' ');
      }
      lcd.setCursor(1, 0);
      //Displays channel name
      lcd.print(Channels[FirstIndex].Name);
      //Displays channel value
      lcd.print(RightJustify(Channels[FirstIndex].Value, Channels[FirstIndex].HasValue));
      lcd.print(',');
      //Displays channel average value
      lcd.print(RightJustify(Channels[FirstIndex].AverageValue, Channels[FirstIndex].HasValue && Channels[FirstIndex].AverageIndex != -1));
      lcd.print(' ');
      //Displays channel description at scroll index
      for (byte i = TopLineScroll; i < min(15, TopLineScroll + 6); i++) {
        lcd.print(Channels[FirstIndex].Description[i]);
      }
      lcd.print(OneLineSpace);
    } //otherwise (if top line channel has not been created)
    else {
      //Displays empty line
      lcd.setCursor(0, 0);
      lcd.print(OneLineSpace);
    }

    //If bottom line channel has been created
    if (SecondName != ' ') {
      //If there is a channel below the line line channel
      if (FindNextChannelBelow(SecondName) != ' ') {
        //Displays down arrow
        lcd.createChar(1, DownArrow);
        lcd.setCursor(0, 1);
        lcd.write(1);
      } //Otherwise (if there is not a channel below the bottom line channel)
      else {
        //Displays no arrow
        lcd.setCursor(0, 1);
        lcd.print(' ');
      }
      lcd.setCursor(1, 1);
      //Displays channel name
      lcd.print(Channels[LastIndex].Name);
      //Displays channel value
      lcd.print(RightJustify(Channels[LastIndex].Value, Channels[LastIndex].HasValue));
      lcd.print(',');
      //Displays channel average value
      lcd.print(RightJustify(Channels[LastIndex].AverageValue, Channels[LastIndex].HasValue && Channels[LastIndex].AverageIndex != -1));
      lcd.print(' ');
      //Displays channel description at scroll index
      for (byte i = BottomLineScroll; i < min(15, BottomLineScroll + 6); i++) {
        lcd.print(Channels[LastIndex].Description[i]);
      }
      lcd.print(OneLineSpace);
    } //otherwise (if bottom line channel has not been created)
    else {
      //Displays empty line
      lcd.setCursor(0, 1);
      lcd.print(OneLineSpace);
    }
  }
}

void UpdateBacklight() {
  //Changes backlight colour to yellow if there exists a channel that is below bounds and a channel that is above bounds
  if (AboveBoundsCount > 0 && BelowBoundsCount > 0) {
    lcd.setBacklight(3);
  } //Otherwise, changes backlight colour to red if there exists a channel that is above bounds
  else if (AboveBoundsCount > 0) {
    lcd.setBacklight(1);
  }//Otherwise, changes backlight colour to green if there exists a channel that is below bounds
  else if (BelowBoundsCount > 0) {
    lcd.setBacklight(2);
  } //Otherwise, changes backlight colour to white
  else {
    lcd.setBacklight(7);
  }
}

byte GetDescriptionLength(char ChannelName) {
  //If channel has not been created
  if (ChannelName == ' ') {
    //return 0
    return 0;
  }
  //Finds index of channel in channels array based on channel name
  byte index = (byte) ChannelName - 65;

  //Returns length of channel description
  byte LastIndex = 0;
  for (byte i = 0; i < 15; i++) {
    if (Channels[index].Description[i] != ' ') {
      LastIndex = i + 1;
    }
  }
  return LastIndex;
}

//Checks if channel's value if within the channel's bounds
void CheckBounds(char ChannelName) {
  //Finds index of channel in channels array based on channel name
  byte index = (byte) ChannelName - 65;
  //Only check bounds if Min <= Max for this channel and the channel has a value
  if (Channels[index].Min <= Channels[index].Max && Channels[index].HasValue) {
    byte newValue = Channels[index].Value;
    //If value above max for first time
    if (newValue > Channels[index].Max && !Channels[index].AboveBounds) {
      //Increases above bounds count by 1
      Channels[index].AboveBounds = true;
      AboveBoundsCount++;
    } //If value now below max and previously was above
    else if (newValue <= Channels[index].Max && Channels[index].AboveBounds) {
      //Decreases above bounds count by 1
      Channels[index].AboveBounds = false;
      AboveBoundsCount--;
    }
    //If value below min for first time
    if (newValue < Channels[index].Min && !Channels[index].BelowBounds) {
      //Increases below bounds count by 1
      Channels[index].BelowBounds = true;
      BelowBoundsCount++;
    }//If value now above min and previously was below
    else if (newValue >= Channels[index].Min && Channels[index].BelowBounds) {
      //Decreases below bounds count by 1
      Channels[index].BelowBounds = false;
      BelowBoundsCount--;
    }
  }//Ignores channel from backlight calculation
  else {
    //Removes channel from above and below bounds count
    if (Channels[index].BelowBounds) {
      Channels[index].BelowBounds = false;
      BelowBoundsCount--;
    }
    if (Channels[index].AboveBounds) {
      Channels[index].AboveBounds = false;
      AboveBoundsCount--;
    }
  }
}

boolean ValidChannel(char ChannelName) {
  //If channel has not been created
  if (ChannelName == ' ') {
    //Channel is invalid
    return false;
  }
  //Finds index of channel in channels array based on channel name
  byte index = (byte) ChannelName - 65;
  //Returns whether channel fits above and/or below bounds search criteria
  if (SearchAboveBounds && SearchBelowBounds) {
    if (!Channels[index].AboveBounds && !Channels[index].BelowBounds) {
      //Channel is invalid according to current search criteria
      return false;
    }
  } else if (SearchAboveBounds) {
    if (!Channels[index].AboveBounds) {
      //Channel is invalid according to current search criteria
      return false;
    }
  } else if (SearchBelowBounds) {
    if (!Channels[index].BelowBounds) {
      //Channel is invalid according to current search criteria
      return false;
    }
  }
  //Channel is valid according to current search criteria
  return true;
}

//Top line channel and botton line channel set to nothing initially
char TopChannelName = ' ';
char BottomChannelName = ' ';

void FindValidLines() {
  //If above and/or below bounds search
  if (SearchAboveBounds || SearchBelowBounds) {

    //If top line channel is nothing
    if (TopChannelName == ' ') {
      //Find first channel in alphebetical order and sets it to top line channel
      byte index = 0;
      while (index < 26 && !ValidChannel(Channels[index].Name)) {
        index++;
      }
      if (index != 26) {
        TopChannelName = Channels[index].Name;
      }
    }
    //If top channel does not meet search critera
    if (!ValidChannel(TopChannelName)) {
      //Looks above top line channel with search critera
      char NewChannelName = FindNextChannelAbove(TopChannelName);
      if (NewChannelName != ' ') {
        TopChannelName = NewChannelName;
      } //If no channel found
      else {
        //Looks below top line channel with search critera
        char NewChannelName = FindNextChannelBelow(TopChannelName);
        if (NewChannelName != ' ') {
          TopChannelName = NewChannelName;
        } //If no channel found then no channels meet search critera
        else {
          //Sets both lines to nothing
          TopChannelName = ' ';
          BottomChannelName = ' ';
        }
      }
    }
    //If top line channel is not nothing
    if (TopChannelName != ' ') {
      //Looks below top line channel with search critera
      char NewChannelName = FindNextChannelBelow(TopChannelName);
      if (NewChannelName != ' ' ) {
        BottomChannelName = NewChannelName;
      } //If no channel found
      else {
        //Looks above top line channel with search critera
        char NewChannelName = FindNextChannelAbove(TopChannelName);
        //If channel found then swap lines
        if (NewChannelName != ' ') {
          BottomChannelName = TopChannelName;
          TopChannelName = NewChannelName;
        } //If no channel found then no other channels meet search criter
        else {
          //Bottom line set to nothing
          BottomChannelName = ' ';
        }
      }
    }
  } //Othwerwise (if not above and/or below bounds search)
  else {
    //If top line channel is nothing
    if (TopChannelName == ' ') {
      //Find first channel in alphebetical order and sets it to top line channel
      byte index = 0;
      while (index < 26 && Channels[index].Name == ' ') {
        index++;
      }
      if (index != 26) {
        TopChannelName = Channels[index].Name;
      }
    }
    //Find next channel below in top line channel
    BottomChannelName = FindNextChannelBelow(TopChannelName);
  }
}

void loop() {
  //Starts in SYNCHRONISATION state
  static enum state_e state = SYNCHRONISATION;
  static String Message = "";
  //Scroll indexes set to 0 initially
  static byte TopLineDescLength = 0;
  static byte BottomLineDescLength = 0;
  static byte TopLineScroll = 0;
  static byte BottomLineScroll = 0;
  static boolean SelectDisplay = false;
  static byte ButtonInput = 0;
  static byte PreviousButtonInput = 0;
  static byte PreviousSelectButtonInput = 0;
  static long SelectPressTime = millis();
  static long PreviosScrollTime = millis();

  switch (state) {

    //SYNCHRONISATION state
    case SYNCHRONISATION: {
        //Sets backlight to purple
        lcd.setBacklight(5);
        char IncomingCharacter = ' ';
        do {
          //Continually send 'Q' until message is recieved
          while (!Serial.available()) {
            Serial.print('Q');
            //1 second delay
            delay(1000);
          }
          //Read first character of input
          IncomingCharacter = Serial.read();
          while (Serial.available()) {
            Serial.read();
            //Delay to allow byte to arrive in input buffer
            delay(2);
          }
          //End loop if 'X' is received
        } while (IncomingCharacter != 'X');
        //Print "UDCHARS,FREERAM,HCI,EEPROM,RECENT,NAMES,SCROLL"
        Serial.println("\nUDCHARS,FREERAM,HCI,EEPROM,RECENT,NAMES,SCROLL");
        //Sets backlight to white
        lcd.setBacklight(7);
        //Transition to INITIALISATION state
        state = INITIALISATION;
        break;
      }

    //INITIALISATION state
    case INITIALISATION: {
        //Wipes EEPROM if it has been written to by another program
        CheckEEPROM();
        //Reads channel data from EEPROM
        ReadFromEEPROM();
        //Sets top and bottom line channels
        byte i = 0;
        while (i < 26 && (TopChannelName == ' ' || BottomChannelName == ' ')) {
          if (Channels[i].Name != ' ') {
            if (TopChannelName == ' ') {
              TopChannelName = Channels[i].Name;
              TopLineScroll = 0;
              TopLineDescLength = GetDescriptionLength(TopChannelName);
            } else if (BottomChannelName == ' ') {
              BottomChannelName = Channels[i].Name;
              BottomLineScroll = 0;
              BottomLineDescLength = GetDescriptionLength(BottomChannelName);
              break;
            }
          }
          i++;
        }
        //Updates the display with two empty channels
        UpdateDisplay(TopChannelName, BottomChannelName,  SelectDisplay, TopLineScroll, BottomLineScroll);
        //Transition to AWAITING_MESSAGE state
        state = AWAITING_MESSAGE;
        break;
      }

    //AWAITING_MESSAGE state
    case AWAITING_MESSAGE:
      //If message not availabe and any button pressed, transition to PROCESSING_BUTTONS
      state = PROCESSING_BUTTONS;
      //If message availalbe and valid, transition to PROCESSING_PROTOCOL
      if (Serial.available()) {
        //Read message from serial monitor (17 characters max)
        Message = "";
        byte i = 0;
        while (i < 17 && Serial.available() > 0) {
          char inputChar = (char) Serial.read();
          if (inputChar == '\n') {
            break;
          }
          Message += inputChar;
          //Delay to allow byte to arrive in input buffer
          delay(2);
          i++;
        }
        //Ignore remander of the message
        while (Serial.available() > 0) {
          char inputChar = (char) Serial.read();
          if (inputChar == '\n') {
            break;
          }
          //Delay to allow byte to arrive in input buffer
          delay(2);
        }

        Message.trim();

        //If input protocol is valid
        if (ValidProtocolInput(Message)) {
          state = PROCESSING_PROTOCOL;
        } //Otherwise (If input protocol is invalid)
        else {
          //Prints error message
          Serial.print("ERROR: ");
          Serial.println(Message);
        }
      }

      //Every half-second
      if (millis() - PreviosScrollTime >= 500) {
        PreviosScrollTime = millis();
        //If top line channel description too long to be displayed
        if (TopLineDescLength > 6) {
          //Scrolls top line channel description by 2 characters
          TopLineScroll ++;
          if (TopLineScroll >= TopLineDescLength) {
            TopLineScroll = 0;
          }
        } else {
          TopLineScroll = 0;
        }
        //If bottom line channel description too long to be displayed
        if (BottomLineDescLength > 6) {
          //Scrolls bottom line channel description by 2 characters
          BottomLineScroll ++;
          if (BottomLineScroll >= BottomLineDescLength) {
            BottomLineScroll = 0;
          }
        } else {
          BottomLineScroll = 0;
        }
        //Updates the display
        UpdateDisplay(TopChannelName, BottomChannelName, SelectDisplay, TopLineScroll, BottomLineScroll);
      }
      break;

    //PROCESSING_BUTTONS state
    case PROCESSING_BUTTONS: {
        //Get button input
        PreviousButtonInput = ButtonInput;
        ButtonInput = lcd.readButtons();
        //If Select held and Select not held last time
        if (!(BUTTON_SELECT & ButtonInput & ~PreviousSelectButtonInput)) {
          //Find time since first press
          SelectPressTime = millis();
          PreviousSelectButtonInput = ButtonInput;
        }

        //If BUTTON_UP pressed
        if (ButtonInput & (BUTTON_UP) & ~PreviousButtonInput) {
          //Find next channel above in alphabetical order
          char NewChannelName = FindNextChannelAbove(TopChannelName);
          //If the next channel is not nothing
          if (NewChannelName != ' ') {
            //Scroll up
            BottomChannelName = TopChannelName;
            BottomLineScroll = 0;
            BottomLineDescLength = GetDescriptionLength(BottomChannelName);

            TopChannelName = NewChannelName;
            TopLineScroll = 0;
            TopLineDescLength = GetDescriptionLength(TopChannelName);

            //Updates the display
            UpdateDisplay(TopChannelName, BottomChannelName,  SelectDisplay, TopLineScroll, BottomLineScroll);
          }


        } //Otherwise if BUTTON_DOWN pressed
        else if (ButtonInput & (BUTTON_DOWN) & ~PreviousButtonInput) {
          //Find next channel below in alphabetical order
          char NewChannelName = FindNextChannelBelow(BottomChannelName);
          //If the next channel is not nothing
          if (NewChannelName != ' ') {
            //Scroll down
            TopChannelName = BottomChannelName;
            TopLineScroll = 0;
            TopLineDescLength = GetDescriptionLength(TopChannelName);

            BottomChannelName = NewChannelName;
            BottomLineScroll = 0;
            BottomLineDescLength = GetDescriptionLength(BottomChannelName);

            //Updates the display
            UpdateDisplay(TopChannelName, BottomChannelName,  SelectDisplay, TopLineScroll, BottomLineScroll);
          }
        }

        boolean SearchBoundsChanged = false;
        //If right button presssed and released
        if (BUTTON_RIGHT & ButtonInput & ~PreviousButtonInput) {
          //Toggles above bounds search
          SearchAboveBounds = !SearchAboveBounds;
          SearchBelowBounds = false;
          SearchBoundsChanged = true;
        }
        //If left button presssed and released
        if (BUTTON_LEFT & ButtonInput & ~PreviousButtonInput) {
          //Toggles below bounds search
          SearchBelowBounds = !SearchBelowBounds;
          SearchAboveBounds = false;
          SearchBoundsChanged = true;
        }
        //If search bounds have changed
        if (SearchBoundsChanged) {
          //Sets top and bottom lines to nearest channels that meet search critera
          FindValidLines();

          TopLineScroll = 0;
          TopLineDescLength = GetDescriptionLength(TopChannelName);

          BottomLineScroll = 0;
          BottomLineDescLength = GetDescriptionLength(BottomChannelName);

          //Updates the display
          UpdateDisplay(TopChannelName, BottomChannelName,  SelectDisplay, TopLineScroll, BottomLineScroll);
        }

        //If select has been held down for a second
        if (ButtonInput & (BUTTON_SELECT) && millis() - SelectPressTime >= 1000) {
          //Displays Student ID and free RAM
          SelectDisplay = true;
          //Sets backlight to purple
          lcd.setBacklight(5);
          //Updates the display
          UpdateDisplay(TopChannelName, BottomChannelName,  SelectDisplay, TopLineScroll, BottomLineScroll);
        } //Otherwise, toggles off select display
        else if (SelectDisplay) {
          //Does not displays Student ID and free RAM
          SelectDisplay = false;
          //Updates the LCD backlight
          UpdateBacklight();
          //Updates the display
          UpdateDisplay(TopChannelName, BottomChannelName, SelectDisplay, TopLineScroll, BottomLineScroll);
        }
        //Transition to AWAITING_MESSAGE state
        state = AWAITING_MESSAGE;
        break;
      }

    //PROCESSING_PROTOCOL state
    case PROCESSING_PROTOCOL:
      //Get protocol type and channel name form protocol message
      char ProtocolType = Message.charAt(0);
      char ChannelName = Message.charAt(1);
      Message = Message.substring(2, Message.length());

      switch (ProtocolType) {
        //If protocol type = 'C' (new channel)
        case 'C':
          {
            //Creates channel
            CreateChannel(ChannelName, Message);

            if (ChannelName == TopChannelName) {
              TopLineScroll = 0;
              TopLineDescLength = GetDescriptionLength(TopChannelName);
            } else if (ChannelName == BottomChannelName) {
              BottomLineScroll = 0;
              BottomLineDescLength = GetDescriptionLength(BottomChannelName);
            }
            //If the new channel meets the search critera
            if (ValidChannel(ChannelName)) {
              //Fill top line if it is empty
              if (TopChannelName == ' ') {
                TopChannelName = ChannelName;
                TopLineScroll = 0;
                TopLineDescLength = GetDescriptionLength(TopChannelName);
              } //Otherwise fill bottom line if it is empty
              else if (BottomChannelName == ' ' && ChannelName != TopChannelName) {
                BottomChannelName = ChannelName;
                if ((byte) BottomChannelName < (byte) TopChannelName) {
                  //Swap line if in wrong alphabetical order
                  char TempChannelName = TopChannelName;
                  TopChannelName = BottomChannelName;
                  TopLineScroll = 0;
                  TopLineDescLength = GetDescriptionLength(TopChannelName);

                  BottomChannelName = TempChannelName;
                  BottomLineScroll = 0;
                  BottomLineDescLength = GetDescriptionLength(BottomChannelName);
                }
              }//If new channel between top and bottom lines alphabetically
              else if ((byte) ChannelName < (byte) BottomChannelName && (byte) ChannelName > (byte) TopChannelName) {
                //New channel displayed on bottom line
                BottomChannelName = ChannelName;
                BottomLineScroll = 0;
                BottomLineDescLength = GetDescriptionLength(BottomChannelName);
              }
              //Updates the display
              UpdateDisplay(TopChannelName, BottomChannelName, SelectDisplay, TopLineScroll, BottomLineScroll);
            }
            break;
          }
        //If protocol type = 'V' (new value)
        case 'V':
          {
            //Sets channel's value
            SetChannelValue(ChannelName, Message.toInt());
            //Checks whether the channel's value is withing the channel's bounds and updates the below and above counts
            CheckBounds(ChannelName);
            //If either the top or bottom line channels do not now meet the search critera (because of value change)
            if (!ValidChannel(TopChannelName) || !ValidChannel(BottomChannelName))  {
              //Sets top and bottom lines to nearest that meet search critera
              FindValidLines();

              TopLineScroll = 0;
              TopLineDescLength = GetDescriptionLength(TopChannelName);

              BottomLineScroll = 0;
              BottomLineDescLength = GetDescriptionLength(BottomChannelName);
            } //If this channel is valid and is between the top and bottom line channels alphabetically
            else if (ValidChannel(ChannelName) && ((byte) ChannelName < (byte) BottomChannelName && (byte) ChannelName > (byte) TopChannelName)) {
              //New channel displayed on bottom line
              BottomChannelName = ChannelName;
              BottomLineScroll = 0;
              BottomLineDescLength = GetDescriptionLength(BottomChannelName);
            }
            //Updates the LCD backlight
            UpdateBacklight();
            //Updates the display
            UpdateDisplay(TopChannelName, BottomChannelName,  SelectDisplay, TopLineScroll, BottomLineScroll);
            break;
          }
        case 'X':
          {
            //Sets channel's max value
            SetChannelMax(ChannelName, Message.toInt());
            //Checks whether the channel's value is withing the channel's bounds and updates the below and above counts
            CheckBounds(ChannelName);
            //If either the top or bottom line channels do not now meet the search critera (because of bounds change)
            if (!ValidChannel(TopChannelName) || !ValidChannel(BottomChannelName))  {
              //Sets top and bottom lines to nearest that meet search critera
              FindValidLines();

              TopLineScroll = 0;
              TopLineDescLength = GetDescriptionLength(TopChannelName);

              BottomLineScroll = 0;
              BottomLineDescLength = GetDescriptionLength(BottomChannelName);
            } //If this channel is valid and is between the top and bottom line channels alphabetically
            else if (ValidChannel(ChannelName) && ((byte) ChannelName < (byte) BottomChannelName && (byte) ChannelName > (byte) TopChannelName)) {
              //New channel displayed on bottom line
              BottomChannelName = ChannelName;
              BottomLineScroll = 0;
              BottomLineDescLength = GetDescriptionLength(BottomChannelName);
            }
            //Updates the LCD backlight
            UpdateBacklight();
            //Updates the display
            UpdateDisplay(TopChannelName, BottomChannelName,  SelectDisplay, TopLineScroll, BottomLineScroll);
            break;
          }
        case 'N':
          {
            //Sets channel's min value
            SetChannelMin(ChannelName, Message.toInt());
            //Checks whether the channel's value is withing the channel's bounds and updates the below and above counts
            CheckBounds(ChannelName);
            //If either the top or bottom line channels do not now meet the search critera (because of bounds change)
            if (!ValidChannel(TopChannelName) || !ValidChannel(BottomChannelName))  {
              //Sets top and bottom lines to nearest that meet search critera
              FindValidLines();

              TopLineScroll = 0;
              TopLineDescLength = GetDescriptionLength(TopChannelName);

              BottomLineScroll = 0;
              BottomLineDescLength = GetDescriptionLength(BottomChannelName);
            } //If this channel is valid and is between the top and bottom line channels alphabetically
            else if (ValidChannel(ChannelName) && ((byte) ChannelName < (byte) BottomChannelName && (byte) ChannelName > (byte) TopChannelName)) {
              //New channel displayed on bottom line
              BottomChannelName = ChannelName;
              BottomLineScroll = 0;
              BottomLineDescLength = GetDescriptionLength(BottomChannelName);
            }
            //Updates the LCD backlight
            UpdateBacklight();
            //Updates the display
            UpdateDisplay(TopChannelName, BottomChannelName,  SelectDisplay, TopLineScroll, BottomLineScroll);
            break;
          }
      }
      //Transition to AWAITING_MESSAGE state
      state = AWAITING_MESSAGE;
      break;
  }
}
