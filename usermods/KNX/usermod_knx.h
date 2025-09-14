#pragma once

#include <cstdio>
#include "wled.h"
#include <KnxTpUart.h>

struct KnxFunction {
  String name;
  bool enabled;
  String listenGroup;
  String stateGroup;
};

// dim Task
enum DimTask {
    DIM_IDLE,
    DIM_STOP,
    DIM_UP,
    DIM_DOWN,
};

class KnxUsermod : public Usermod {

  private:

    bool enabled = false;
    bool initDone = false;
    unsigned long lastTime = 0;

    static const char _name[];
    static const char _enabled[];
    static const char _address[];
    static const char _group[];
    static const char _state[];
    static const char _time[];
    static const char _invalidaddress[];
    static const char _invalidgroup[];
    static const char _rxPin[];
    static const char _txPin[];

    int8_t txPin; // TX pin to connect to RX pin from KNX UART device
    int8_t rxPin; // RX pin to connect to TX pin from KNX UART device

    String individualAddress;
    KnxFunction switchFunction {"Switch"};
    KnxFunction absoluteDimFunction {"Absolute dim"};
    KnxFunction relativeDimFunction {"Relative dim"};
    KnxFunction colorFunction {"Color"};
    KnxFunction presetsFunction {"Presets"};
    KnxFunction effectFunction {"Effect"};
    KnxFunction paletteFunction {"Palette"};
    //KnxFunction playlistFunction {"Playlist"};

    int relativeDimTime;
    bool isDimming = false;
    unsigned long currentMillis = 0;
    unsigned long time;
    unsigned long lastTaskExecution;
    int currentTask = DimTask::DIM_IDLE;

    std::vector<KnxFunction> knxFunctions;

    byte lastKnownBri = 0;
    byte lastKnownCol[4] = { 0, 0, 0, 0 };

    std::unique_ptr<KnxTpUart> knxPtr;

    void allocatePins();                                                  // Allocate pins for the bus connection
    void updateWLED();                                                    // Update WLED
    void initBus();                                                       // Initialize the bus connection
    void updateFromBus();                                                 // Read telegram from bus and adjust light if necessary
    void populateKnxFunctions();                                          // Populate function vector
    void dimLight();                                                      // Dim light relatively based on bus telegrams
    int countDelimiter(const String& address, const char delimiter);      // Count the delimiter in the address to validate and recognize address style
    bool validateAddress(const String& address);                          // KNX invidual addresses should be in the format X.Y.Z, with X and Y 0-15 and Z 1-255       
    bool validateGroup(const String& address);                            // Group addresses can be in 3-level X/Y/Z (0-31/0-7/0-255), 2-level X/Z (0-31/0-2047) or free style Z (0-65535), and the members can't add up to 0
    bool isGroupTarget(const KnxTelegram telegram, const String& target); // Does the telegram source group match the target group

  public:

    inline void enable(bool enable) { enabled = enable; }
    inline bool isEnabled() { return enabled; }

    void setup() override 
    {
      if (isEnabled()) {
        DEBUG_PRINTLN(F("---Init KNX usermod ---"));
        initBus();
        populateKnxFunctions();
        // default brightness configured in WLED
        lastKnownBri = briS;
        // default color
        lastKnownCol[0] = colPri[0];
        lastKnownCol[1] = colPri[1];
        lastKnownCol[2] = colPri[2];
        lastKnownCol[3] = colPri[3];
        // send setup status to bus
        knxPtr->groupWriteBool(switchFunction.stateGroup, offMode);
        knxPtr->groupWrite1ByteInt(absoluteDimFunction.stateGroup, lastKnownBri);
        int color = (colPri[0] << 24) | (colPri[1] << 16) | (colPri[2] << 8) | colPri[3];
        knxPtr->groupWrite6ByteInt(colorFunction.stateGroup, color);
      }
      initDone = true;
    }

    void loop() override 
    {
      if (!enabled || strip.isUpdating()) return;
      if (knxPtr) {
        updateFromBus();
        dimLight();
      }
    }

    void onStateChange(uint8_t mode) override 
    {
      if (!initDone) return;
      if (mode == CALL_MODE_NO_NOTIFY) return;
      if (knxPtr)
      {
        //knxPtr->groupAnswer14ByteText("6/1/99", String(mode)); // just an example to send state change to group 6/1/99
        // Light Switch State
        if (bri != lastKnownBri) {
          lastKnownBri = bri;
          if (bri) {
            // @FIX: only write if group is valid, use ternary to remove else statement for switch state
            knxPtr->groupWriteBool(switchFunction.stateGroup, true);
            knxPtr->groupWrite1ByteInt(absoluteDimFunction.stateGroup, bri);
          } else {
            knxPtr->groupWriteBool(switchFunction.stateGroup, false);
            knxPtr->groupWrite1ByteInt(absoluteDimFunction.stateGroup, briLast);
          }
        }
        // Color State
        if (colPri[0] != lastKnownCol[0] || colPri[1] != lastKnownCol[1] || colPri[2] != lastKnownCol[2] || colPri[3] != lastKnownCol[3]) {
          lastKnownCol[0] = colPri[0];
          lastKnownCol[1] = colPri[1];
          lastKnownCol[2] = colPri[2];
          lastKnownCol[3] = colPri[3];
          int color = (colPri[0] << 24) | (colPri[1] << 16) | (colPri[2] << 8) | colPri[3];
          knxPtr->groupWrite6ByteInt(colorFunction.stateGroup, color);
        }
      }
    }

    void addToConfig(JsonObject& root) override
    {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top[FPSTR(_enabled)] = enabled;

      JsonObject pins = top.createNestedObject(F("Serial Pins"));
      pins[FPSTR(_txPin)] = txPin;
      pins[FPSTR(_rxPin)] = rxPin;

      JsonObject indivAddr = top.createNestedObject(F("Individual Address"));
      indivAddr[FPSTR(_address)] = validateAddress(individualAddress) ? individualAddress : FPSTR(_invalidaddress);

      JsonObject swGroups = top.createNestedObject(F("Switch Groups"));
      swGroups[FPSTR(_enabled)] = switchFunction.enabled;
      swGroups[FPSTR(_group)] = validateGroup(switchFunction.listenGroup) ? switchFunction.listenGroup : FPSTR(_invalidgroup);
      swGroups[FPSTR(_state)] = validateGroup(switchFunction.stateGroup) ? switchFunction.stateGroup : FPSTR(_invalidgroup);

      JsonObject absDimGroups = top.createNestedObject(F("Absolute Dim Groups"));
      absDimGroups[FPSTR(_enabled)] = absoluteDimFunction.enabled;
      absDimGroups[FPSTR(_group)] = validateGroup(absoluteDimFunction.listenGroup) ? absoluteDimFunction.listenGroup : FPSTR(_invalidgroup);
      absDimGroups[FPSTR(_state)] = validateGroup(absoluteDimFunction.stateGroup) ? absoluteDimFunction.stateGroup : FPSTR(_invalidgroup);

      JsonObject relDimGroups = top.createNestedObject(F("Relative Dim Groups"));
      relDimGroups[FPSTR(_enabled)] = relativeDimFunction.enabled;
      relDimGroups[FPSTR(_group)] = validateGroup(relativeDimFunction.listenGroup) ? relativeDimFunction.listenGroup : FPSTR(_invalidgroup);
      relDimGroups[FPSTR(_time)] = relativeDimTime;

      JsonObject colGroups = top.createNestedObject(F("Color Groups"));
      colGroups[FPSTR(_enabled)] = colorFunction.enabled;
      colGroups[FPSTR(_group)] = validateGroup(colorFunction.listenGroup) ? colorFunction.listenGroup : FPSTR(_invalidgroup);
      colGroups[FPSTR(_state)] = validateGroup(colorFunction.stateGroup) ? colorFunction.stateGroup : FPSTR(_invalidgroup);

      JsonObject presetsGroups = top.createNestedObject(F("Presets Groups"));
      presetsGroups[FPSTR(_enabled)] = presetsFunction.enabled;
      presetsGroups[FPSTR(_group)] = validateGroup(presetsFunction.listenGroup) ? presetsFunction.listenGroup : FPSTR(_invalidgroup);

      JsonObject effectGroups = top.createNestedObject(F("Effect Groups"));
      effectGroups[FPSTR(_enabled)] = effectFunction.enabled;
      effectGroups[FPSTR(_group)] = validateGroup(effectFunction.listenGroup) ? effectFunction.listenGroup : FPSTR(_invalidgroup);

      JsonObject paletteGroups = top.createNestedObject(F("Palette Groups"));
      paletteGroups[FPSTR(_enabled)] = paletteFunction.enabled;
      paletteGroups[FPSTR(_group)] = validateGroup(paletteFunction.listenGroup) ? paletteFunction.listenGroup : FPSTR(_invalidgroup);
    }

    bool readFromConfig(JsonObject& root) override
    {
      JsonObject top = root[FPSTR(_name)];
      // @FIX - incorrect configcomplete
      bool configComplete = !top.isNull();
      configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled);

      JsonObject pins = top[F("Serial Pins")];
      configComplete = !pins.isNull();
      configComplete &= getJsonValue(pins[FPSTR(_rxPin)], rxPin, -1);
      configComplete &= getJsonValue(pins[FPSTR(_txPin)], txPin, -1);

      JsonObject indivAddr = top[F("Individual Address")];
      configComplete = !indivAddr.isNull();
      configComplete &= getJsonValue(indivAddr[FPSTR(_address)], individualAddress, FPSTR(_invalidaddress));

      JsonObject swGroups = top[F("Switch Groups")];
      configComplete = !swGroups.isNull();      
      configComplete &= getJsonValue(swGroups[FPSTR(_enabled)], switchFunction.enabled, false);
      configComplete &= getJsonValue(swGroups[FPSTR(_group)], switchFunction.listenGroup, FPSTR(_invalidgroup));
      configComplete &= getJsonValue(swGroups[FPSTR(_state)], switchFunction.stateGroup, FPSTR(_invalidgroup));

      JsonObject absDimGroups = top[F("Absolute Dim Groups")];
      configComplete = !absDimGroups.isNull();
      configComplete &= getJsonValue(absDimGroups[FPSTR(_enabled)], absoluteDimFunction.enabled, false);
      configComplete &= getJsonValue(absDimGroups[FPSTR(_group)], absoluteDimFunction.listenGroup, FPSTR(_invalidgroup));
      configComplete &= getJsonValue(absDimGroups[FPSTR(_state)], absoluteDimFunction.stateGroup, FPSTR(_invalidgroup));

      JsonObject relDimGroups = top[F("Relative Dim Groups")];
      configComplete = !relDimGroups.isNull();
      configComplete &= getJsonValue(relDimGroups[FPSTR(_enabled)], relativeDimFunction.enabled, false);
      configComplete &= getJsonValue(relDimGroups[FPSTR(_group)], relativeDimFunction.listenGroup, FPSTR(_invalidgroup));
      configComplete &= getJsonValue(relDimGroups[FPSTR(_time)], relativeDimTime, 5000);

      JsonObject colGroups = top[F("Color Groups")];
      configComplete = !colGroups.isNull();      
      configComplete &= getJsonValue(colGroups[FPSTR(_enabled)], colorFunction.enabled, false);
      configComplete &= getJsonValue(colGroups[FPSTR(_group)], colorFunction.listenGroup, FPSTR(_invalidgroup));
      configComplete &= getJsonValue(colGroups[FPSTR(_state)], colorFunction.stateGroup, FPSTR(_invalidgroup));

      JsonObject presetsGroups = top[F("Presets Groups")];
      configComplete = !presetsGroups.isNull();
      configComplete &= getJsonValue(presetsGroups[FPSTR(_enabled)], presetsFunction.enabled, false);
      configComplete &= getJsonValue(presetsGroups[FPSTR(_group)], presetsFunction.listenGroup, FPSTR(_invalidgroup));

      JsonObject effectGroups = top[F("Effect Groups")];
      configComplete = !effectGroups.isNull();
      configComplete &= getJsonValue(effectGroups[FPSTR(_enabled)], effectFunction.enabled, false);
      configComplete &= getJsonValue(effectGroups[FPSTR(_group)], effectFunction.listenGroup, FPSTR(_invalidgroup));

      JsonObject paletteGroups = top[F("Palette Groups")];
      configComplete = !paletteGroups.isNull();
      configComplete &= getJsonValue(paletteGroups[FPSTR(_enabled)], paletteFunction.enabled, false);
      configComplete &= getJsonValue(paletteGroups[FPSTR(_group)], paletteFunction.listenGroup, FPSTR(_invalidgroup));

      return configComplete; 
    }

    void addToJsonInfo(JsonObject& root) override
    {
      // if "u" object does not exist yet we need to create it
      JsonObject user = root["u"];
      if (user.isNull()) user = root.createNestedObject("u");

      user.createNestedArray(FPSTR(_name));

      JsonArray addressArr = user.createNestedArray(F("Individual address:"));
      addressArr.add(individualAddress);
    }  

    uint16_t getId() override
    {
      return USERMOD_ID_KNX;
    }
};

//-----------------------------------------------------------------
// implementation of non-inline member methods
//-----------------------------------------------------------------

void KnxUsermod::populateKnxFunctions() {
  if (switchFunction.enabled) { knxFunctions.push_back(switchFunction); }
  if (absoluteDimFunction.enabled) { knxFunctions.push_back(absoluteDimFunction); }
  if (relativeDimFunction.enabled) { knxFunctions.push_back(relativeDimFunction); }
  if (colorFunction.enabled) { knxFunctions.push_back(colorFunction); }
  if (presetsFunction.enabled) { knxFunctions.push_back(presetsFunction); }
  if (effectFunction.enabled) { knxFunctions.push_back(effectFunction); }
  if (paletteFunction.enabled) { knxFunctions.push_back(paletteFunction); }
  //if (playlistFunction.enabled) { knxFunctions.push_back(playlistFunction); }
}

void KnxUsermod::allocatePins() {
  PinManagerPinType pins[2] = { { txPin, true }, { rxPin, false } };
  if (!PinManager::allocateMultiplePins(pins, 2, PinOwner::UM_KNX)) {
    txPin = -1;
    rxPin = -1;
    return;
  }
}

void KnxUsermod::initBus() {
  if (individualAddress) {
    allocatePins();
    knxPtr = std::unique_ptr<KnxTpUart>(new KnxTpUart(&Serial1, individualAddress));
    if (knxPtr) {
      // @FIX group should both exist and be different from invalidgroup
      if (switchFunction.listenGroup != "0/0/0") {
        knxPtr->addListenGroupAddress(switchFunction.listenGroup);
      }
      if (absoluteDimFunction.listenGroup != "0/0/0") {
        knxPtr->addListenGroupAddress(absoluteDimFunction.listenGroup);            
      }
      if (relativeDimFunction.listenGroup != "0/0/0") {
        knxPtr->addListenGroupAddress(relativeDimFunction.listenGroup);
      }
      if (colorFunction.listenGroup != "0/0/0") {
        knxPtr->addListenGroupAddress(colorFunction.listenGroup);
      }
      if (presetsFunction.listenGroup != "0/0/0") {
        knxPtr->addListenGroupAddress(presetsFunction.listenGroup);
      }
      if (effectFunction.listenGroup != "0/0/0") {
        knxPtr->addListenGroupAddress(effectFunction.listenGroup);
      }
      if (paletteFunction.listenGroup != "0/0/0") {
        knxPtr->addListenGroupAddress(paletteFunction.listenGroup);
      }
    }
    Serial1.begin(19200, SERIAL_8E1, rxPin, txPin);
  }
}

void KnxUsermod::updateWLED() {
  colorUpdated(CALL_MODE_NO_NOTIFY);
}

void KnxUsermod::updateFromBus() {
  KnxTpUartSerialEventType eType = knxPtr->serialEvent();
  if (eType != KNX_TELEGRAM) return;

  KnxTelegram* telegram = knxPtr->getReceivedTelegram();

  // Switch group
  if (isGroupTarget(*telegram, switchFunction.listenGroup)) {
    bool on = telegram->getBool();
    if (on && bri == 0) {
      bri = briLast;
      DEBUG_PRINT(F("On: "));  DEBUG_PRINTLN(bri);
      knxPtr->groupWriteBool(switchFunction.stateGroup, true);
      updateWLED();
    } else if (!on && bri != 0) {
      bri = 0;
      DEBUG_PRINT(F("Off: "));  DEBUG_PRINTLN(bri);
      knxPtr->groupWriteBool(switchFunction.stateGroup, false);
      updateWLED();
    }
  }

  // Absolute Dim Group
  if (isGroupTarget(*telegram, absoluteDimFunction.listenGroup)) {
    uint8_t brightness = telegram->get1ByteIntValue();
    bri = brightness ? brightness : 0;
    DEBUG_PRINT(F("Brightness: "));  DEBUG_PRINTLN(bri);
    if (bri == 0) {
      knxPtr->groupWriteBool(switchFunction.stateGroup, false);
    } else {
      knxPtr->groupWriteBool(switchFunction.stateGroup, true);
      knxPtr->groupWrite1ByteInt(absoluteDimFunction.stateGroup, bri);
    }
    updateWLED();
  }

  // Relative Dim Group
  if (isGroupTarget(*telegram, relativeDimFunction.listenGroup)) {
    int direction = telegram->get4BitDirectionValue();
    int step = telegram->get4BitStepsValue();
    DEBUG_PRINT(F("Direction: "));  DEBUG_PRINTLN(direction);
    DEBUG_PRINT(F("Step: "));  DEBUG_PRINTLN(step);
    if (step == 0) { // stop
      currentTask = DimTask::DIM_STOP;
    } else if (direction == 1) { // up
      currentTask = DimTask::DIM_UP;
    } else if (direction == 0) { // down
      currentTask = DimTask::DIM_DOWN;
    }
  }

  // Color Select Group
  if (isGroupTarget(*telegram, colorFunction.listenGroup)) {
    int rgbw = telegram->get6ByteIntValue();
    DEBUG_PRINTLN(F("Color: "));
    colPri[0] = (rgbw >> 24) & 0xFF;
    colPri[1] = (rgbw >> 16) & 0xFF;
    colPri[2] = (rgbw >> 8) & 0xFF;
    colPri[3] = rgbw & 0xFF;
    DEBUG_PRINTLN(colPri[0]); DEBUG_PRINTLN(colPri[1]); DEBUG_PRINTLN(colPri[2]); DEBUG_PRINTLN(colPri[3]);
    int color = (colPri[0] << 24) | (colPri[1] << 16) | (colPri[2] << 8) | colPri[3];
    knxPtr->groupWrite6ByteInt(colorFunction.stateGroup, color);
    updateWLED();
  }

  // Presets Select Group
  if (isGroupTarget(*telegram, presetsFunction.listenGroup)) {
    int presets = telegram->get1ByteIntValue() + 1;
    DEBUG_PRINT(F("Presets: "));  DEBUG_PRINTLN(presets);
    applyPreset(presets, CALL_MODE_BUTTON_PRESET);
  }

  // Effect Select Group
  if (isGroupTarget(*telegram, effectFunction.listenGroup)) {
    int effect = telegram->get1ByteIntValue() + 1;
    DEBUG_PRINT(F("Effect: "));  DEBUG_PRINTLN(effect);
    // set effect for all segments
    for (uint8_t i = 0; i < strip.getMaxSegments(); i++) {
      strip.setMode(i, effect);
    }
    strip.trigger();
  }

  // Palette Select Group
  if (isGroupTarget(*telegram, paletteFunction.listenGroup)) {
    int palette = telegram->get1ByteIntValue() + 1;
    DEBUG_PRINT(F("Palette: "));  DEBUG_PRINTLN(palette);
    // set color palette for all segments
    for (uint8_t i = 0; i < strip.getMaxSegments(); i++) {
      strip.getSegment(i).palette = palette;
    }
    strip.trigger();
  }
}

void KnxUsermod::dimLight() {
  if (currentTask == DimTask::DIM_IDLE) return;
  currentMillis = millis();

  if (currentTask == DimTask::DIM_STOP) {
    isDimming = false;
    currentTask = DimTask::DIM_IDLE;
    updateWLED();
    knxPtr->groupWrite1ByteInt(absoluteDimFunction.stateGroup, bri);
    return;
  }

  uint8_t target = (currentTask == DimTask::DIM_UP) ? 255 : 1;
  if (bri == target) {
    currentTask = DimTask::DIM_STOP;
    return;
  }

  if (!isDimming) {
    uint16_t steps = abs((int)target - (int)bri);
    time = steps ? (relativeDimTime / steps) : relativeDimTime;
  }

  if (currentMillis - lastTaskExecution >= time) {
    bri += (currentTask == DimTask::DIM_UP) ? 1 : -1;
    bri = constrain(bri, 1, 255);
    isDimming = true;
    updateWLED();
    lastTaskExecution = currentMillis;
    if (bri == target) currentTask = DimTask::DIM_STOP;
  }
}

int KnxUsermod::countDelimiter(const String& address, const char delimiter) {
  int delimits = 0;

  for (int i=0; i < address.length(); i++) {
    if (address.c_str()[i] == delimiter) {
      delimits++;
    }
  }
  return delimits;
}

bool KnxUsermod::isGroupTarget(KnxTelegram telegram, const String& target) {
  String sourceGroup;
  int main, middle, sub;

  main = telegram.getTargetMainGroup();
  middle = telegram.getTargetMiddleGroup();
  sub = telegram.getTargetSubGroup();

  // @fix: only accounts for three level style groups
  sourceGroup = String(String(main) + '/' + String(middle) + '/' + String(sub));
  return (target == sourceGroup) ? true : false;
}

bool KnxUsermod::validateAddress(const String& address) {
  bool validAddress = false;
  int dots, members, area, line, device;
  
  dots = countDelimiter(address, '.');

  if (dots == 2) {
    members = std::sscanf(address.c_str(), "%i.%i.%i", &area, &line, &device);

    if (members == 3) {
      // Devices should never have 0 as their "device" member
      if ((0 <= area) && (area <= 15) && (0 <= line) && (line <= 15) && (1 <= device) && (device <= 255)) {
        validAddress = true;
      }
    }
  }
  return validAddress;
}

bool KnxUsermod::validateGroup(const String& address) {
  bool validAddress = false;
  int slashes, members, first, second, third;
  
  slashes = countDelimiter(address, '/');
  
  if (slashes < 3) {
    members = std::sscanf(address.c_str(), "%i/%i/%i", &first, &second, &third);

    // 3-level structure
    if (members == 3 && ((first + second + third) != 0)) {
      if ((0 <= first) && (first <= 31) && (0 <= second) && (second <= 7) && (0 <= third) && (third <= 255)) {
        validAddress = true;
      }
    }
    // 2-level structure
    else if (members == 2 && ((first + second) != 0)) {
      if ((0 <= first) && (first <= 31) && (0 <= second) && (second <= 2047)) {
        validAddress = true;
      }
    }
    // free structure
    else if (members == 1 && (first != 0)) {
      if (first <= 65535) {
        validAddress = true;
      }
    }  
  }
  return validAddress; 
}

// add more strings here to reduce flash memory usage
const char KnxUsermod::_name[]            PROGMEM = "KNX";
const char KnxUsermod::_enabled[]         PROGMEM = "enabled";
const char KnxUsermod::_address[]         PROGMEM = "address:";
const char KnxUsermod::_group[]           PROGMEM = "group:";
const char KnxUsermod::_state[]           PROGMEM = "state:";
const char KnxUsermod::_time[]            PROGMEM = "time:";
const char KnxUsermod::_invalidaddress[]  PROGMEM = "0.0.0";
const char KnxUsermod::_invalidgroup[]    PROGMEM = "0/0/0";
const char KnxUsermod::_txPin[]           PROGMEM = "TX-pin:";
const char KnxUsermod::_rxPin[]           PROGMEM = "RX-pin:";