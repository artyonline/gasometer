/******************************************************* Description ********************************************************

  This is an code written for the GASO METER which is used to measure the percentage of gas remaining
  in the gas cylinder. The calculated percentage is updated into a cloud then into an Andoid app for
  the user to see.

  You can configure WiFI login from yur Android phone. Press the Configuration button till the LCD display says
  "Update new SSID and Password". Connect your Android phones Wifi to GASOMETERs hotspot/access point. And goto
  the GASO METER App, to COnfig ESP tab. And then scan button once pressed will show all the available networks
  within GASO METERS reach. Select the required SSID and eneter your password. GASO METER will automatically connect
  to the given SSID and password.

  Before a new gas cylinder is kept on the scale, GASO METER needs to reset. And one should never
  reset the GASO METER with a gas cylinder on it. It will cause wrong readings.


  The GASO MTETER can be powered externally and has a built in LiPo battery which is used to power the ESP
  when external power is down. GASOMETER also has biult in smart charging to charge the battery when external power
  is up. Battery can be used to power the ESP upto one year without external power.

  Load Weight sensor (max100kg) together with HX711 ADC module has been used to measure the weight
  ESP8266 - 07 do all the necessary processes and connect with the WiFi to upload the data into the server
  Cloud from Adafruit (Adafruit.io) which is based on MQTT platform is used to upload the data from the system


  Created on 28 May 2017

  /******************************************************** Libraries** **********************************************************/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "HX711.h"
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"


#define calibration_factor    47565
#define config_pin            16        // System COnfiguration button
#define wifi_status           14        // indicator - blinks fast when if and only if gas is less than 5%, and external power is down
//           - blinks with a normal rate, if WiFi is not connected or when power is down
//           - always on when WiFi is connected
//           - blinks fast two times when in Config mode

HX711 scale(13, 12);                    // DOUT and SCK pins connected to 13 and 12 of ESP
LiquidCrystal_I2C lcd(0x3F, 16, 2);     // I@C address of LCD 0x3F, 16 colums x 2 rows LCD
ESP8266WebServer  server(80);           // define server with port 80


/******************************************************** Variables used *************************************************************/

// for weight sensor calculations
const float full_weight                      = 5;  // weight of the gas cylinder with gas in kg
const float empty_weight                     = 0;  // weight of the empty gas cylinder in kg
float       gas_weight;
float       current_weight;
float       current_gas_weight;
int         current_gas_percentage;
int         gas_percentage_compensation;
int         error                            = 0;

//flags used for the software
boolean     set_new_wifi_flag     = false;
boolean     wifi_dis_serial_flag  = true;
boolean     switch_flag           = false;
boolean     wifi_lcdLflag         = true;
boolean     hotspot_lcd_flag      = true;
boolean     error_comp_flag       = true;


//for lcd display and button
int         count = 0;
int         count_lcd = 0;
String      lcd_data = "";

/************************************************************ WiFi Access Point *************************************************************/

String WLAN_SSID;
String WLAN_PASS;

const char *ssid       = "WiFi_Gas_Scale";
const char *password   = "gg123";

/********************************************************* Adafruit.io Account Details ********************************************************/

#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "arty270"
#define AIO_KEY         "fbdadc26b3bf47c3bb9dd359d3ac1d5b"


/************************************************************ Adafruit.IO MQTT CLIENT **********************************************************/
//Connecting to MQTT Clinet (Adarfuit.io)
WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);


/********************************************************************* Feeds *******************************************************************/
// used to update feeds in Adafruit.io
Adafruit_MQTT_Publish gas_scale_percentage = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/gas_scale_percentage");
Adafruit_MQTT_Publish gas_scale_weight = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/gas_scale_weight");



void ssid_pw_read();
void wifi_scan();
void MQTT_WIFI_fail();

void setup() {
  pinMode(wifi_status, OUTPUT);          // indicator
  pinMode(config_pin, INPUT);            // pin used to send the system to WiFI-cofig mode


  Wire.begin(4, 5);                      // I2C enabled to interface with LCD display
  delay(10);
  Serial.begin(115200);                  // Enables Serial communication with PC
  delay(10);

  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing..!");
  lcd.setCursor(3, 1);
  lcd.print("Please Wait");
  delay(5000);

  WiFi.mode(WIFI_STA);                   // Wifi Mode is set to station, so that it can be connected to the router
  WiFi.disconnect();
  delay(100);

  EEPROM.begin(512);                                            // WiFi SSID and Password are loaded from EEPROM.. Powering down the system by any means will not erase the WiFi details
  Serial.println("Reading EEPROM ssid");                        // In such case powering back the system will load the SSID and password stored in the system
  for (int i = 0; i < 32; ++i)
  {
    WLAN_SSID += char(EEPROM.read(i));
  }
  Serial.print("SSID: ");
  Serial.println(WLAN_SSID.c_str());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting to");
  lcd.setCursor(0, 1);
  lcd.print(WLAN_SSID.c_str());
  delay(5000);

  Serial.println("Reading EEPROM pass");                        // Password read from EEPROM
  String epass = "";
  for (int i = 32; i < 96; ++i)
  {
    WLAN_PASS += char(EEPROM.read(i));
  }
  Serial.print("PASS: ");
  Serial.println(WLAN_PASS);

  // Connect to WiFi access point.
  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WLAN_SSID);

  WiFi.begin(WLAN_SSID.c_str(),  WLAN_PASS.c_str());
  delay(50);
  Serial.println();

  Serial.println("WiFi connected");
  Serial.println("IP address: "); Serial.println(WiFi.localIP());

  scale.set_scale(calibration_factor);                        // 47565 is the vlaue used here. It is found by an keeping a 20kg weight and get the return value from the module to be 20kg. Callibaration factor is adjusted in doing so
  scale.tare();                                               // Will tare the weight of the stand, and this is also why a battery is used to keep it not from resetting when a gas cylinder is kept on and if external power goes.
  gas_weight = full_weight - empty_weight;

  delay(100);
}


void loop() {

  MQTT_WIFI_fail();                                                   // will check if WiFi and MQTT is connected.. If not it will take necessary actions to connect

  current_weight = scale.get_units(10), 2;                            // current weight sensed by the scale (average of 10 readings with 2 decimal places)
  current_gas_weight = current_weight - empty_weight;
  current_gas_percentage = (current_gas_weight / gas_weight) * 100;

  if (current_gas_percentage > 100) {                                 // if the weight with full gas exceeds the weight defined in the code, it will give a >100%..
    current_gas_percentage = 100;                                     // this code will round it back to 100%
  }
  if (current_gas_percentage < 0) {                                   // if the weight with empty gas is lesser than the weight defined in the code, it will give a <0%..
    current_gas_percentage = 0;                                       // this code will round it back to 0%
  }

  gas_percentage_compensation = current_gas_percentage;               // for error compensation if external power goes, used in  MQTT_WIFI_fail();

  Serial.print("Current Weight: ");
  Serial.print(current_weight);
  Serial.print(" Kg");
  Serial.println("");
  Serial.print("Gas Percentage: ");
  Serial.print(current_gas_percentage);
  Serial.print("%");
  Serial.println("");

  gas_scale_percentage.publish(current_gas_percentage);               // publishes data to Adafruit.io

  lcd_data = current_gas_percentage;

  lcd.clear();
  lcd.setCursor( 3, 0);
  lcd.print("GASO METER");

  if ( lcd_data.length() == 1) {                 // the 3 if() condition are used to centre the values in the display
    lcd_data = current_gas_percentage;
    lcd_data += "%  FULL";
    lcd.setCursor( 4, 1);
    lcd.print(lcd_data);
  }

  if ( lcd_data.length() == 2) {
    lcd_data = current_gas_percentage;
    lcd_data += "% FULL";
    lcd.setCursor( 4, 1);
    lcd.print(lcd_data);
  }

  if ( lcd_data.length() == 3) {
    lcd_data = current_gas_percentage;
    lcd_data += "%  FULL";
    lcd.setCursor( 3, 1);
    lcd.print(lcd_data);
  }



  for (int i = 0; i < 1000; i++) {
    count = 0;
    while ((digitalRead(config_pin) == LOW) && (count < 40) && (switch_flag == false)) {            // Will send the system to Wi-Fi Configuration Mode
      count++;
      Serial.println(count);
      delay(50);

      if (count >= 40) {
        hotspot();
        break;
      }
    }
    delay(1);
  }
  switch_flag = false;
}


void hotspot() {                                                          // System will change from station mode to access point mode thus enabling other devices to connect to the ESP

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi CONFIG MODE");

  Serial.println("Config Mode");
  delay(1000);
  Serial.print("Configuring access point...");
  WiFi.softAP(ssid, password);                                            // Access point mode is set
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.on("/login", ssid_pw_read);                                      // ip/login request from a device will make the ESP respond with ssid_pw_read()   ...used to update SSID and Password
  server.on("/scan", wifi_scan);                                          // ip/scan request from a device will make the ESP respond with wifi_scan         ...used to scan the WiFI networks in the ESP's range
  server.begin();
  Serial.println("HTTP server started");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Update SSID and");
  lcd.setCursor(6, 1);
  lcd.print("Password");

  while (1) {


    for (int i = 0; i < 2; i++) {                                          // indicator
      digitalWrite(wifi_status, HIGH);
      delay(75);
      digitalWrite(wifi_status, LOW);
      delay(75);
      digitalWrite(wifi_status, HIGH);
      delay(75);
    }

    server.handleClient();

    for (int i = 0; i < 925; i++) {                                         // will go back to loop(), in case the config button was mistakenly pressed
      delay(1);
      count = 0;
      while ((digitalRead(config_pin) == LOW) && (count < 40)) {
        count++;
        Serial.println(count);
        delay(50);

        if (count >= 40) {
          switch_flag = true;
          wifi_dis_serial_flag = true;
          return;
        }
      }
    }
    if (set_new_wifi_flag == true) {                                        // will be set true once return from ssid_pw_read(), and thus...
      Serial.println("Return");                                             // this will break the while(1) loop...and the system will go into the main loop...
      set_new_wifi_flag = false;                                            // trying to connect to the New WiFi
      return;
    }
  }
  return;
}




void wifi_scan() {                            // scand the netwroks and returns it to the device requested
  String ssid = "";
  Serial.print("Scan start ... ");
  int n = WiFi.scanNetworks();                // n is the count of networks within ESP's range
  Serial.print(n);
  Serial.println(" network(s) found");
  for (int i = 0; i < n; i++)                 // ESP will respond to the device with 201 code, and response content of the SSID's seperated by a comma
  { // SSID's with commas wont work therefore
    ssid += WiFi.SSID(i);                     // SSID's have to be seperated at each an every comma in the receiving device and displayed as an list or takein into appropriate use
    ssid += ",";
  }
  Serial.println();
  Serial.println(ssid);
  server.send(201, "text/plain", ssid);       // returns 201 code with response content
}


void ssid_pw_read() {                               // this handler will take care when new SSID and PASSword is received from the the connected device

  server.send(200);                                 // will send a 200 confiemation code to the device

  String user = server.arg("USERNAME");             // argument infront of USERNAME will be the SSID
  String pass = server.arg("PASSWORD");             // argument infront of the PASSWORD will be the password of the SSID network

  Serial.print ("user = ");
  Serial.println(user);
  Serial.print (" password = ");
  Serial.println(pass);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("New Login");
  lcd.setCursor(7, 1);
  lcd.print("Received");

  if (user.length() > 0 && pass.length() > 0) {
    Serial.println("clearing eeprom");
    for (int i = 0; i < 96; ++i) {
      EEPROM.write(i, 0);
    }
    Serial.println(user);
    Serial.println("");
    Serial.println(pass);
    Serial.println("");

    Serial.println("writing eeprom ssid:");
    for (int i = 0; i < user.length(); ++i)         // SSID will be written into EEPROM so that it will be stored there permanently until erased or overwriten
    {
      EEPROM.write(i, user[i]);
      Serial.print("Wrote: ");
      Serial.println(user[i]);
    }
    Serial.println("writing eeprom pass:");
    for (int i = 0; i < pass.length(); ++i)         // Password will be written into EEPROM so that it will be stored there permanently until erased or overwriten
    {
      EEPROM.write(32 + i, pass[i]);
      Serial.print("Wrote: ");
      Serial.println(pass[i]);
    }
    hotspot_lcd_flag = true;
    EEPROM.commit();                                // will save the changes made to the flash memory

  }

  delay(3000);
  WiFi.mode(WIFI_STA);                              // ESP will go back to station mode, exiting from Access point mode, connecting to the SSID and password received from the device.
  WiFi.disconnect();
  delay(100);
  WiFi.begin(user.c_str(),  pass.c_str());          // To connect to new WiFi network
  set_new_wifi_flag = true;
  Serial.println("Connecting to new Wifi");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting to");
  lcd.setCursor(0, 1);
  lcd.print(user);
  WLAN_SSID = user;
  delay(3000);
  return;
}




void MQTT_WIFI_fail() {                                                       // function will make the system connect to WiFi or MQTT, if not connected
  int8_t ret;

  if (analogRead(0) < 400) {                                                  // If there is no external power, the system will loop in the following while()
    Serial.println("Entering to Sleep Mode");                                 // Before it loops till power comes back again, WiFi and scale sensor will be disabled till power comes back again
    digitalWrite(wifi_status, LOW);                                           // Normally ESP will take upto 350-800mA, in sleep mode it will take only 15mA increasing the battery life of the system
    scale.power_down();                                                       // Also the scale is power down which will only consume, < 1uA as per the datasheet
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(1);
    int hx711_sleep_count = 6000;
    int delay_led = 1;


    while (1) {
      delay(1);
      digitalWrite(wifi_status, !digitalRead(wifi_status));
      delay(delay_led);                                                       // delay will be set by the below code..if the gas remaining % gets lower than 5% when there is no current.
      // the blue led will blink fast..and slowly when there is no power but gas % is greater than 5%
      hx711_sleep_count++;
      delay(1);
      
      if (hx711_sleep_count > 6000) {                                          // put 15 to test
        hx711_sleep_count = 0;
        scale.power_up();                                                      // power up the scale to take the readings
        delay(50);
        current_weight = scale.get_units(10), 2;                               // readings are taken every 5 mins from the scale
        current_gas_weight = current_weight - empty_weight;
        current_gas_percentage = (current_gas_weight / gas_weight) * 100;
        scale.power_down();                                                    // power down once the readings are taken
        if (error_comp_flag == true) {
          delay(3000);                                                          // error callibaration of the sensor, caused by the voltage drop of the battery..
          error =  gas_percentage_compensation - current_gas_percentage + 6;    // error to be calculated once when it switches to baterry..thus flags are used
          error_comp_flag = false;
        }

        current_gas_percentage = current_gas_percentage + error;                // error compensation

        Serial.print("gas % :");
        Serial.println(current_gas_percentage);
        if (current_gas_percentage < 5)delay_led = 150;                         // sets the timing of the led as explained above..will blink quickly if gas remaining is <5
        if (current_gas_percentage >= 5)delay_led = 500;
      }

      if (analogRead(0) > 400) {                                              // if external power is back on..ADC pin of ESP will return a value greater than 400, breaking the while(1) loop
        Serial.println("Wake Up");                                            // on doing so, WiFI modem is enabled again and LCD is iniitialied back
        delay(100);
        lcd.begin();
        lcd.backlight();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Please Wait...");
        WiFi.forceSleepWake();
        delay(1);
        WiFi.mode(WIFI_STA);
        WiFi.begin(WLAN_SSID.c_str(),  WLAN_PASS.c_str());
        boolean error_comp_flag = true;
        scale.power_up();
        break;
      }
    }
  }

  if (mqtt.connected()) {                // returns if already connected
    digitalWrite(wifi_status, HIGH);     // indicator
    return;
  }


  while (( mqtt.connect()) != 0) {       // connect will return 0 if connected to MQTT
    digitalWrite(wifi_status, LOW);      //indicator

    while (WiFi.status() != WL_CONNECTED) {     // returns "WL_CONNECTED" if connected to Wi-Fi

      if (wifi_dis_serial_flag == true) {       // to Serial print once
        Serial.println("WiFi disconnected");
        wifi_dis_serial_flag = false;
      }

      count_lcd++;                              // start
      if (count_lcd > 2) {                      // to switch the LCD text... the flag is inverted
        if (wifi_lcdLflag == true) {            // in each if(), so that in next instance
          lcd.clear();                          // it will goto the other if()..and so on
          lcd.setCursor(0, 0);
          lcd.print("WiFi Failed..");
          wifi_lcdLflag = false;

        }


        else if (wifi_lcdLflag == false) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Connecting to..");
          lcd.setCursor(0, 1);
          lcd.print(WLAN_SSID.c_str());
          wifi_lcdLflag = true;
        }
        count_lcd = 0;

      }                                          //end

      digitalWrite(wifi_status, !digitalRead(wifi_status));            // indicator turn on and off
      for (int j = 0; j < 100; j++) {

        while ((digitalRead(config_pin) == LOW) && (count < 40)) {     // Sends the system to Config mode if config button (connected to config_pin) is pressed (Grounded).
          count++;                                                     // count * delay = 40 * 10ms = 400ms is the time the button should be pressed for the system togo into config mode.
          Serial.println(count);
          delay(50);

          if (count >= 40) {
            hotspot();
            break;
          }
        }
        delay(10);
      }
    }
    wifi_dis_serial_flag = true;                                          // Serial print once flag set back to true again..

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Server Failed...");
    lcd.setCursor(0, 1);
    lcd.print("Retrying in 2s..");

    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Retrying MQTT connection in 5 seconds...");
    mqtt.disconnect();
  }                                                                       // will exit this loop if MQTT is connected

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Server Connected");
  Serial.println("MQTT Connected..");
  delay(1000);
  return;
}

