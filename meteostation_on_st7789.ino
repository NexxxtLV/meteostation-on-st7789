#include <Adafruit_ST7789.h>
#include <Adafruit_GFX.h>
#include <RTClib.h>
#include <GyverBME280.h>
#include <SensirionI2CScd4x.h>
#include <GyverButton.h>

#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8
#define TFT_MOSI 11
#define TFT_SCLK 13

#define WHITE 0xFFFF
#define BLACK 0x0000

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
RTC_DS3231 rtc; //real time clock
GyverBME280 bme; //temperature, pressure, humidity sensor
SensirionI2CScd4x scd4x; //СО2 sensor
GButton button1(3);

byte mode = 0;

uint16_t co2 = 0;
float temperature = 0.f;
float humidity = 0.f;

float bmeTemperature = 0.f;
float bmeHumidity = 0.f;
float bmePressure = 0.f;
float seaLevelPressure = 1013.25f;
float voltage = 0.f;

float measurementTickCount = 0.f;
float mainTickCount = 0.f; 
float altitudeTickCount = 0.f;
float pressureTickCount = 0.f;
float debugTickCount = 0.f;
 
float measurementUpdateDelay = 15000;
float mainUpdateDelay = 15000;
float altitudeUpdateDelay = 1000;
float pressureUpdateDelay = 1800000;

bool isDisplayVisible = 1;
bool isAltitudeMode = 0;

int timeW, timeH = 0;
int dateW = 0;
int tempW, tempH = 0;
int co2H, co2W = 0;

const char* daysOfWeek[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

String draw12Hour()
{
  String str;
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
  String str;
  DateTime now = rtc.now();

  str += now.day();
  str += "/";
  str += now.month();
  str += "/";
  str += now.year();
  str += " ";
  str += daysOfWeek[now.dayOfTheWeek()];

  return str;
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

void setup() 
{
  Serial.begin(9600);

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
  //rtc.adjust(DateTime(2024, 8, 10, 0, 5, 45)); //y, m, d, h, m, s

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
  
  //Sync loops with current time
  DateTime now = rtc.now();
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
 
  /*
  if (millis() - debugTickCount >= 1000)
  {
    Serial.print("mainThreadDelay: ");
    Serial.println((int)(15-((millis() - mainTickCount)/1000)));

    debugTickCount = millis();
  } 
  */

  if (button1.isHold())
  {
    mode = 0;
    measurementUpdateDelay = 15000;
    mainUpdateDelay = 15000;
    mainTickCount = millis()-15000;
    seaLevelPressure = 1013.25f;
    tft.fillScreen(BLACK);
  }

  if (button1.isSingle())
  {
    // mode++;
    // if (mode>3)
    //   mode = 0;

    if (isAltitudeMode)
      seaLevelPressure = bmePressure*0.01f;
  }

  //turn display off
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

  if (millis() - measurementTickCount >= measurementUpdateDelay)
  {
    measurementTickCount = millis();
    
    bmeTemperature = bme.readTemperature();
    bmeHumidity = bme.readHumidity();
    bmePressure = bme.readPressure();
    scd4x.readMeasurement(co2, temperature, humidity);
    voltage = (((float)analogRead(A7)/1024)*readVcc());
  }

  if (millis() - mainTickCount >= mainUpdateDelay)
  {
    mainTickCount = millis();

    if (mode == 0)
    {
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
      tft.getTextBounds(String(bmeTemperature), 0, 0, 0, 0, &tempW, &tempH);
      tft.setCursor(15,162);
      tft.print(bmeTemperature, 1);
      tft.drawCircle(tempW+10, 165, 3, WHITE); //for some reason GFX lib can't print degree sigh
      tft.setCursor(tempW+16, 165); //this is ridiculous, I can’t put space before C cause it's considered a char and has a black background that paints over the degree sigh
      tft.print("C");

      tft.setCursor(15, tft.getCursorY()+tempH+15);
      tft.print(bmeHumidity, 1);
      tft.print(" %");

      //if co2String length decreases by 1, then "ppm" will be "ppmm" due to strange text replacement 
      if (co2W < 3*6*4) //3-textSize; 6-width of one char; 4-number of chars
        tft.fillRect(160+3*6*7, 162, 160+3*6*8, co2H, BLACK); 
      
      tft.setCursor(160, 162);
      tft.getTextBounds(String(co2), 0, 0, 0, 0, &co2W, &co2H);
      tft.print(co2);
      tft.print(" ppm");

      tft.setCursor(160, tft.getCursorY()+co2H+15);
      tft.print(bmePressure*0.01f, 1);
      tft.print("hPa");
    }
  }
  
  if (isAltitudeMode && millis() - altitudeTickCount >= altitudeUpdateDelay)
  {
    bmePressure = bme.readPressure();
    tft.setCursor(160, 123);
    tft.print(-(8.31f*(15+273.15f)/(0.029f*9.81f))*log((bmePressure*0.01f)/seaLevelPressure), 1);
    altitudeTickCount = millis();
  }
}