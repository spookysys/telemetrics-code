#include "SimCom.hpp"
#include "Logger.hpp"
#include "MySerial.hpp"

// APN setup
#define APN "data.lyca-mobile.no"
#define APN_USER "lmno"
#define APN_PW "plus"

// OpenWeatherMap key
#define OWM_APIKEY "18143027801bd9493887c2020cb2968e"



// serials and irq handlers
namespace simcom
{
MySerial gsmSerial;
MySerial gpsSerial;
}

void SERCOM2_Handler()
{
  simcom::gsmSerial.IrqHandler();
}

void SERCOM5_Handler()
{
  simcom::gpsSerial.IrqHandler();
}



namespace simcom
{
const auto PIN_GPS_EN = 26ul;
const auto PIN_STATUS = 25ul;
const auto PIN_PWRKEY = 38ul;

bool isOn()
{
  return digitalRead(PIN_STATUS);
}


void powerOnOff() {
  int startStatus = isOn();
  if (startStatus) {
    logger.println("SimCom is on - turning off");
  } else {
    logger.println("SimCom is off - turning on");
  }
  pinMode(PIN_PWRKEY, OUTPUT);
  digitalWrite(PIN_PWRKEY, LOW);
  delay(1000);
  for (int i = 0; i <= 20; i++) {
    delay(100);
    if (isOn() != startStatus) break;
    assert(i < 20);
  }
  digitalWrite(PIN_PWRKEY, HIGH);
  pinMode(PIN_PWRKEY, INPUT);
  //delay(100);
  int stopStatus = isOn();
  if (stopStatus) {
    logger.println("SimCom is now on");
  } else {
    logger.println("SimCom is now off");
  }
  assert(startStatus != stopStatus);
}

void OpenGsmSerial()
{
  logger.println("GSM: Opening serial");
  gsmSerial.begin("gsm",  115200,  3ul/*PA09 SERCOM2.1 RX<-GSM_TX */,  4ul/*PA08 SERCOM2.0 TX->GSM_RX*/, PIO_SERCOM_ALT, PIO_SERCOM_ALT, SERCOM_RX_PAD_1, UART_TX_PAD_0, &sercom2);
  logger.println();  
  
  logger.println("GSM: Detecting baud");
  gsmSerial.setTimeout(100);
  for (int i = 0; i <= 10; i++) {
    gsmSerial.println("AT");
    if (gsmSerial.find("OK\r")) break;
    assert(i < 10);
  }
  logger.println();  
  gsmSerial.setTimeout(1000);

  logger.println("GSM: Disabling echo");
  gsmSerial.println("ATE0");
  assert(gsmSerial.find("OK\r") != -1);
  logger.println();
  
  logger.println("GSM: Enabling flow control");
  gsmSerial.enableHandshaking(2ul /* RTS PA14 SERCOM2.2 */, 5ul /* CTS SERCOM2.3 */);
  gsmSerial.println("AT+IFC=2,2");
  assert(gsmSerial.find("OK\r") != -1);
  logger.println();  
}

void OpenGpsSerial()
{
  logger.println("GPS: Opening serial");
  gpsSerial.begin("gps", 115200, 31ul/*PB23 SERCOM5.3 RX<-GPS_TX */, 30ul/*PB22 SERCOM5.2 TX->GPS_RX*/, PIO_SERCOM_ALT, PIO_SERCOM_ALT, SERCOM_RX_PAD_3, UART_TX_PAD_2, &sercom5);
}

// state
bool gsm_inited = false;
bool gprs_connected = false;

void begin()
{
  logger.println("SimCom?");
  pinMode(PIN_GPS_EN, INPUT); // high-z
  pinMode(PIN_STATUS, INPUT_PULLDOWN);
  pinMode(PIN_PWRKEY, INPUT_PULLDOWN);

  // If module already on, reset it
  if (isOn()) {
    powerOnOff();
    assert(!isOn());
    delay(800);
    assert(!isOn());
  }

  // Turn module on
  assert(!isOn());
  powerOnOff();
  assert(isOn());


  // GSM
  OpenGsmSerial();


  // GPS
  // OpenGpsSerial();

  logger.println("SimCom!");
}


void update()
{
  long long t_millis = millis();

  // send commands from serial to GSM port
#ifdef DEBUG
  while (Serial.available()) {
    char ch = Serial.read();
    gsmSerial.write(ch);
  }
#endif

  // parse gsm input (depend on there being a \n every bufferfull)
  while (gsmSerial.contains('\n'))
  {
    String line = gsmSerial.readStringUntil('\n');
    line.trim();
    if (!gsm_inited && line == "SMS Ready") {
      gsm_inited = true;
      logger.println("GSM Initialized!");
    }
  }

  delay(500);

  /*
  static int hello=0;
  hello++;
  int rts = (hello>>4)&1;
  digitalWrite(PIN_GSM_RTS, rts); // 0 = active
  
  logger.print("RTS: ");
  logger.println(rts);
  delay(100);
  */


  const auto PIN_GSM_RTS = 2ul; // PA14 SERCOM2.2
  const auto PIN_GSM_CTS = 5ul; // PA15 SERCOM2.3

  
  //logger.print("CTS: ");
  //logger.println(digitalRead(PIN_GSM_CTS));


  // maintain connection every 10 seconds
  /*
    static long long last_connection_attempt = -10000;
    if (gsm_inited && t_millis >= last_connection_attempt+10000) {
    last_connection_attempt = t_millis;


    // http://www.instructables.com/id/How-to-make-a-Mobile-Cellular-Location-Logger-with/step3/The-AT-Commands/
    // https://github.com/sparkfun/phant-arduino/blob/master/src/Phant.cpp
    // https://github.com/sparkfun/phant-input-udp

    // https://libcoap.net/
    // https://github.com/1248/microcoap

    // AT+CMGF=1

    // AT+CGATT=1
    gsmSerial.println("AT+CGATT=1");
    assert(gsmSerial.find("OK\r") != -1);
    logger.println();

    // AT+SAPBR=3,1,"Contype","GPRS"
    gsmSerial.println("AT+SAPBR=3,1,\"Contype\",\"GPRS\"");
    assert(gsmSerial.find("OK\r") != -1);
    logger.println();

    // AT+SAPBR=3,1,"APN","data.lyca-mobile.no"
    gsmSerial.print("AT+SAPBR=3,1,\"APN\",\""); gsmSerial.print(APN); gsmSerial.println("\"");
    assert(gsmSerial.find("OK\r") != -1);
    logger.println();

    // AT+SAPBR=1,1
    gsmSerial.println("AT+SAPBR=1,1");
    assert(gsmSerial.find("OK\r") != -1);
    logger.println();

    // request location
    gsmSerial.println("AT+CIPGSMLOC=1,1");
    assert(gsmSerial.find("OK\r") != -1);
    logger.println();

    }
  */

}


}



// old junk
#if 0


// GSM: Wait for stuff to come up
gsmSerial.setTimeout(10000);
gsmSerial.find("SMS Ready\r");
logger.println();
delay(1000);


void getTime()
{
  gsmSerial.println("AT+CIPGSMLOC=1,1");
  assert(gsmSerial.find("+CIPGSMLOC:") != -1);
  delay(100);
}


void getWeatherData(int lat, int lon) {
  gsmSerial.setTimeout(10000);


  gsmSerial.println("AT+HTTPINIT");
  assert(gsmSerial.find("OK\r") != -1);
  logger.println();

  gsmSerial.println(String("") + "AT+HTTPPARA=\"URL\",\"api.openweathermap.org/data/2.5/weather?lat=" + lat + "&lon=" + lon + "&APPID=" + OWM_APIKEY + "\"");
  assert(gsmSerial.find("OK\r") != -1);
  logger.println();

  gsmSerial.println("AT+HTTPACTION=0");
  assert(gsmSerial.find("OK\r") != -1);
  assert(gsmSerial.find("+HTTPACTION") != -1);
  logger.println();

  gsmSerial.println("AT+HTTPREAD");
  assert(gsmSerial.find("OK\r") != -1);
  logger.println();

  // +HTTPREAD: 107
  // {"cod":401, "message": "Invalid API key. Pleamap.org/faq#error401 for more info."}

}

#endif




