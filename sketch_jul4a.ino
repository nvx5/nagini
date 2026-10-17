#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>
#include <Preferences.h>
#include <string.h>

// ============================================================
// PINS
// ============================================================
#define TFT_CS    10
#define TFT_DC    8
#define TFT_RST   9
#define TFT_MOSI  6
#define TFT_SCLK  7

#define ENC_S1    0
#define ENC_S2    1
#define ENC_KEY   2

#define DS18B20_PIN 3
#define AHT20_SDA   20
#define AHT20_SCL   21

// Swapped as requested:
#define LIGHT_PIN 4
#define FAN_PIN   5

// Change to false if your MOSFET/relay input is active LOW.
const bool LIGHT_ACTIVE_HIGH = true;
const bool FAN_ACTIVE_HIGH = true;

// ============================================================
// PWM LIGHTING
// ============================================================
const uint32_t LIGHT_PWM_FREQUENCY = 5000;
const uint8_t LIGHT_PWM_RESOLUTION = 8;
const uint16_t LIGHT_PWM_MAX = 255;

// ============================================================
// OBJECTS
// ============================================================
Adafruit_ST7789 tft = Adafruit_ST7789(
  TFT_CS,
  TFT_DC,
  TFT_RST
);

Adafruit_AHTX0 aht;
Preferences preferences;

OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

// ============================================================
// SCREEN / COLOURS
// ============================================================
#define W 320
#define H 170

#define BG      ST77XX_BLACK
#define TEXT    ST77XX_WHITE
#define MUTED   0x8410
#define CARD    ST77XX_BLACK
#define BORDER  0x4208
#define SELECT  ST77XX_WHITE
#define SELTXT  ST77XX_BLACK

#define GREEN   0x1585
#define YELLOW  0xFEA0
#define RED     0xD8A7

// ============================================================
// SCREEN STATE
// ============================================================
enum ScreenMode {
  IDLE,
  MAIN,
  PARAM_MENU,
  FEED_MENU
};

ScreenMode screen = IDLE;

unsigned long lastInput = 0;
unsigned long lastSensorRead = 0;

const unsigned long TIMEOUT_MS = 10000;
const unsigned long SENSOR_INTERVAL = 2000;

// ============================================================
// SENSOR VALUES
// ============================================================
float airTemp = NAN;
float humidity = NAN;
float warmTemp = NAN;
float coolTemp = NAN;

int dsCount = 0;

// Change this if your warm and cool probes are reversed.
bool swapProbes = false;

// ============================================================
// SENSOR CONNECTION STATE
// ============================================================
bool ahtOnline = false;
bool warmProbeOnline = false;
bool coolProbeOnline = false;

// ============================================================
// PARAMETER MODEL
// ============================================================
// optLow / optHigh:
// Green optimal range.
//
// tolLow:
// How far below optLow remains yellow.
//
// tolHigh:
// How far above optHigh remains yellow.
//
// Anything beyond the yellow range is red.

struct ParamSetting {
  const char* name;
  bool isHumidity;

  float optLow;
  float optHigh;

  float tolLow;
  float tolHigh;
};

ParamSetting params[4] = {
  {
    "WARM SIDE",
    false,
    28,
    30,
    2,
    2
  },
  {
    "COOL SIDE",
    false,
    20,
    24,
    2,
    2
  },
  {
    "AIR TEMP",
    false,
    23,
    26,
    2,
    2
  },
  {
    "HUMIDITY",
    true,
    40,
    50,
    5,
    10
  }
};

const ParamSetting defaultParams[4] = {
  {
    "WARM SIDE",
    false,
    28,
    30,
    2,
    2
  },
  {
    "COOL SIDE",
    false,
    20,
    24,
    2,
    2
  },
  {
    "AIR TEMP",
    false,
    23,
    26,
    2,
    2
  },
  {
    "HUMIDITY",
    true,
    40,
    50,
    5,
    10
  }
};

// ============================================================
// PARAMETER STATUS / HYSTERESIS
// ============================================================
enum ParamStatus {
  STATUS_UNKNOWN,
  STATUS_OPTIMAL,
  STATUS_SUBOPTIMAL,
  STATUS_CRITICAL
};

ParamStatus warmStatus = STATUS_UNKNOWN;
ParamStatus coolStatus = STATUS_UNKNOWN;
ParamStatus airStatus = STATUS_UNKNOWN;
ParamStatus humStatus = STATUS_UNKNOWN;

const float TEMP_HYST = 0.2;
const float HUM_HYST = 1.0;

bool editingParam = false;
int activeParamField = -1;

// ============================================================
// LIGHT / FAN MODES, CLOCK AND PERSISTENCE
// ============================================================
enum DeviceMode { MODE_AUTO, MODE_ON, MODE_OFF };
DeviceMode lightMode = MODE_ON;
DeviceMode fanMode = MODE_OFF;
int lightBrightness = 100;
int fanSpeed = 100;
bool editingBrightness = false;
bool editingFanSpeed = false;
float currentLightPercent = 0.0f;
float targetLightPercent = 0.0f;
float currentFanPercent = 0.0f;
float targetFanPercent = 0.0f;
unsigned long lastFadeUpdate = 0;
const unsigned long FADE_UPDATE_MS = 20;
const float FADE_PERCENT_PER_SECOND = 50.0f;
bool lightOutputOn = false;
bool fanOutputOn = false;
bool fanAutoRunning = false;
const float FAN_TEMP_OFF_HYST = 0.5;
const float FAN_HUM_OFF_HYST = 2.0;
struct LocalDateTime { uint16_t year; uint8_t month, day, hour, minute, second; };
LocalDateTime clockTime = {2026, 7, 24, 12, 0, 0};
unsigned long lastClockMillis = 0;
bool editingDateTime = false;
int dateTimeField = 0;
const float UK_LATITUDE_DEG = 52.0f;
const int SUN_TRANSITION_MINUTES = 60;
const uint32_t SETTINGS_VERSION = 5;
bool preferencesReady = false;

const int DEFAULT_FEED_WARNING_DAYS = 16;
int feedWarningDays = DEFAULT_FEED_WARNING_DAYS;

int lastFeedDays = -1;
int lastShedDays = -1;
int feedSelected = 0;
bool editingLastFeed = false;
bool editingFeedWarning = false;

struct DashboardCache {
  bool initialized;
  bool online;
  char value1[12];
  char value2[12];
  int marker1;
  int marker2;
  bool warning;
};

// ============================================================
// EXPLICIT PROTOTYPES FOR ARDUINO PREPROCESSOR
// These must appear after all custom types above. Without them,
// Arduino may generate prototypes before the type declarations.
// ============================================================
bool validDS18B20Temperature(float value);
int dayOfYear(const LocalDateTime& dt);
const char* modeText(DeviceMode mode);
void cycleMode(DeviceMode& mode);
ParamStatus classifyValue(
  float value,
  ParamSetting setting,
  ParamStatus previous,
  float hysteresis
);
int rangeMarkerX(
  int x,
  int width,
  float value,
  const ParamSetting& setting,
  bool online
);
void drawRangeBar(
  int x,
  int y,
  int width,
  float value,
  const ParamSetting& setting,
  bool online
);
void drawWarningSymbol(int x, int y);
void updateDS18B20Card(
  int x,
  const char* deviceName,
  const char* role,
  float value,
  const ParamSetting& setting,
  ParamStatus status,
  bool online,
  DashboardCache& cache
);



// DS18B20 error values commonly returned when a probe is missing
// or has not completed a valid conversion.
bool validDS18B20Temperature(float value) {
  return (
    !isnan(value) &&
    value > -100.0 &&
    value < 125.0 &&
    fabs(value - 85.0) > 0.05
  );
}

// ============================================================
// MENU DATA
// ============================================================
const char* mainMenu[] = {
  "LIGHTING", "BRIGHTNESS", "FAN", "FAN SPEED", "DATETIME",
  "SETTINGS", "FEEDING", "SHEDDING"
};

const int mainCount =
  sizeof(mainMenu) / sizeof(mainMenu[0]);

const char* paramMenu[] = {
  "WARM SIDE",
  "COOL SIDE",
  "AIR TEMP",
  "HUMIDITY",
  "FEED WARNING",
  "RESET DEFAULTS",
  "BACK"
};

const int paramCount =
  sizeof(paramMenu) / sizeof(paramMenu[0]);

const char* feedMenu[] = {
  "RECORD MEAL",
  "LAST FEED",
  "BACK"
};

const int feedCount =
  sizeof(feedMenu) / sizeof(feedMenu[0]);

int selected = 0;
int paramSelected = 0;
int topIndex = 0;

const int visibleRows = 6;

const int rowX = 12;
const int rowW = 296;
const int rowH = 23;
const int rowGap = 3;
const int startY = 12;

// ============================================================
// ENCODER
// ============================================================
int lastS1 = HIGH;
bool lastButton = HIGH;

unsigned long lastMoveTime = 0;
unsigned long lastButtonTime = 0;

// ============================================================
// CLOCK, SUNRISE/SUNSET, PERSISTENCE AND OUTPUT CONTROL
// ============================================================
bool isLeapYear(uint16_t y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
uint8_t daysInMonth(uint16_t y, uint8_t m) {
  static const uint8_t d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  return (m == 2 && isLeapYear(y)) ? 29 : d[constrain((int)m,1,12)-1];
}
int dayOfYear(const LocalDateTime& dt) {
  int n = dt.day;
  for (int m=1; m<dt.month; ++m) n += daysInMonth(dt.year,m);
  return n;
}
void incrementOneSecond() {
  if (++clockTime.second < 60) return; clockTime.second=0;
  if (++clockTime.minute < 60) return; clockTime.minute=0;
  if (++clockTime.hour < 24) return; clockTime.hour=0;
  if (lastFeedDays >= 0 && lastFeedDays < 9999) lastFeedDays++;
  if (lastShedDays >= 0 && lastShedDays < 9999) lastShedDays++;
  saveSettings();
  if (++clockTime.day <= daysInMonth(clockTime.year,clockTime.month)) return; clockTime.day=1;
  if (++clockTime.month <= 12) return; clockTime.month=1; clockTime.year++;
}
void updateClock() {
  unsigned long now=millis();
  while (now-lastClockMillis >= 1000UL) { lastClockMillis += 1000UL; incrementOneSecond(); }
}
void calculateSunTimes(float& sunriseMinutes, float& sunsetMinutes) {
  const float r=PI/180.0f;
  float declination=23.44f*sinf((360.0f/365.0f)*(dayOfYear(clockTime)-81)*r);
  float cosH=-tanf(UK_LATITUDE_DEG*r)*tanf(declination*r);
  cosH=constrain(cosH,-1.0f,1.0f);
  float daylightHours=2.0f*(acosf(cosH)/r)/15.0f;
  sunriseMinutes=(12.0f-daylightHours/2.0f)*60.0f;
  sunsetMinutes=(12.0f+daylightHours/2.0f)*60.0f;
}
float automaticLightPercent() {
  float rise,set; calculateSunTimes(rise,set);
  float now=clockTime.hour*60.0f+clockTime.minute+clockTime.second/60.0f;
  if (now<rise || now>=set) return 0.0f;
  if (now<rise+SUN_TRANSITION_MINUTES)
    return lightBrightness*(now-rise)/SUN_TRANSITION_MINUTES;
  if (now>=set-SUN_TRANSITION_MINUTES)
    return lightBrightness*(set-now)/SUN_TRANSITION_MINUTES;
  return lightBrightness;
}
void writePwmPercent(int pin,float pct,bool activeHigh) {
  pct=constrain(pct,0.0f,100.0f);
  uint32_t duty=lroundf(pct*LIGHT_PWM_MAX/100.0f);
  if (!activeHigh) duty=LIGHT_PWM_MAX-duty;
  ledcWrite(pin,duty);
}
const char* modeText(DeviceMode mode) {
  if (mode==MODE_AUTO) return "AUTO";
  if (mode==MODE_ON) return "ON";
  return "OFF";
}
void cycleMode(DeviceMode& mode) {
  if (mode==MODE_AUTO) mode=MODE_ON;
  else if (mode==MODE_ON) mode=MODE_OFF;
  else mode=MODE_AUTO;
}
void brightnessText(char* b) { snprintf(b,8,"%d%%",lightBrightness); }
void fanSpeedText(char* b) { snprintf(b,8,"%d%%",fanSpeed); }
void saveSettings() {
  if (!preferencesReady) return;
  preferences.putUInt("ver",SETTINGS_VERSION);
  preferences.putUChar("lmode",(uint8_t)lightMode);
  preferences.putUChar("fmode",(uint8_t)fanMode);
  preferences.putUChar("lpct",(uint8_t)lightBrightness);
  preferences.putUChar("fpct",(uint8_t)fanSpeed);
  preferences.putUShort("year",clockTime.year);
  preferences.putUChar("month",clockTime.month);
  preferences.putUChar("day",clockTime.day);
  preferences.putUChar("hour",clockTime.hour);
  preferences.putUChar("minute",clockTime.minute);
  preferences.putBool("swap",swapProbes);
  preferences.putInt("feedDays", lastFeedDays);
  preferences.putInt("shedDays", lastShedDays);
  preferences.putInt("feedWarn", feedWarningDays);
  for (int i=0;i<4;i++) {
    char k[12];
    snprintf(k,sizeof(k),"p%dl",i); preferences.putFloat(k,params[i].optLow);
    snprintf(k,sizeof(k),"p%dh",i); preferences.putFloat(k,params[i].optHigh);
    snprintf(k,sizeof(k),"p%dtl",i); preferences.putFloat(k,params[i].tolLow);
    snprintf(k,sizeof(k),"p%dth",i); preferences.putFloat(k,params[i].tolHigh);
  }
}
void loadSettings() {
  preferencesReady=preferences.begin("snakeTank",false);
  if (!preferencesReady) return;
  bool settingsNeedUpgrade = preferences.getUInt("ver",0) != SETTINGS_VERSION;
  lightMode=(DeviceMode)constrain((int)preferences.getUChar("lmode",MODE_ON),0,2);
  fanMode=(DeviceMode)constrain((int)preferences.getUChar("fmode",MODE_OFF),0,2);
  lightBrightness=constrain((int)preferences.getUChar("lpct",100),0,100);
  fanSpeed=constrain((int)preferences.getUChar("fpct",100),0,100);
  clockTime.year=constrain((int)preferences.getUShort("year",2026),2024,2099);
  clockTime.month=constrain((int)preferences.getUChar("month",7),1,12);
  clockTime.day=constrain((int)preferences.getUChar("day",24),1,(int)daysInMonth(clockTime.year,clockTime.month));
  clockTime.hour=constrain((int)preferences.getUChar("hour",12),0,23);
  clockTime.minute=constrain((int)preferences.getUChar("minute",0),0,59);
  clockTime.second=0;
  swapProbes=preferences.getBool("swap",false);
  lastFeedDays=preferences.getInt("feedDays",-1);
  lastShedDays=preferences.getInt("shedDays",-1);
  feedWarningDays=constrain(preferences.getInt("feedWarn",DEFAULT_FEED_WARNING_DAYS),1,999);
  for (int i=0;i<4;i++) {
    char k[12];
    snprintf(k,sizeof(k),"p%dl",i); params[i].optLow=preferences.getFloat(k,defaultParams[i].optLow);
    snprintf(k,sizeof(k),"p%dh",i); params[i].optHigh=preferences.getFloat(k,defaultParams[i].optHigh);
    snprintf(k,sizeof(k),"p%dtl",i); params[i].tolLow=preferences.getFloat(k,defaultParams[i].tolLow);
    snprintf(k,sizeof(k),"p%dth",i); params[i].tolHigh=preferences.getFloat(k,defaultParams[i].tolHigh);
  }
  if (settingsNeedUpgrade) saveSettings();
}
bool fanHighTriggerPresent() {
  return (!isnan(warmTemp)&&warmTemp>params[0].optHigh+params[0].tolHigh) ||
         (!isnan(airTemp)&&airTemp>params[2].optHigh+params[2].tolHigh) ||
         (!isnan(humidity)&&humidity>params[3].optHigh+params[3].tolHigh);
}
bool fanSafeToStop() {
  return (isnan(warmTemp)||warmTemp<params[0].optHigh+params[0].tolHigh-FAN_TEMP_OFF_HYST) &&
         (isnan(airTemp)||airTemp<params[2].optHigh+params[2].tolHigh-FAN_TEMP_OFF_HYST) &&
         (isnan(humidity)||humidity<params[3].optHigh+params[3].tolHigh-FAN_HUM_OFF_HYST);
}
float automaticFanPercent() {
  if (!fanAutoRunning && fanHighTriggerPresent()) fanAutoRunning=true;
  else if (fanAutoRunning && fanSafeToStop()) fanAutoRunning=false;
  if (!fanAutoRunning) return 0.0f;
  float demand=40.0f;
  if (!isnan(warmTemp)) demand=max(demand,40.0f+20.0f*(warmTemp-params[0].optHigh));
  if (!isnan(airTemp)) demand=max(demand,40.0f+20.0f*(airTemp-params[2].optHigh));
  if (!isnan(humidity)) demand=max(demand,40.0f+4.0f*(humidity-params[3].optHigh));
  return constrain(demand,40.0f,100.0f);
}
float moveTowards(float current,float target,float step) {
  if (current<target) return min(current+step,target);
  if (current>target) return max(current-step,target);
  return current;
}
void updateOutputs() {
  updateClock();
  targetLightPercent=(lightMode==MODE_OFF)?0.0f:(lightMode==MODE_ON?lightBrightness:automaticLightPercent());
  targetFanPercent=(fanMode==MODE_OFF)?0.0f:(fanMode==MODE_ON?fanSpeed:automaticFanPercent());
  unsigned long now=millis();
  if (now-lastFadeUpdate<FADE_UPDATE_MS) return;
  float step=FADE_PERCENT_PER_SECOND*((now-lastFadeUpdate)/1000.0f);
  lastFadeUpdate=now;
  currentLightPercent=moveTowards(currentLightPercent,targetLightPercent,step);
  currentFanPercent=moveTowards(currentFanPercent,targetFanPercent,step);
  writePwmPercent(LIGHT_PIN,currentLightPercent,LIGHT_ACTIVE_HIGH);
  writePwmPercent(FAN_PIN,currentFanPercent,FAN_ACTIVE_HIGH);
  lightOutputOn=currentLightPercent>0.05f;
  fanOutputOn=currentFanPercent>0.05f;
}

// ============================================================
// BASIC HELPERS
// ============================================================
void printDSAddress(DeviceAddress address) {
  for (uint8_t i = 0; i < 8; i++) {
    if (address[i] < 16) {
      Serial.print("0");
    }

    Serial.print(address[i], HEX);
  }
}

void trimLeadingSpaces(char* buffer) {
  char* start = buffer;

  while (*start == ' ') {
    start++;
  }

  if (start != buffer) {
    memmove(
      buffer,
      start,
      strlen(start) + 1
    );
  }
}

void formatTemp(
  char* buffer,
  float value
) {
  if (
    isnan(value) ||
    value < -100
  ) {
    strcpy(buffer, "N/A");
    return;
  }

  dtostrf(value, 4, 1, buffer);
  strcat(buffer, "C");
}

void formatHumidity(
  char* buffer,
  float value
) {
  if (isnan(value)) {
    strcpy(buffer, "N/A");
    return;
  }

  dtostrf(value, 2, 0, buffer);
  strcat(buffer, "%");
}

void formatCompactNumber(
  char* buffer,
  float value,
  bool isHumidity
) {
  if (isHumidity) {
    dtostrf(value, 2, 0, buffer);
  } else {
    if (
      fabs(value - round(value)) <
      0.05
    ) {
      dtostrf(value, 2, 0, buffer);
    } else {
      dtostrf(value, 3, 1, buffer);
    }
  }

  trimLeadingSpaces(buffer);
}

void resetParams() {
  for (int i = 0; i < 4; i++) {
    params[i] = defaultParams[i];
  }
  feedWarningDays = DEFAULT_FEED_WARNING_DAYS;
}

// ============================================================
// PARAMETER STATUS
// ============================================================
ParamStatus classifyValue(
  float value,
  ParamSetting setting,
  ParamStatus previous,
  float hysteresis
) {
  if (
    isnan(value) ||
    value < -100
  ) {
    return STATUS_UNKNOWN;
  }

  float yellowLow =
    setting.optLow -
    setting.tolLow;

  float yellowHigh =
    setting.optHigh +
    setting.tolHigh;

  // Previously green
  if (previous == STATUS_OPTIMAL) {
    if (
      value >= setting.optLow - hysteresis &&
      value <= setting.optHigh + hysteresis
    ) {
      return STATUS_OPTIMAL;
    }

    if (
      value >= yellowLow &&
      value <= yellowHigh
    ) {
      return STATUS_SUBOPTIMAL;
    }

    return STATUS_CRITICAL;
  }

  // Previously yellow
  if (previous == STATUS_SUBOPTIMAL) {
    if (
      value >= setting.optLow + hysteresis &&
      value <= setting.optHigh - hysteresis
    ) {
      return STATUS_OPTIMAL;
    }

    if (
      value < yellowLow - hysteresis ||
      value > yellowHigh + hysteresis
    ) {
      return STATUS_CRITICAL;
    }

    return STATUS_SUBOPTIMAL;
  }

  // Previously red
  if (previous == STATUS_CRITICAL) {
    if (
      value >= setting.optLow + hysteresis &&
      value <= setting.optHigh - hysteresis
    ) {
      return STATUS_OPTIMAL;
    }

    if (
      value >= yellowLow + hysteresis &&
      value <= yellowHigh - hysteresis
    ) {
      return STATUS_SUBOPTIMAL;
    }

    return STATUS_CRITICAL;
  }

  // Initial classification
  if (
    value >= setting.optLow &&
    value <= setting.optHigh
  ) {
    return STATUS_OPTIMAL;
  }

  if (
    value >= yellowLow &&
    value <= yellowHigh
  ) {
    return STATUS_SUBOPTIMAL;
  }

  return STATUS_CRITICAL;
}

void updateStatuses() {
  warmStatus = classifyValue(
    warmTemp,
    params[0],
    warmStatus,
    TEMP_HYST
  );

  coolStatus = classifyValue(
    coolTemp,
    params[1],
    coolStatus,
    TEMP_HYST
  );

  airStatus = classifyValue(
    airTemp,
    params[2],
    airStatus,
    TEMP_HYST
  );

  humStatus = classifyValue(
    humidity,
    params[3],
    humStatus,
    HUM_HYST
  );
}

// ============================================================
// SENSOR READING
// ============================================================
void readSensors() {
  sensors_event_t humidityEvent;
  sensors_event_t temperatureEvent;

  ahtOnline = aht.getEvent(
    &humidityEvent,
    &temperatureEvent
  );

  if (ahtOnline) {
    airTemp = temperatureEvent.temperature;
    humidity = humidityEvent.relative_humidity;

    if (
      isnan(airTemp) ||
      isnan(humidity)
    ) {
      ahtOnline = false;
    }
  }

  if (!ahtOnline) {
    airTemp = NAN;
    humidity = NAN;
  }

  ds18b20.requestTemperatures();

  float probe0 = NAN;
  float probe1 = NAN;

  if (dsCount >= 1) {
    probe0 = ds18b20.getTempCByIndex(0);
  }

  if (dsCount >= 2) {
    probe1 = ds18b20.getTempCByIndex(1);
  }

  if (swapProbes) {
    warmTemp = probe0;
    coolTemp = probe1;
  } else {
    coolTemp = probe0;
    warmTemp = probe1;
  }

  warmProbeOnline = validDS18B20Temperature(warmTemp);
  coolProbeOnline = validDS18B20Temperature(coolTemp);

  if (!warmProbeOnline) {
    warmTemp = NAN;
  }

  if (!coolProbeOnline) {
    coolTemp = NAN;
  }

  updateStatuses();
}

// ============================================================
// TEXT HELPERS
// ============================================================
void boldCentre(
  const char* text,
  int y,
  int size
) {
  int16_t x1;
  int16_t y1;
  uint16_t textW;
  uint16_t textH;

  tft.setTextSize(size);
  tft.setTextColor(TEXT);

  tft.getTextBounds(
    text,
    0,
    0,
    &x1,
    &y1,
    &textW,
    &textH
  );

  int x = (W - textW) / 2;

  tft.setCursor(x, y);
  tft.print(text);

  tft.setCursor(x + 1, y);
  tft.print(text);
}

void centreText(
  const char* text,
  int y,
  int size,
  uint16_t colour
) {
  int16_t x1;
  int16_t y1;
  uint16_t textW;
  uint16_t textH;

  tft.setTextSize(size);
  tft.setTextColor(colour);

  tft.getTextBounds(
    text,
    0,
    0,
    &x1,
    &y1,
    &textW,
    &textH
  );

  tft.setCursor(
    (W - textW) / 2,
    y
  );

  tft.print(text);
}

// ============================================================
// DASHBOARD
// ============================================================
const int DASH_MARGIN = 6;
const int DASH_GAP = 6;

const int AHT_X = DASH_MARGIN;
const int AHT_Y = DASH_MARGIN;
const int AHT_W = 205;
const int AHT_H = 76;
const int INFO_X = AHT_X + AHT_W + DASH_GAP;
const int INFO_Y = AHT_Y;
const int INFO_W = W - INFO_X - DASH_MARGIN;
const int INFO_H = AHT_H;

const int DS_Y = AHT_Y + AHT_H + DASH_GAP;
const int DS_W = (W - (DASH_MARGIN * 2) - DASH_GAP) / 2;
const int DS_H = H - DS_Y - DASH_MARGIN;
const int COOL_X = DASH_MARGIN;
const int WARM_X = COOL_X + DS_W + DASH_GAP;

const uint16_t OFFLINE = 0x4208;
const uint16_t ONLINE_TEXT = 0xBDF7;
const int RANGE_BAR_HEIGHT = 3;
const int RANGE_MARKER_RADIUS = 3;

DashboardCache ahtCache = {false,false,"","",-1000,-1000,false};
DashboardCache coolCache = {false,false,"","",-1000,-1000,false};
DashboardCache warmCache = {false,false,"","",-1000,-1000,false};
char cachedFeedText[12] = "";
char cachedShedText[12] = "";
int cachedFeedWarningState = -1;

int dashboardTextWidth(const char* text, int size = 1) {
  int16_t x1,y1; uint16_t width,height;
  tft.setTextSize(size);
  tft.getTextBounds(text,0,0,&x1,&y1,&width,&height);
  return width;
}

void drawDeviceCardShell(int x,int y,int width,int height) {
  tft.fillRoundRect(x,y,width,height,8,CARD);
  tft.drawRoundRect(x,y,width,height,8,BORDER);
}

void drawDeviceHeader(int x,int y,int width,const char* deviceName,bool online) {
  tft.fillRect(x+7,y+5,width-14,15,CARD);
  tft.setTextSize(1);
  tft.setTextColor(online ? TEXT : OFFLINE);
  tft.setCursor(x+9,y+8); tft.print(deviceName);
  const char* stateText = online ? "ONLINE" : "OFFLINE";
  int stateWidth = dashboardTextWidth(stateText);
  tft.setTextColor(online ? ONLINE_TEXT : OFFLINE);
  tft.setCursor(x+width-stateWidth-9,y+8); tft.print(stateText);
}

uint16_t blend565(uint16_t a,uint16_t b,uint8_t amount) {
  uint8_t inv=255-amount;
  uint8_t ar=(a>>11)&0x1F, ag=(a>>5)&0x3F, ab=a&0x1F;
  uint8_t br=(b>>11)&0x1F, bg=(b>>5)&0x3F, bb=b&0x1F;
  uint8_t r=(ar*inv+br*amount)/255;
  uint8_t g=(ag*inv+bg*amount)/255;
  uint8_t bl=(ab*inv+bb*amount)/255;
  return (r<<11)|(g<<5)|bl;
}

uint16_t rangeGradientColour(float position) {
  position=constrain(position,0.0f,1.0f);
  if (position<0.25f) return blend565(RED,YELLOW,(uint8_t)((position/0.25f)*255.0f));
  if (position<0.50f) return blend565(YELLOW,GREEN,(uint8_t)(((position-0.25f)/0.25f)*255.0f));
  if (position<0.75f) return blend565(GREEN,YELLOW,(uint8_t)(((position-0.50f)/0.25f)*255.0f));
  return blend565(YELLOW,RED,(uint8_t)(((position-0.75f)/0.25f)*255.0f));
}

int rangeMarkerX(int x,int width,float value,const ParamSetting& setting,bool online) {
  if (!online || isnan(value)) return -1000;
  float rangeLow=setting.optLow-setting.tolLow;
  float rangeHigh=setting.optHigh+setting.tolHigh;
  float span=max(1.0f,rangeHigh-rangeLow);
  float displayLow=rangeLow-span*0.35f;
  float displayHigh=rangeHigh+span*0.35f;
  float ratio=constrain((value-displayLow)/(displayHigh-displayLow),0.0f,1.0f);
  return x+(int)(ratio*(width-1));
}

void drawRangeBar(int x,int y,int width,float value,const ParamSetting& setting,bool online) {
  int clearX=x-RANGE_MARKER_RADIUS-1;
  int clearY=y-RANGE_MARKER_RADIUS-1;
  tft.fillRect(clearX,clearY,width+(RANGE_MARKER_RADIUS+1)*2,
               RANGE_BAR_HEIGHT+(RANGE_MARKER_RADIUS+1)*2,CARD);
  if (!online || isnan(value)) {
    tft.fillRoundRect(x,y,width,RANGE_BAR_HEIGHT,1,OFFLINE);
    return;
  }
  for (int px=0;px<width;px++) {
    float pos=(width<=1)?0.0f:(float)px/(float)(width-1);
    tft.drawFastVLine(x+px,y,RANGE_BAR_HEIGHT,rangeGradientColour(pos));
  }
  int markerX=rangeMarkerX(x,width,value,setting,online);
  int markerY=y+RANGE_BAR_HEIGHT/2;
  tft.fillCircle(markerX,markerY,RANGE_MARKER_RADIUS+1,BG);
  tft.fillCircle(markerX,markerY,RANGE_MARKER_RADIUS,TEXT);
}

void resetDashboardCaches() {
  ahtCache.initialized=coolCache.initialized=warmCache.initialized=false;
  ahtCache.value1[0]=ahtCache.value2[0]='\0';
  coolCache.value1[0]=warmCache.value1[0]='\0';
  ahtCache.marker1=ahtCache.marker2=coolCache.marker1=warmCache.marker1=-1000;
  ahtCache.warning=coolCache.warning=warmCache.warning=false;
  cachedFeedText[0]=cachedShedText[0]='\0';
  cachedFeedWarningState=-1;
}

void drawIdleLayout() {
  screen=IDLE;
  tft.fillScreen(BG);
  drawDeviceCardShell(AHT_X,AHT_Y,AHT_W,AHT_H);
  drawDeviceCardShell(INFO_X,INFO_Y,INFO_W,INFO_H);
  drawDeviceCardShell(COOL_X,DS_Y,DS_W,DS_H);
  drawDeviceCardShell(WARM_X,DS_Y,DS_W,DS_H);

  tft.setTextSize(1); tft.setTextColor(MUTED);
  tft.setCursor(AHT_X+10,AHT_Y+27); tft.print("TEMP");
  tft.setCursor(AHT_X+106,AHT_Y+27); tft.print("HUMIDITY");

  // No card title. Keep the sections close but clearly separated.
  tft.setTextSize(1);
  tft.setTextColor(MUTED);
  tft.setCursor(INFO_X+9,INFO_Y+8);  tft.print("LAST FEED");
  tft.setCursor(INFO_X+9,INFO_Y+40); tft.print("LAST SHED");

  resetDashboardCaches();
}

void drawWarningSymbol(int x, int y) {
  // Compact filled warning triangle with a black exclamation mark.
  tft.fillTriangle(x + 6, y, x, y + 11, x + 12, y + 11, RED);
  tft.drawFastVLine(x + 6, y + 3, 5, BG);
  tft.drawPixel(x + 6, y + 9, BG);
}

void updateAHT20Card() {
  char tempBuffer[12],humidityBuffer[12];
  formatTemp(tempBuffer,airTemp); formatHumidity(humidityBuffer,humidity);
  const int leftX=AHT_X+10, rightX=AHT_X+106;
  const int leftW=82, rightW=88, barY=AHT_Y+65;
  int marker1=rangeMarkerX(leftX,leftW,airTemp,params[2],ahtOnline);
  int marker2=rangeMarkerX(rightX,rightW,humidity,params[3],ahtOnline);
  bool onlineChanged=!ahtCache.initialized || ahtCache.online!=ahtOnline;
  if (onlineChanged) {
    drawDeviceHeader(AHT_X,AHT_Y,AHT_W,"AHT20",ahtOnline);
    tft.setTextSize(1); tft.setTextColor(ahtOnline?MUTED:OFFLINE);
    tft.fillRect(leftX,AHT_Y+25,70,10,CARD); tft.setCursor(leftX,AHT_Y+27); tft.print("TEMP");
    tft.fillRect(rightX,AHT_Y+25,80,10,CARD); tft.setCursor(rightX,AHT_Y+27); tft.print("HUMIDITY");
  }
  if (onlineChanged || strcmp(ahtCache.value1,tempBuffer)!=0) {
    tft.fillRect(leftX,AHT_Y+39,leftW,18,CARD);
    tft.setTextSize(2); tft.setTextColor(ahtOnline?TEXT:OFFLINE); tft.setCursor(leftX,AHT_Y+39); tft.print(tempBuffer);
    strcpy(ahtCache.value1,tempBuffer);
  }
  if (onlineChanged || strcmp(ahtCache.value2,humidityBuffer)!=0) {
    tft.fillRect(rightX,AHT_Y+39,rightW,18,CARD);
    tft.setTextSize(2); tft.setTextColor(ahtOnline?TEXT:OFFLINE); tft.setCursor(rightX,AHT_Y+39); tft.print(humidityBuffer);
    strcpy(ahtCache.value2,humidityBuffer);
  }
  if (onlineChanged || ahtCache.marker1!=marker1) {
    drawRangeBar(leftX,barY,leftW,airTemp,params[2],ahtOnline); ahtCache.marker1=marker1;
  }
  if (onlineChanged || ahtCache.marker2!=marker2) {
    drawRangeBar(rightX,barY,rightW,humidity,params[3],ahtOnline); ahtCache.marker2=marker2;
  }
  ahtCache.online=ahtOnline; ahtCache.initialized=true;
}

void updateDS18B20Card(
  int x,
  const char* deviceName,
  const char* role,
  float value,
  const ParamSetting& setting,
  ParamStatus status,
  bool online,
  DashboardCache& cache
) {
  char valueBuffer[12];
  formatTemp(valueBuffer,value);

  int barX=x+9;
  int barW=DS_W-18;
  int barY=DS_Y+DS_H-12;
  int marker=rangeMarkerX(barX,barW,value,setting,online);
  bool warning=online && status==STATUS_CRITICAL;

  bool onlineChanged=!cache.initialized || cache.online!=online;
  bool warningChanged=!cache.initialized || cache.warning!=warning;

  if (onlineChanged) {
    drawDeviceHeader(x,DS_Y,DS_W,deviceName,online);
    tft.fillRect(x+8,DS_Y+24,DS_W-16,11,CARD);
    tft.setTextSize(1);
    tft.setTextColor(online?MUTED:OFFLINE);
    tft.setCursor(x+9,DS_Y+27);
    tft.print(role);
  }

  if (onlineChanged || strcmp(cache.value1,valueBuffer)!=0) {
    // Leave the right side of the value row available for the warning icon.
    tft.fillRect(x+8,DS_Y+38,DS_W-38,19,CARD);
    tft.setTextSize(2);
    tft.setTextColor(online?TEXT:OFFLINE);
    tft.setCursor(x+9,DS_Y+40);
    tft.print(valueBuffer);
    strcpy(cache.value1,valueBuffer);
  }

  if (warningChanged) {
    int warningX=x+DS_W-23;
    int warningY=DS_Y+41;
    tft.fillRect(warningX-1,warningY-1,15,14,CARD);
    if (warning) drawWarningSymbol(warningX,warningY);
  }

  if (onlineChanged || cache.marker1!=marker) {
    drawRangeBar(barX,barY,barW,value,setting,online);
    cache.marker1=marker;
  }

  cache.online=online;
  cache.warning=warning;
  cache.initialized=true;
}

void formatDaysText(int days,char* out,size_t len) {
  if (days<0) snprintf(out,len,"N/A");
  else snprintf(out,len,"%dd",days);
}

void updateHistoryCard() {
  char feedText[12],shedText[12];
  formatDaysText(lastFeedDays,feedText,sizeof(feedText));
  formatDaysText(lastShedDays,shedText,sizeof(shedText));

  bool feedWarning=
    lastFeedDays>=0 &&
    lastFeedDays>feedWarningDays;

  if (strcmp(cachedFeedText,feedText)!=0) {
    // Keep the right side free for the warning symbol.
    tft.fillRect(INFO_X+9,INFO_Y+18,INFO_W-39,18,CARD);
    tft.setTextSize(2);
    tft.setTextColor(TEXT);
    tft.setCursor(INFO_X+9,INFO_Y+19);
    tft.print(feedText);
    strcpy(cachedFeedText,feedText);
  }

  if (cachedFeedWarningState!=(int)feedWarning) {
    int warningX=INFO_X+INFO_W-23;
    int warningY=INFO_Y+21;
    tft.fillRect(warningX-1,warningY-1,15,14,CARD);
    if (feedWarning) drawWarningSymbol(warningX,warningY);
    cachedFeedWarningState=(int)feedWarning;
  }

  if (strcmp(cachedShedText,shedText)!=0) {
    tft.fillRect(INFO_X+9,INFO_Y+50,INFO_W-18,18,CARD);
    tft.setTextSize(2);
    tft.setTextColor(TEXT);
    tft.setCursor(INFO_X+9,INFO_Y+51);
    tft.print(shedText);
    strcpy(cachedShedText,shedText);
  }
}

void updateIdleValues() {
  updateAHT20Card();
  updateHistoryCard();
  updateDS18B20Card(COOL_X,"DS18B20L","COOL SIDE",coolTemp,params[1],coolStatus,coolProbeOnline,coolCache);
  updateDS18B20Card(WARM_X,"DS18B20R","WARM SIDE",warmTemp,params[0],warmStatus,warmProbeOnline,warmCache);
}

// ============================================================
// MENU HELPERS
// ============================================================
int getTopIndex(
  int index,
  int count
) {
  int top = index - 2;

  if (top < 0) {
    top = 0;
  }

  if (
    top >
    count - visibleRows
  ) {
    top =
      count - visibleRows;
  }

  if (top < 0) {
    top = 0;
  }

  return top;
}

const char* labelForMode(ScreenMode mode, int index) {
  if (mode == MAIN) return mainMenu[index];
  if (mode == PARAM_MENU) return paramMenu[index];
  if (mode == FEED_MENU) return feedMenu[index];
  return "";
}

int textWidth(const char* text) {
  int16_t x1;
  int16_t y1;
  uint16_t width;
  uint16_t height;

  tft.setTextSize(1);

  tft.getTextBounds(
    text,
    0,
    0,
    &x1,
    &y1,
    &width,
    &height
  );

  return width;
}

void drawRightText(
  const char* value,
  int y,
  bool selectedRow
) {
  int width = textWidth(value);

  tft.setTextSize(1);

  tft.setTextColor(
    selectedRow ?
    SELTXT :
    MUTED
  );

  tft.setCursor(
    rowX +
    rowW -
    width -
    14,
    y + 8
  );

  tft.print(value);
}

// ============================================================
// MANUALLY DRAWN ± SYMBOL
// ============================================================
// The built-in GFX font does not reliably include ±,
// so the symbol is drawn directly.

void drawPlusMinusSymbol(
  int x,
  int y,
  uint16_t colour
) {
  // Plus sign
  tft.drawFastHLine(
    x,
    y + 3,
    5,
    colour
  );

  tft.drawFastVLine(
    x + 2,
    y + 1,
    5,
    colour
  );

  // Lower minus sign
  tft.drawFastHLine(
    x,
    y + 8,
    5,
    colour
  );
}

// ============================================================
// PARAMETER FIELD DRAWING
// ============================================================
void drawTextField(
  const char* value,
  int x,
  int y,
  bool active,
  bool selectedRow
) {
  int width = textWidth(value);
  int boxWidth = width + 10;
  int boxHeight = 15;

  if (active) {
    uint16_t fillColour =
      selectedRow ?
      SELTXT :
      SELECT;

    uint16_t textColour =
      selectedRow ?
      SELECT :
      SELTXT;

    tft.fillRoundRect(
      x,
      y + 4,
      boxWidth,
      boxHeight,
      4,
      fillColour
    );

    tft.setTextColor(textColour);
  } else {
    tft.setTextColor(
      selectedRow ?
      SELTXT :
      MUTED
    );
  }

  tft.setTextSize(1);
  tft.setCursor(x + 5, y + 8);
  tft.print(value);
}

void drawPlusMinusField(
  const char* value,
  int x,
  int y,
  bool active,
  bool selectedRow
) {
  int valueWidth = textWidth(value);
  int boxWidth = valueWidth + 22;
  int boxHeight = 15;

  uint16_t fieldColour;

  if (active) {
    uint16_t fillColour =
      selectedRow ?
      SELTXT :
      SELECT;

    fieldColour =
      selectedRow ?
      SELECT :
      SELTXT;

    tft.fillRoundRect(
      x,
      y + 4,
      boxWidth,
      boxHeight,
      4,
      fillColour
    );
  } else {
    fieldColour =
      selectedRow ?
      SELTXT :
      MUTED;
  }

  drawPlusMinusSymbol(
    x + 5,
    y + 6,
    fieldColour
  );

  tft.setTextSize(1);
  tft.setTextColor(fieldColour);
  tft.setCursor(x + 14, y + 8);
  tft.print(value);
}

void drawSignedToleranceField(
  const char* value,
  char sign,
  int x,
  int y,
  bool active,
  bool selectedRow
) {
  char buffer[12];

  buffer[0] = sign;
  buffer[1] = '\0';

  strcat(buffer, value);

  drawTextField(
    buffer,
    x,
    y,
    active,
    selectedRow
  );
}

void drawParamSummary(
  int paramIndex,
  int y,
  bool selectedRow
) {
  ParamSetting& setting =
    params[paramIndex];

  char optLowBuffer[8];
  char optHighBuffer[8];
  char tolLowBuffer[8];
  char tolHighBuffer[8];

  formatCompactNumber(
    optLowBuffer,
    setting.optLow,
    setting.isHumidity
  );

  formatCompactNumber(
    optHighBuffer,
    setting.optHigh,
    setting.isHumidity
  );

  formatCompactNumber(
    tolLowBuffer,
    setting.tolLow,
    setting.isHumidity
  );

  formatCompactNumber(
    tolHighBuffer,
    setting.tolHigh,
    setting.isHumidity
  );

  bool currentRowEditing =
    editingParam &&
    paramSelected == paramIndex;

  int x = 154;

  drawTextField(
    optLowBuffer,
    x,
    y,
    currentRowEditing &&
    activeParamField == 0,
    selectedRow
  );

  x += 34;

  drawTextField(
    optHighBuffer,
    x,
    y,
    currentRowEditing &&
    activeParamField == 1,
    selectedRow
  );

  x += 34;

  bool symmetric =
    fabs(
      setting.tolLow -
      setting.tolHigh
    ) < 0.05;

  // Condense matching tolerances to ±X when not editing.
  //
  // While editing, display -X and +Y separately so all
  // four stored values remain individually editable.
  if (
    symmetric &&
    !currentRowEditing
  ) {
    drawPlusMinusField(
      tolLowBuffer,
      x,
      y,
      false,
      selectedRow
    );
  } else {
    drawSignedToleranceField(
      tolLowBuffer,
      '-',
      x,
      y,
      currentRowEditing &&
      activeParamField == 2,
      selectedRow
    );

    x += 39;

    drawSignedToleranceField(
      tolHighBuffer,
      '+',
      x,
      y,
      currentRowEditing &&
      activeParamField == 3,
      selectedRow
    );
  }
}

// ============================================================
// ROW DRAWING
// ============================================================
bool mainItemEnabled(int index) {
  if (index == 1) return lightMode == MODE_ON;
  if (index == 3) return fanMode == MODE_ON;
  return true;
}
int nextEnabledMainIndex(int current, int direction) {
  int candidate=current;
  while (true) {
    candidate += direction;
    if (candidate < 0 || candidate >= mainCount) return current;
    if (mainItemEnabled(candidate)) return candidate;
  }
}
void formatDateTimeField(int field,char* b,size_t n) {
  if (field==0) snprintf(b,n,"%02u",clockTime.day);
  else if (field==1) snprintf(b,n,"%02u",clockTime.month);
  else if (field==2) snprintf(b,n,"%04u",clockTime.year);
  else if (field==3) snprintf(b,n,"%02u",clockTime.hour);
  else if (field==4) snprintf(b,n,"%02u",clockTime.minute);
  else b[0]='\0';
}

void drawInlineDateTime(int y, bool selectedRow) {
  char fields[5][6];
  for (int i=0;i<5;i++) formatDateTimeField(i, fields[i], sizeof(fields[i]));

  const char* separators[4] = {"/", "/", " ", ":"};
  int widths[5];
  int total=0;
  for (int i=0;i<5;i++) {
    widths[i]=textWidth(fields[i]);
    total += widths[i];
    if (i<4) total += textWidth(separators[i]);
  }

  int x = rowX + rowW - total - 14;
  uint16_t colour = selectedRow ? SELTXT : MUTED;
  tft.setTextSize(1);
  tft.setTextColor(colour);

  for (int i=0;i<5;i++) {
    tft.setCursor(x, y+8);
    tft.print(fields[i]);
    if (editingDateTime && selectedRow && i==dateTimeField) {
      tft.drawFastHLine(x, y+17, widths[i], colour);
    }
    x += widths[i];
    if (i<4) {
      tft.setCursor(x, y+8);
      tft.print(separators[i]);
      x += textWidth(separators[i]);
    }
  }
}

void drawRow(ScreenMode mode,int row,int realIndex,bool isSelected) {
  int y=startY+row*(rowH+rowGap);
  tft.fillRect(rowX-2,y-1,rowW+4,rowH+2,BG);
  bool enabled=(mode!=MAIN)||mainItemEnabled(realIndex);
  if (isSelected && enabled) {
    tft.fillRoundRect(rowX,y,rowW,rowH,7,SELECT);
    tft.setTextColor(SELTXT);
  } else tft.setTextColor(enabled?TEXT:MUTED);
  tft.setTextSize(1); tft.setCursor(rowX+14,y+8); tft.print(labelForMode(mode,realIndex));
  if (mode==MAIN) {
    if (realIndex==0) drawRightText(modeText(lightMode),y,isSelected);
    if (realIndex==1) { char b[8]; brightnessText(b); drawRightText(b,y,isSelected); }
    if (realIndex==2) drawRightText(modeText(fanMode),y,isSelected);
    if (realIndex==3) { char b[8]; fanSpeedText(b); drawRightText(b,y,isSelected); }
    if (realIndex==4) drawInlineDateTime(y,isSelected);
  }
  if (mode==PARAM_MENU && realIndex<4) drawParamSummary(realIndex,y,isSelected);
  if (mode==PARAM_MENU && realIndex==4) {
    char b[12];
    snprintf(b,sizeof(b),"%dd",feedWarningDays);
    drawRightText(b,y,isSelected);
    if (editingFeedWarning && isSelected) {
      int tw=textWidth(b);
      tft.drawFastHLine(rowX+rowW-tw-14,y+17,tw,SELTXT);
    }
  }
  if (mode==FEED_MENU && realIndex==1) { char b[12]; formatDaysText(lastFeedDays,b,sizeof(b)); drawRightText(b,y,isSelected); if (editingLastFeed && isSelected) { int tw=textWidth(b); tft.drawFastHLine(rowX+rowW-tw-14,y+17,tw,SELTXT); } }
}

void drawListInitial(
  ScreenMode mode,
  int count,
  int index
) {
  tft.fillScreen(BG);

  topIndex =
    getTopIndex(
      index,
      count
    );

  for (
    int row = 0;
    row < visibleRows;
    row++
  ) {
    int realIndex =
      topIndex + row;

    if (realIndex >= count) {
      break;
    }

    drawRow(
      mode,
      row,
      realIndex,
      realIndex == index
    );
  }

}

void updateListFast(
  ScreenMode mode,
  int count,
  int oldIndex,
  int newIndex
) {
  int newTop =
    getTopIndex(
      newIndex,
      count
    );

  // Full visible-list refresh only when the list scrolls.
  if (newTop != topIndex) {
    topIndex = newTop;

    tft.fillRect(
      0,
      0,
      W,
      152,
      BG
    );

    for (
      int row = 0;
      row < visibleRows;
      row++
    ) {
      int realIndex =
        topIndex + row;

      if (realIndex >= count) {
        break;
      }

      drawRow(
        mode,
        row,
        realIndex,
        realIndex == newIndex
      );
    }

    return;
  }

  // Otherwise redraw only the old and new selected rows.
  int oldRow =
    oldIndex - topIndex;

  int newRow =
    newIndex - topIndex;

  if (
    oldRow >= 0 &&
    oldRow < visibleRows
  ) {
    drawRow(
      mode,
      oldRow,
      oldIndex,
      false
    );
  }

  if (
    newRow >= 0 &&
    newRow < visibleRows
  ) {
    drawRow(
      mode,
      newRow,
      newIndex,
      true
    );
  }

}

void refreshMainRow(
  int mainIndex
) {
  if (screen != MAIN) {
    return;
  }

  int row =
    mainIndex - topIndex;

  if (
    row >= 0 &&
    row < visibleRows
  ) {
    drawRow(
      MAIN,
      row,
      mainIndex,
      mainIndex == selected
    );
  }
}

void refreshParamRow() {
  int row =
    paramSelected - topIndex;

  if (
    row >= 0 &&
    row < visibleRows
  ) {
    drawRow(
      PARAM_MENU,
      row,
      paramSelected,
      true
    );
  }
}

// ============================================================
// MENU NAVIGATION
// ============================================================
void openMainMenu() {
  screen=MAIN; selected=0; editingBrightness=false; editingFanSpeed=false; editingDateTime=false; editingParam=false; editingFeedWarning=false; activeParamField=-1;
  drawListInitial(MAIN,mainCount,selected);
}

void openFeedMenu() {
  screen=FEED_MENU; feedSelected=0; editingLastFeed=false;
  editingBrightness=false; editingFanSpeed=false; editingDateTime=false; editingParam=false; activeParamField=-1;
  drawListInitial(FEED_MENU,feedCount,feedSelected);
}

void openParamMenu() {
  screen=PARAM_MENU; paramSelected=0; editingBrightness=false; editingFanSpeed=false; editingDateTime=false; editingParam=false; editingFeedWarning=false; activeParamField=-1;
  drawListInitial(PARAM_MENU,paramCount,paramSelected);
}

void changeDateTimeValue(int direction) {
  if (dateTimeField==0) clockTime.day=constrain((int)clockTime.day+direction,1,(int)daysInMonth(clockTime.year,clockTime.month));
  else if (dateTimeField==1) clockTime.month=constrain((int)clockTime.month+direction,1,12);
  else if (dateTimeField==2) clockTime.year=constrain((int)clockTime.year+direction,2024,2099);
  else if (dateTimeField==3) clockTime.hour=(clockTime.hour+direction+24)%24;
  else if (dateTimeField==4) clockTime.minute=(clockTime.minute+direction+60)%60;
  clockTime.day=min((int)clockTime.day,(int)daysInMonth(clockTime.year,clockTime.month));
  refreshMainRow(4);
}

// ============================================================
// BRIGHTNESS EDITING
// ============================================================
void changeBrightness(
  int direction
) {
  lightBrightness +=
    direction * 5;

  if (lightBrightness < 0) {
    lightBrightness = 0;
  }

  if (lightBrightness > 100) {
    lightBrightness = 100;
  }

  updateOutputs();
  refreshMainRow(1);
}

void changeFanSpeed(int direction) {
  fanSpeed=constrain(fanSpeed+direction*5,0,100);
  updateOutputs(); refreshMainRow(3);
}

// ============================================================
// PARAMETER EDITING
// ============================================================
void changeActiveParamValue(
  int direction
) {
  if (
    !editingParam ||
    paramSelected >= 4
  ) {
    return;
  }

  ParamSetting& setting =
    params[paramSelected];

  float step =
    setting.isHumidity ?
    1.0 :
    0.1;

  if (activeParamField == 0) {
    setting.optLow +=
      direction * step;
  }

  if (activeParamField == 1) {
    setting.optHigh +=
      direction * step;
  }

  if (activeParamField == 2) {
    setting.tolLow +=
      direction * step;
  }

  if (activeParamField == 3) {
    setting.tolHigh +=
      direction * step;
  }

  // Keep optimal low and high ordered.
  if (
    setting.optLow >
    setting.optHigh
  ) {
    if (activeParamField == 0) {
      setting.optLow =
        setting.optHigh;
    } else {
      setting.optHigh =
        setting.optLow;
    }
  }

  // Tolerances cannot be negative.
  if (setting.tolLow < 0) {
    setting.tolLow = 0;
  }

  if (setting.tolHigh < 0) {
    setting.tolHigh = 0;
  }

  if (setting.isHumidity) {
    if (setting.optLow < 0) {
      setting.optLow = 0;
    }

    if (setting.optHigh > 100) {
      setting.optHigh = 100;
    }

    if (setting.tolLow > 50) {
      setting.tolLow = 50;
    }

    if (setting.tolHigh > 50) {
      setting.tolHigh = 50;
    }
  } else {
    if (setting.optLow < 0) {
      setting.optLow = 0;
    }

    if (setting.optHigh > 50) {
      setting.optHigh = 50;
    }

    if (setting.tolLow > 15) {
      setting.tolLow = 15;
    }

    if (setting.tolHigh > 15) {
      setting.tolHigh = 15;
    }
  }

  updateStatuses();
  updateOutputs();
  refreshParamRow();
}

// ============================================================
// ROTARY ACTIONS
// ============================================================
void rotate(int direction) {
  lastInput=millis();
  if (screen==IDLE) { openMainMenu(); return; }
  if (screen==MAIN) {
    if (editingBrightness) { changeBrightness(direction); return; }
    if (editingFanSpeed) { changeFanSpeed(direction); return; }
    if (editingDateTime) { changeDateTimeValue(direction); return; }
    int old=selected; selected=nextEnabledMainIndex(selected,direction);
    if (old!=selected) updateListFast(MAIN,mainCount,old,selected);
    return;
  }
  if (screen==FEED_MENU) {
    if (editingLastFeed) { lastFeedDays=constrain(lastFeedDays+direction,0,9999); int row=feedSelected-topIndex; if(row>=0&&row<visibleRows) drawRow(FEED_MENU,row,feedSelected,true); return; }
    int old=feedSelected; feedSelected=constrain(feedSelected+direction,0,feedCount-1);
    if (old!=feedSelected) updateListFast(FEED_MENU,feedCount,old,feedSelected); return;
  }
  if (screen==PARAM_MENU) {
    if (editingParam) { changeActiveParamValue(direction); return; }
    if (editingFeedWarning) {
      feedWarningDays=constrain(feedWarningDays+direction,1,999);
      refreshParamRow();
      return;
    }
    int old=paramSelected; paramSelected=constrain(paramSelected+direction,0,paramCount-1);
    if (old!=paramSelected) updateListFast(PARAM_MENU,paramCount,old,paramSelected); return;
  }
}

// ============================================================
// BUTTON ACTIONS
// ============================================================
void buttonPress() {
  lastInput=millis();
  if (screen==IDLE) { openMainMenu(); return; }
  if (screen==MAIN) {
    if (editingDateTime && selected==4) {
      dateTimeField++;
      if (dateTimeField>4) {
        editingDateTime=false;
        dateTimeField=0;
        clockTime.second=0;
        lastClockMillis=millis();
        saveSettings();
      }
      refreshMainRow(4);
      return;
    }
    if (selected==0) { cycleMode(lightMode); editingBrightness=false; updateOutputs(); saveSettings(); drawListInitial(MAIN,mainCount,selected); return; }
    if (selected==1 && lightMode==MODE_ON) { editingBrightness=!editingBrightness; if(!editingBrightness)saveSettings(); refreshMainRow(1); return; }
    if (selected==2) { cycleMode(fanMode); editingFanSpeed=false; updateOutputs(); saveSettings(); drawListInitial(MAIN,mainCount,selected); return; }
    if (selected==3 && fanMode==MODE_ON) { editingFanSpeed=!editingFanSpeed; if(!editingFanSpeed)saveSettings(); refreshMainRow(3); return; }
    if (selected==4) { editingDateTime=true; dateTimeField=0; editingBrightness=false; editingFanSpeed=false; refreshMainRow(4); return; }
    if (selected==5) { openParamMenu(); return; }
    if (selected==6) { openFeedMenu(); return; }
    return;
  }
  if (screen==FEED_MENU) {
    if (feedSelected==0) { lastFeedDays=0; saveSettings(); int valueRow=1-topIndex; if(valueRow>=0&&valueRow<visibleRows) drawRow(FEED_MENU,valueRow,1,false); int row=feedSelected-topIndex; if(row>=0&&row<visibleRows) drawRow(FEED_MENU,row,feedSelected,true); return; }
    if (feedSelected==1) {
      if (lastFeedDays<0) lastFeedDays=0;
      editingLastFeed=!editingLastFeed;
      if (!editingLastFeed) saveSettings();
      int row=feedSelected-topIndex; if(row>=0&&row<visibleRows) drawRow(FEED_MENU,row,feedSelected,true); return;
    }
    screen=MAIN; drawListInitial(MAIN,mainCount,selected); return;
  }
  if (screen==PARAM_MENU) {
    if (paramSelected<4) {
      if (!editingParam) { editingParam=true; activeParamField=0; }
      else if (++activeParamField>3) { editingParam=false; activeParamField=-1; saveSettings(); }
      refreshParamRow(); return;
    }
    if (paramSelected==4) {
      editingFeedWarning=!editingFeedWarning;
      if (!editingFeedWarning) saveSettings();
      refreshParamRow();
      return;
    }
    if (paramSelected==5) {
      resetParams();
      saveSettings();
      updateStatuses();
      updateOutputs();
      drawListInitial(PARAM_MENU,paramCount,paramSelected);
      return;
    }
    screen=MAIN;
    drawListInitial(MAIN,mainCount,selected);
    return;
  }
}

// ============================================================
// INPUT READING
// ============================================================
void readEncoder() {
  int currentS1 =
    digitalRead(ENC_S1);

  if (currentS1 != lastS1) {
    if (
      millis() -
      lastMoveTime >
      2
    ) {
      if (currentS1 == LOW) {
        int currentS2 =
          digitalRead(ENC_S2);

        if (currentS2 == HIGH) {
          rotate(1);
        } else {
          rotate(-1);
        }

        lastMoveTime =
          millis();
      }
    }
  }

  lastS1 = currentS1;
}

void readButton() {
  bool currentButton =
    digitalRead(ENC_KEY);

  if (
    lastButton == HIGH &&
    currentButton == LOW
  ) {
    if (
      millis() -
      lastButtonTime >
      180
    ) {
      lastButtonTime =
        millis();

      buttonPress();
    }
  }

  lastButton =
    currentButton;
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  // Encoder
  pinMode(
    ENC_S1,
    INPUT_PULLUP
  );

  pinMode(
    ENC_S2,
    INPUT_PULLUP
  );

  pinMode(
    ENC_KEY,
    INPUT_PULLUP
  );

  lastS1 =
    digitalRead(ENC_S1);

  lastButton =
    digitalRead(ENC_KEY);

  loadSettings();

  // ----------------------------------------------------------
  // LIGHT AND FAN PWM
  // ----------------------------------------------------------
  bool lightPwmAttached = ledcAttach(LIGHT_PIN, LIGHT_PWM_FREQUENCY, LIGHT_PWM_RESOLUTION);
  bool fanPwmAttached = ledcAttach(FAN_PIN, LIGHT_PWM_FREQUENCY, LIGHT_PWM_RESOLUTION);
  if (!lightPwmAttached) Serial.println("ERROR: Failed to attach lighting PWM");
  if (!fanPwmAttached) Serial.println("ERROR: Failed to attach fan PWM");
  writePwmPercent(LIGHT_PIN, 0.0f, LIGHT_ACTIVE_HIGH);
  writePwmPercent(FAN_PIN, 0.0f, FAN_ACTIVE_HIGH);
  lightOutputOn = false;
  fanOutputOn = false;
  lastFadeUpdate = millis();
  lastClockMillis = millis();

  // ----------------------------------------------------------
  // TFT
  // ----------------------------------------------------------
  SPI.begin(
    TFT_SCLK,
    -1,
    TFT_MOSI,
    TFT_CS
  );

  tft.init(170, 320);
  tft.setRotation(1);
  tft.setSPISpeed(40000000);
  tft.setTextWrap(false);

  // ----------------------------------------------------------
  // AHT20
  // ----------------------------------------------------------
  Wire.begin(
    AHT20_SDA,
    AHT20_SCL
  );

  if (!aht.begin()) {
    Serial.println(
      "AHT20 not found"
    );
  } else {
    Serial.println(
      "AHT20 found"
    );
  }

  // ----------------------------------------------------------
  // DS18B20
  // ----------------------------------------------------------
  ds18b20.begin();

  dsCount =
    ds18b20.getDeviceCount();

  Serial.print(
    "DS18B20 devices found: "
  );

  Serial.println(dsCount);

  for (
    int i = 0;
    i < dsCount;
    i++
  ) {
    DeviceAddress address;

    if (
      ds18b20.getAddress(
        address,
        i
      )
    ) {
      Serial.print("Probe ");
      Serial.print(i);
      Serial.print(": ");

      printDSAddress(address);

      Serial.println();
    }
  }

  // ----------------------------------------------------------
  // INITIAL STATE
  // ----------------------------------------------------------
  readSensors();

  // Applies:
  // Lighting ON at 100%
  // Fan OFF
  updateOutputs();

  drawIdleLayout();
  updateIdleValues();

  lastSensorRead = millis();
  lastInput = millis();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  readEncoder();
  readButton();

  unsigned long now =
    millis();

  if (
    now -
    lastSensorRead >=
    SENSOR_INTERVAL
  ) {
    lastSensorRead = now;

    readSensors();
    updateOutputs();

    if (screen == IDLE) {
      updateIdleValues();
    }
  }

  // Keeps PWM and any automatic output logic current.
  updateOutputs();

  if (
    screen != IDLE &&
    now - lastInput >
    TIMEOUT_MS
  ) {
    editingParam = false;
    editingBrightness = false;
    editingFanSpeed = false;
    editingDateTime = false;
    editingLastFeed = false;
    editingFeedWarning = false;
    editingFanSpeed = false;
    activeParamField = -1;

    drawIdleLayout();
    updateIdleValues();
  }
}