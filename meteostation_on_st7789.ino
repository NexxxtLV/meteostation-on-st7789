#include <Adafruit_ST7789.h>
#include <RTClib.h>
#include <GyverBME280.h>
#include <SensirionI2CScd4x.h>
#include <GyverButton.h>

#define DEBUG 0

#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8
#define TFT_MOSI 11
#define TFT_SCLK 13

#define WHITE 0xFFFF
#define BLACK 0x0000
#define YELLOW 0xFFE0
#define GREEN 0x07E0
#define DKYELLOW 0x8400
#define DKGREY 0x4A49

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST); //display
RTC_DS3231 rtc; //real time clock
GyverBME280 bme; //temperature, pressure, humidity sensor
SensirionI2CScd4x scd4x; //СО2 sensor
GButton button1(3);
GButton button2(4);

namespace scd41
{
  uint16_t co2 = 0;
  float temperature = 0.f;
  float humidity = 0.f;
};

float bmeTemperature = 0.f;
float bmeHumidity = 0.f;
float bmePressure = 0.f;
float seaLevelPressure = 1013.25f;
float voltage = 0.f;

uint32_t measurementTickCount = 0;
uint32_t mainTickCount = 0; 
uint32_t altitudeTickCount = 0;
uint32_t pressureTickCount = 0;
uint32_t debugTickCount = 0;
 
uint16_t measurementUpdateDelay = 15000;
uint16_t mainUpdateDelay = 15000;
uint16_t altitudeUpdateDelay = 1000;
uint32_t pressureUpdateDelay = 1800000;

bool isDisplayVisible = 1;
bool isAltitudeMode = 0;

int timeW, timeH = 0;
int dateW = 0;
int co2H, co2W = 0;

const int gx0 = 31;  //x lower left graph location
const int gy0 = 224; //y lower left graph location
const int gx1 = 320; //x lower right graph location
const int gy1 = 8; //y upper left graph location
float gyMinValue, gyMaxValue = 0.f;
float pressureSyncDelay = 0;
int currentIndex = 0;
int currentDayIndex = 0;
bool isPlotPrinted = 0;

float pressureArr[2][49];
int defaultPlotX[2][1] = {31, 31};
byte plotDay[2][1];
byte test = 0;
byte mode = 0;

String draw12Hour()
{
  String str = "";
  DateTime now = rtc.now();
  const char* period = "AM"; 

  if (now.isPM())
    period = "PM";
      
  str += now.twelveHour();
  str += ":";
  if (now.minute() < 10) 
    str += "0";
  str += now.minute();
  str += period;
  
  return str;
}

String drawDate()
{
  DateTime now = rtc.now();
  char buf[] = "DD/MM/YYYY DDD";

  return now.toString(buf);
}

//https://provideyourown.com/2012/secret-arduino-voltmeter-measure-battery-voltage/
float readVcc() 
{
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // unlocks both

  long result = (high<<8) | low;

  result = 1163155L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result*0.001f; // Vcc in Volts
}

void drawAxisX()
{
  int segments = 48; //24 for hours and 24 for half hour
  int segmentsWidth = 6; //6 measurements per hour (6 pixels between segments)
  int numberOffset = gx0 - 2; //2 is centering numbers under the segment line

  //draw X axis
  tft.drawLine(gx0, gy0, gx1, gy0, YELLOW);

  //draw segments
  for (int i = 1; i <= segments; i++)
  {
    int x = gx0 + i*segmentsWidth;
    if (i%2 == 0)
      tft.drawLine(x, gy0+4, x, 24, DKYELLOW);
    else
      tft.drawLine(x, gy0+2, x, 24, DKGREY);
  }

  //draw hours under segments
  for (int i = 0; i <= 24; i++)
  {
    //remove odd hours from 10 to 24 cause they're don't fit on the screen
    if (i > 9 && i % 2 == 1) 
      continue;
    
    if (i < 10)
      tft.setCursor(numberOffset, gy0+8); //shift hours numbers from 0 to 10
    else
      tft.setCursor(numberOffset-3, gy0+8); //shift even hours numbers from 10 to 24

    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.print(i);

    numberOffset += (i < 10) ? 12 : ((i == 22) ? 18 : 24);
  }
}

void drawAxisY(int basePressure)
{
  int segments = 21; //max height includes 21 numbers
  int segmentsWidthY = 10; //10 pixels between segments
  int numberOffset = gy0 - 3;

  int roundedValue = round(basePressure);
  int middleIndex = segments / 2;
  float yAxisValue = 0;
  float pressureStep = 0.5f;
  String str = "";
  DateTime now = rtc.now();

  //draw Y axis
  tft.drawLine(gx0, gy0, gx0, gy1, YELLOW);
  tft.setCursor(8, 8);
  tft.print("hPa");

  str += plotDay[test][0];
  str += "/"; 
  str += now.month();
  str += "/";
  str += now.year();
  tft.setCursor(41, 8);
  tft.print(str);

  for (int i = 0; i < segments; ++i) 
  { 
    //draw segments
    int y = gy0 - i*segmentsWidthY;
    if (i%2 == 0)
      tft.drawLine(gx0, y, 320, y, DKYELLOW);
    else
      tft.drawLine(gx0, y, 320, y, DKGREY);

    //generate and draw numbers of pressure where base pressure value will be in center 
    yAxisValue = roundedValue + (i - middleIndex) * pressureStep;
    tft.setCursor(0, y-3);
    tft.print(yAxisValue, 1);
    numberOffset += segmentsWidthY;

    if (i == 0)
      gyMinValue = yAxisValue;
    else if (i == segments-1)
      gyMaxValue = yAxisValue;
  }
}

float customMap(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void plotData(float data[][49])
{
  int plotX = defaultPlotX[test][0];
  int prevX = defaultPlotX[test][0]; 
  int prevY = 0;

  for (int i = 0; i < 49; i++)
  {
    //Serial.println(data[test][i]);
    int y = customMap(data[test][i], gyMinValue, gyMaxValue, 224, 24);
    
    if (data[test][i] == 0)
      continue;

    if (i == 0)
    {
      tft.drawLine(prevX, y, plotX, y, GREEN); //a dot will be drawn (beginning of the graphic)
      plotX += 6;
      prevY = y;
      continue;
    }
    
    tft.drawLine(prevX, prevY, plotX, prevY, GREEN); //intemediate horizontal line
    tft.drawLine(prevX+6, prevY, plotX, y, GREEN); //vertical line
    plotX += 6;
    prevX += 6;
    prevY = y;
  }
}

void syncPlot() 
{
  DateTime now = rtc.now();
  long waitSeconds = 0;

  if (now.minute() < 30) 
  {
    defaultPlotX[0][0] += 6;
    waitSeconds = (1800 - now.minute()*60 - now.second());
  }
  else if (now.minute() < 60)
  {
    defaultPlotX[0][0] += 12;
    waitSeconds = (3600 - now.minute()*60 - now.second());
  }

  defaultPlotX[0][0] += now.hour()*12;
  pressureUpdateDelay = waitSeconds*1000+5000;
}

void redrawPlot()
{
  tft.setTextSize(1);

  switch (mode)
  {
    case 1:
      test = 0;
      drawAxisX();
      drawAxisY(pressureArr[0][0]);
      plotData(pressureArr);
      break;
    case 2:
      test = 1;
      drawAxisX();
      drawAxisY(pressureArr[1][0]);
      plotData(pressureArr);
      break;
  }
}

void drawFirstScreen()
{
  DateTime now = rtc.now();
  tft.setTextColor(WHITE, BLACK); //redraw new chars

  if (readVcc() > 4.8f)
  {
    tft.setTextSize(1);
    tft.setCursor(0, 0);
    tft.print(voltage+0.03f); //0.03f is voltage drops on 10kOm R
    tft.print("V");
  }

  //fix bug (if time goes from 12:59PM->1:00AM it will be 11:00AM1)
  if (now.twelveHour() == 1 && now.minute() == 0)
    tft.fillRect(0, 0, 320, timeH, BLACK);

  tft.setTextSize(5);
  tft.getTextBounds(draw12Hour(), 0, 0, 0, 0, &timeW, &timeH);
  tft.setCursor(320/2 - timeW/2,0);
  tft.print(draw12Hour());

  tft.setTextSize(2);
  tft.getTextBounds(drawDate(), 0, 0, 0, 0, &dateW, 0);
  tft.setCursor(320/2 - dateW/2, tft.getCursorY()+timeH+5);
  tft.print(drawDate());
  
  tft.setTextSize(3);
  tft.setCursor(15,162);
  tft.print(bmeTemperature, 1);
  tft.drawCircle(3*6*5+10, 165, 3, WHITE); //for some reason GFX lib can't print degree sigh
  tft.setCursor(3*6*5+16, 165); //this is ridiculous, I can’t put space before C cause it's considered a char and has a black background that paints over the degree sigh
  tft.print("C");

  tft.setCursor(15, tft.getCursorY()+3*8+15);
  tft.print(bmeHumidity, 1);
  tft.print(" %");

  //if co2String length decreases by 1, then "ppm" will be "ppmm" due to strange text replacement 
  if (co2W < 3*6*4) //3-textSize; 6-width of one char; 4-number of chars
    tft.fillRect(160+3*6*7, 162, 160+3*6*8, co2H, BLACK); 
  
  tft.setCursor(160, 162);
  tft.getTextBounds(String(scd41::co2), 0, 0, 0, 0, &co2W, &co2H);
  tft.print(scd41::co2);
  tft.print(" ppm");

  tft.setCursor(160, tft.getCursorY()+co2H+15);
  tft.print(bmePressure, 1);
  tft.print("hPa");

  drawEasterEgg();
}

int pastDay = 0;
bool isDayChanged()
{
  DateTime now = rtc.now();
  if (pastDay != now.day())
  {
    pastDay = now.day();
    return true;
  }

  return false;
}

void drawEasterEgg()
{
  tft.setRotation(2); //it's works only in vertical rotation
  tft.drawPixel(0, 315, 0xFFFF); //RGB565 color
  tft.drawPixel(0, 316, 0x001F);
  tft.drawPixel(0, 317, 0x07E0);
  tft.drawPixel(0, 318, 0xF800);
  tft.drawPixel(0, 319, 0xFFFF);
  tft.setRotation(3);
}

void setup() 
{
#if DEBUG
  Serial.begin(9600);
#endif

  tft.init(240, 320);
  tft.setRotation(3);
  tft.fillScreen(BLACK);

  tft.setTextSize(2);
  tft.setCursor(70, 100);
  tft.print("Preparation for");
  tft.setCursor(40, 132);
  tft.print("starting the program");

  //Init realtime module   
  if (!rtc.begin())
  {
    Serial.println("DS3231 not found");
    //rtcOff = 1;
  }
  //rtc.adjust(DateTime(2024, 8, 16, 16, 50, 30)); //y, m, d, h, m, s

  //Init BME module
  if (!bme.begin()) 
  {
    Serial.println("BME not found"); // запуск датчика и проверка на работоспособность
    //bmeOff = 1;
  }

  //Init CO2 module and start measurement
  Wire.begin();
  scd4x.begin(Wire);

  //stop potentially previously started measurement to avoid errors
  if (scd4x.stopPeriodicMeasurement()) 
  {
    Serial.println("CO2 not found");
    //scd4xOff = 1;
  }
  scd4x.startPeriodicMeasurement(); 
  
  syncPlot();
  
  //Sync loops with current time
  DateTime now = rtc.now();
  pastDay = now.day();
  plotDay[0][0] = now.day();
  DateTime future (now + TimeSpan(1,0,0,0));
  plotDay[1][0] = future.day();

  int delayBeforeStart = (15-now.second()%15)*1000;
  delay(delayBeforeStart);
  measurementTickCount -= 15000 - delayBeforeStart;
  mainTickCount -= 15000 - delayBeforeStart;
  tft.fillScreen(BLACK);
}

void loop(void) 
{
  DateTime now = rtc.now();
  button1.tick();
  button2.tick();
 
#if DEBUG 
  if (millis() - debugTickCount >= 1000)
  {
    Serial.print("mainThreadDelay: ");
    Serial.println((int)(15-((millis() - mainTickCount)/1000)));
    Serial.print("pressureThreadDelay: ");
    Serial.println((int)(pressureUpdateDelay/1000-((millis() - pressureTickCount)/1000)));
    Serial.print("pressureUpdateDelay: ");
    Serial.println(pressureUpdateDelay);
    Serial.print("defaultPlotX: ");
    Serial.println(defaultPlotX[0][0]);
    Serial.print("Time: ");
    Serial.print(draw12Hour());
    Serial.println(now.second());

    debugTickCount = millis();
  } 
#endif

  if (button1.isSingle())
  {
    if (isAltitudeMode)
      seaLevelPressure = bmePressure;
    else
    {
      mode++;
      if (mode>2)
        mode = 0;

      isPlotPrinted = 0;
      tft.fillScreen(BLACK);
      mainTickCount = millis()-15000;
    }
  }

  //turn off display
  if (button1.isDouble())
  {
    isDisplayVisible = !isDisplayVisible;
    tft.enableDisplay(isDisplayVisible);
  }

  if (button1.isTriple())
  {
    isAltitudeMode = !isAltitudeMode;
    tft.fillScreen(BLACK);
    mainTickCount = millis()-15000;
  }

  if (button1.isHold())
  {
    mode = 0;
    mainTickCount = millis()-15000;
    seaLevelPressure = 1013.25f;
    tft.fillScreen(BLACK);
  }
  
  if (millis() - measurementTickCount >= measurementUpdateDelay)
  {
    measurementTickCount = millis();
    
    bmeTemperature = bme.readTemperature();
    bmeHumidity = bme.readHumidity();
    bmePressure = bme.readPressure()*0.01f;
    scd4x.readMeasurement(scd41::co2, scd41::temperature, scd41::humidity);
    voltage = (((float)analogRead(A7)/1024)*readVcc());
  }

  if (millis() - mainTickCount >= mainUpdateDelay)
  {
    mainTickCount = millis();

    if (mode == 0)
    {
      drawFirstScreen();
    }
    else
    {
      if (!isPlotPrinted)
      {
        redrawPlot();
        isPlotPrinted = 1;
      }
    }
  }
  
  if (isAltitudeMode && millis() - altitudeTickCount >= altitudeUpdateDelay)
  {
    bmePressure = bme.readPressure();
    tft.setCursor(160, 123);
    tft.print(-(8.31f*(15+273.15f)/(0.029f*9.81f))*log((bmePressure)/seaLevelPressure), 1);
    altitudeTickCount = millis();
  }

  if (millis() - pressureTickCount >= pressureUpdateDelay)
  { 
    pressureTickCount = millis();
    
    if (isDayChanged())
    {
      currentIndex = 0;
      currentDayIndex++;

      if (currentDayIndex == 2)
      {
        currentDayIndex = 1;
        plotDay[0][0] = plotDay[1][0];
        plotDay[1][0] = now.day();
        
        for (int i = 0; i < 49; i++)
        {
          defaultPlotX[0][0] = 31;

          pressureArr[0][i] = pressureArr[1][i];
          pressureArr[1][i] = 0;
        }
      }
    }

    pressureArr[currentDayIndex][currentIndex] = bmePressure;
    currentIndex++;
    pressureUpdateDelay = 1800000;
  }
}