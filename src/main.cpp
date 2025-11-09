//This code configures the Raspberry Pico W into Soft Access Point mode 
//and will act as a web server for all the connecting devices. 
//The application will turn ON and OFF four different colours of the RGB LEDs
//according to commands from the clients.
#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include "Adafruit_NeoPixel.h"           //include the RGB library
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "BH1745NUC.h"          //light measurement sensor library
#include <PDM.h>
#include <WebServer.h>
#include <ctime>
#include "time.h"
#include "Adafruit_MAX1704X.h" //fuel gauge library
#include "bme68xLibrary.h"         //This library is not available in PlatformIO
                                   //Library added to lib folder on the left
//RGB LED declarations ----------------
#define PIN_WS2812B  6  // The Pico pin that connects to WS2812B
#define NUM_PIXELS   3  // The number of LEDs (pixels) on WS2812B
#define DELAY_INTERVAL 500

Adafruit_NeoPixel WS2812B(NUM_PIXELS, PIN_WS2812B, NEO_GRB + NEO_KHZ800);
#define NEW_GAS_MEAS (BME68X_GASM_VALID_MSK | BME68X_HEAT_STAB_MSK | BME68X_NEW_DATA_MSK)
#define BH1745NUC_DEVICE_ADDRESS_38 0x38    //light measurement sensor I2C address
#define BIT_PER_SAMPLE 16
BH1745NUC bh1745nuc = BH1745NUC();
void readLightMeasurements();
Adafruit_MAX17048 maxlipo;   //creates fuel gauge object
//define OLED screen dimensions -----------
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # 
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
// output channels - SOUND
static const char channels = 1;
static const int frequency = 16000; //sample frequency
short sampleBuffer[256];
volatile int samplesRead;
int recordingStatus = 0; //0 = not recording, 1 = recording
PDMClass PDM(2,3);
//create OLED display object "display" ----------
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Bme68x bme;  // climate sensor variable
float battPercentage;
int climateDelay = 2000; // in MS
unsigned long previousMillis = 0;
unsigned long lastUpdateMillis =0;
unsigned long dataTimeOut = 120000;
unsigned long climateLastUpdate = 0;
unsigned long batteryLastUpdate = 0;
unsigned long rgbcLastUpdate = 0;
unsigned long noiseLastUpdate = 0;
//-------Web server parameters ----------

struct DeviceConfig {
  int deviceNumber;
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  String zone;
  String ssid;
  String password;
};
DeviceConfig config;

bool readConfigFromSD(DeviceConfig &cfg) {
  File configFile = SD.open("/config.txt");
  if (!configFile) {
    Serial.println("config.txt missing or unavalible");
    return false;
  }

  while (configFile.available()) {
    String line = configFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("#")) continue;

    int sepIndex = line.indexOf('=');
    if (sepIndex == -1) continue;

    String key = line.substring(0, sepIndex);
    String value = line.substring(sepIndex + 1);
    key.trim();
    value.trim();

    if (key == "DEVICE_NUMBER") cfg.deviceNumber = value.toInt();
    else if (key == "IP") cfg.ip.fromString(value);
    else if (key == "GATEWAY") cfg.gateway.fromString(value);
    else if (key == "SUBNET") cfg.subnet.fromString(value);
    else if (key == "ZONE") cfg.zone = value;
    else if (key == "SSID") cfg.ssid = value;
    else if (key == "PASSWORD") cfg.password = value;
  }

  configFile.close();
  return true;
}

//specifies the SSID and Password of the soft Access Point
const char* ap_ssid = "GowersSmall";           //sets soft Access Point SSID
const char* ap_password= "mattyisalegend";    //sets access Point Password
// MOBILE
/* const char* ap_ssid = "Harvey's iPhone";
const char* ap_password= "harvey123"; */
uint8_t max_connections=8;               //Sets maximum Connection Limit for AP
int current_stations=0, new_stations=0;  //variables to hold the number of connected clients

//IPAddress local_IP(10, 45, 1, 14);      //set your desired static IP address (i.e. vary the last digit)
//IPAddress gateway(10, 45, 1, 1);
IPAddress local_IP(10, 45, 1, 13);
IPAddress gateway(10, 45, 1, 1);
IPAddress subnet(255, 255, 255, 0);  
/* IPAddress local_IP(172,20,10,6);
IPAddress gateway(172,20,10,1);
IPAddress subnet(255,255,255,240); */
IPAddress IP;

//specifies the Webserver instance to connect with at HTTP Port: 80
WebServer server(80);
//------------------------------------ 

//specifies the boolean variables indicating the status of red, green & blue LEDs
//specifies the current colour values of the LEDs
bool redLED_status=false, greenLED_status=false, blueLED_status=false;
int  redValue = 0, greenValue = 0, blueValue = 0;

//declares the functions implemented in the program
float readFuelGaugeMeasurement();
void handle_OnConnect();
void handle_redON();
bool isDataFresh();
void handle_redOFF();
void handle_greenON();
void handle_greenOFF();
void handle_blueON();
void handle_blueOFF();
void handle_NotFound();
void displayParameters();
void handle_getTemp();
void handle_getHumidity();
void handle_getPressure();
void handle_ClimateData();
void handle_getBattery();
void handle_lowPowerModeOn();
void handle_lowPowerModeOff();
void handle_getLowPower();
void handle_autoLowPowerModeOn();
void handle_autoLowPowerModeOff();
void handle_sanityCheck();
void handle_getRGBC();
void handle_GetStoredReadings();
void handle_ClearStoredReadings();
void addCORS();
void handle_getSoundLevel();
void getSoundSample();
void onPDMdata();
String HTML();
String temp =     "";
String humid =    "";
String pressure = "";
bool lowPowerMode = false;
bool autoLowPower = true;
String red="";
String green="";
String blue="";
String clear="";
String noiseLevel ="";
String zone = "Zone 1";
int device = 1;
//---------------------------------------------
/* struct tm getDateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    // Return an empty/default struct if time not set
    struct tm empty = {};
    return empty;
  }
  return timeinfo;
} */
void addCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
}
void setup() {
  //Start the serial communication channel
  Serial.begin(115200);
  Serial.println();
  display.setCursor(0,10);                 //Start at top-left corner (Col=0, Row =10)
  display.setTextSize(1);                 //Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);
  WiFi.disconnect(true);
  delay(300);
  //configTime(0, 0, "pool.ntp.org");    // UTC time
  //-----------
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
    WS2812B.begin();
    WS2812B.show();
  // the library initializes this with an Adafruit splash screen.
  display.display();  //this function is required to display image
  delay(1000); // Pause for 2 seconds

  //configure Pico WiFi
  //WiFi.mode(WIFI_AP);                            //configures Pico WiFi as soft Access Point
  if (!SD.begin(SS)) {
    Serial.println("Card failed, or not present");
    // don't do anything more:
  }

if (readConfigFromSD(config)) { //read the config file
  Serial.println("config.txt loaded: ");
  Serial.print("IP: "); Serial.println(config.ip);
  Serial.print("Gateway: "); Serial.println(config.gateway);
  Serial.print("Subnet: "); Serial.println(config.subnet);
  Serial.print("Zone: "); Serial.println(config.zone);
  Serial.print("SSID: "); Serial.println(config.ssid);
  Serial.print("Device Number: "); Serial.println(config.deviceNumber);
  zone = config.zone;
  device = config.deviceNumber;
}
else
{
  Serial.println("failed to load config"); //default values
  config.deviceNumber = 1;
  config.ip = IPAddress(10, 45, 1, 14);
  config.gateway = IPAddress(10, 45, 1, 1);
  config.subnet = IPAddress(255, 255, 255, 0);
  config.zone = zone;
  config.ssid = ap_ssid;
  config.password = ap_password;
}

  //WiFi.softAPConfig(local_IP, gateway, subnet);  //configures static IP for the soft AP
  WiFi.mode(WIFI_STA);
  //WiFi.config(local_IP,gateway, subnet);
  WiFi.config(config.ip, config.gateway, config.subnet);
  WiFi.setHostname("PicoW"); // sets the device hostname
  //WiFi.begin(ap_ssid,ap_password);
  WiFi.begin(config.ssid.c_str(), config.password.c_str()); //configure the wifi using the config
  display.clearDisplay();
  display.setCursor(0,10);                 //Start at top-left corner (Col=0, Row =10)
  display.setTextSize(1);                 //Normal 1:1 pixel scale
  while(WiFi.status()!=WL_CONNECTED)
  {
    Serial.println("Attempting to connect to Wifi");
    display.clearDisplay();
    display.println("Attempting to connect to Wifi");
    display.display();
    delay(100);
  }

  	bme.begin(0x76, Wire);
	if(bme.checkStatus()){
		if (bme.checkStatus() == BME68X_ERROR){
			Serial.println("Sensor error:" + bme.statusString());
			return;
		}
		else if (bme.checkStatus() == BME68X_WARNING){
			Serial.println("Sensor Warning:" + bme.statusString());
		}
	}
  bme.setTPH();
  uint16_t tempProf[10] = { 100, 200, 320 }; // set temp in degree Celsius
  uint16_t durProf[10] = { 150, 150, 150 }; // set dur to milliseconds

  bme.setSeqSleep(BME68X_ODR_250_MS);
	bme.setHeaterProf(tempProf, durProf, 3);
	bme.setOpMode(BME68X_SEQUENTIAL_MODE);
  //Sound
  PDM.onReceive(onPDMdata);
  if (!PDM.begin(channels, frequency)) {
    Serial.println("Failed to start PDM!");
    while (1);
  }
  PDM.setGain(30);
  /*//Setting the AP Mode with SSID, Password, and Max Connection Limit
  if(WiFi.softAP(ap_ssid,ap_password,1,false,max_connections)==true){
    Serial.print("Access Point is Created with SSID: ");
    Serial.println(ap_ssid);
    Serial.print("Max Connections Allowed: ");
    Serial.println(max_connections);
    Serial.print("Access Point IP: ");
    Serial.println(WiFi.softAPIP());
  }
  else{
    Serial.println("Unable to Create Access Point");
  }*/
  Serial.println("Connected to Wifi");
  display.clearDisplay();
  display.println("Connected to Wifi");
  display.println("IP Address:");
  display.println(WiFi.localIP());
  Serial.println("IP Address: ");
  Serial.println(WiFi.localIP());
  display.display();
  delay(5000);
  display.clearDisplay();
  display.println(WiFi.status());
  delay(2000);
  Serial.print("Device IP: ");
  Serial.println(WiFi.localIP());
  while(!maxlipo.begin()){
    Serial.println("Could not find a MAX1704X sensor");
    delay(2000);
  }
  Serial.print(F("Fuel Gauge sensor found!"));
  //Specifying the functions which will be executed upon corresponding GET request from the client
  server.on("/",handle_OnConnect);
  server.on("/redON",handle_redON);
  server.on("/redOFF",handle_redOFF);
  server.on("/greenON",handle_greenON);
  server.on("/greenOFF",handle_greenOFF);
  server.on("/sanityCheck",handle_sanityCheck);
  server.on("/blueON",handle_blueON);
  server.on("/blueOFF",handle_blueOFF);
  server.on("/getClimateData",handle_ClimateData);
  server.on("/getTemp",handle_getTemp);
  server.on("/getHumidity",handle_getHumidity);
  server.on("/getPressure",handle_getPressure);
  server.on("/getBattery",handle_getBattery);
  server.on("/lowPowerModeOn", handle_lowPowerModeOn);
  server.on("/lowPowerModeOff", handle_lowPowerModeOff);
  server.on("/getLowPower", handle_getLowPower);
  server.on("/autoLowPowerModeOff", handle_autoLowPowerModeOff);
  server.on("/autoLowPowerModeOn", handle_autoLowPowerModeOn);
  server.on("/getRGBC",handle_getRGBC);
  server.on("/getSoundLevel",handle_getSoundLevel);
  server.on("/getStoredReadings", handle_GetStoredReadings);
  server.on("/clearStoredReadings", handle_ClearStoredReadings);
  server.onNotFound(handle_NotFound);
  display.display();
  //Starting the Server
  server.begin();
  Serial.println("HTTP Server Started");
  display.clearDisplay();
display.println("HTTP Server Started");
display.print("IP: ");
display.println(WiFi.localIP());
display.display();
bh1745nuc.begin(BH1745NUC_DEVICE_ADDRESS_38);
bh1745nuc.startMeasurement();
delay(3000);
}

void fetchClimateData();
void sendCORSHeaders();
void saveToSD();
void sendCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void saveToSD(String data){
  File dataFile = SD.open("readings.txt",FILE_WRITE);

  if(dataFile)
  {
    
    dataFile.println(data);
    dataFile.close();
    Serial.println("Written to SD: "+data);
    
  }
  else{
    Serial.println("Error opening readings.txt");
  }
}
//Callback function to process the data from the PDM microphone.
//NOTE: This callback is executed as part of an ISR.
//Therefore using `Serial` to print messages inside this function isn't supported.
void onPDMdata() {
  // Query the number of available bytes
  int bytesAvailable = PDM.available();

  // Read into the sample buffer
  PDM.read(sampleBuffer, bytesAvailable);
  // 16-bit, 2 bytes per sample
  samplesRead = bytesAvailable / 2;
}
uint32_t lastSoundSend = 0;
const uint32_t SOUND_INTERVAL_MS = 1000;
long soundSum = 0;
int soundCount =0;
void getSoundSample() {

  //wait for audio samples to be read
  
  if (samplesRead>0) {
    int n = samplesRead;
    samplesRead=0;
      for (int i=0; i<n; i++) {
        //client.print("Noise Level: " + String(sampleBuffer[i]) + '\n');
        //delay(350);
        soundSum += abs(sampleBuffer[i]);
        soundCount++;
        /*Serial.println(sampleBuffer[i]);
        display.clearDisplay();               //clears OLED screen
        display.setCursor(0,30);   
            //display sound value, higher value = louder
                                              //can you visualize this as a graph?
        display.display();                    //needs to be invoked for the display to work
        */
      }
      uint32_t now = millis();
      if (now - lastSoundSend >= SOUND_INTERVAL_MS && soundCount > 0) {
        int avgSound = soundSum/soundCount; // avg
        Serial.println("Noise Level: "+String(avgSound));
        noiseLevel = String(avgSound);
        delay(300);
        soundSum = 0;
        soundCount=0;
        lastSoundSend=now;
      }  
  }
}


void fetchClimateData()
{
  
  bme68xData data;
  uint8_t nFieldsLeft = 0;
  delay(200);
  climateLastUpdate = millis();
  if(bme.fetchData()){
    do{
      nFieldsLeft = bme.getData(data);
      temp =  String(data.temperature-4.49);
      humid = String(data.humidity);
      pressure =  String(data.pressure);
      if(data.gas_index == 2) /* Sequential mode sleeps after this measurement */
          delay(250);
    }while(nFieldsLeft);
  }
}
/* void fetchClimateData()
{
  
  bme68xData data;
  uint8_t nFieldsLeft = 0;
  delay(200);
  climateLastUpdate = millis();
  if(bme.fetchData()){
    do{
      nFieldsLeft = bme.getData(data);
      temp =  String(data.temperature-4.49);
      humid = String(data.humidity);
      pressure =  String(data.pressure);
      //if(data.gas_index == 2) Sequential mode sleeps after this measurement
          delay(250);
    }while(nFieldsLeft);
  }
} */

void readLightMeasurements() {
  if(!bh1745nuc.read()) {
    Serial.println("Failed to read light data from sensor!");
    delay(500);
    return;
  }
  unsigned short rgbc[4];
  rgbc[0] = bh1745nuc.red;
  red = (String(rgbc[0]));
  rgbc[1] = bh1745nuc.green;
  green = (String(rgbc[1]));
  rgbc[2] = bh1745nuc.blue;
  blue = (String(rgbc[2]));
  rgbc[3] = bh1745nuc.clear;
  clear = (String(rgbc[3]));
  Serial.print(String("R: ")+(String(rgbc[0]))+",G"+(String(rgbc[1]))+",B"+(String(rgbc[2]))+",C"+(String(rgbc[3]))+"\n");
  
} 
void loop() {
  //Assign the server to handle the clients
  server.handleClient();
  //delay(1000);
  //readFuelGaugeMeasurement();
  
  //displayParameters();
  unsigned long currentMillis = millis();
  if(currentMillis - previousMillis >= climateDelay) {
    fetchClimateData();
    readLightMeasurements();
    getSoundSample();
    previousMillis = currentMillis;
  }
  

  display.clearDisplay();
  display.println("Make a choice on light");
  display.display();
  //changes the three RGB LEDs On and OFF according client commands
  //Red LED
  if(redLED_status==false) {
    redValue = 0;
  }
  else {
    redValue = 255;
  }
  //Greed LED
  if(greenLED_status==false) {
    greenValue = 0;
  }
  else {
    greenValue = 255;
  }
  //Blue LED
  if(blueLED_status==false) {
    blueValue = 0;
  }
  else {
    blueValue = 255;
  }

  // turn first pixel to red one 
  WS2812B.clear(); // set all pixel colors to 'off'. It only takes effect if pixels.show() is called
  
  //sets 1st LED
  WS2812B.setPixelColor(0, WS2812B.Color(redValue, 0, 0)); // it only takes effect if pixels.show() is called
  
  //sets 2nd LED (middle)
  WS2812B.setPixelColor(1, WS2812B.Color(0, greenValue, 0)); 

  //sets 3rd LED
  WS2812B.setPixelColor(2, WS2812B.Color(0, 0, blueValue)); 
  
  WS2812B.show();   // send the updated pixel colors to the WS2812B hardware.
}
void handle_OnConnect(){
  Serial.println("Client Connected");
  server.send(200, "text/html","OK"); 
}

void handle_GetStoredReadings(){
  addCORS();
  File file = SD.open("readings.txt");
  if(!file)
  {
    server.send(404,"text/plain","No Stored Readings Found");
    Serial.println("No Readings found on SD");
    return;
  }
  String response = "";
  while(file.available()){
    response+= file.readStringUntil('\n');
    response +="\n";
    if(response.length()>2000){
      server.sendContent(response); // chunk limit
      response="";
    }
  }
  file.close();
  if(response.length()>0)
  {
    server.send(200,"text/plain",response);
  }
  else{
    server.send(200,"text/plain","");
  }
  Serial.println("Sent stored readings to base station");
  Serial.println("Clearing stored Readings");
}

void handle_ClearStoredReadings() {
  if (SD.exists("/readings.txt")) {
    SD.remove("/readings.txt");
    server.send(200, "text/plain", "Cleared stored readings.");
    Serial.println("Cleared readings.txt.");
  } else {
    server.send(404, "text/plain", "No readings.txt to clear.");
  }
}
void handle_redON(){
  Serial.println("RED ON");
  addCORS();
  redLED_status=true;
  server.send(200, "text/html","OK");
}
 
void handle_redOFF()
{
  addCORS();
  Serial.println("RED OFF");
  redLED_status=false;
  server.send(200, "text/html","OK");
}
void handle_getSoundLevel(){
  addCORS();
  delay(200);
  server.send(200, "text/plain", noiseLevel);
}
void handle_ClimateData(){
  addCORS();
  delay(200);
  String climateData[5];
  //readLightMeasurements();
  //getSoundSample();
  //Get current Unix timestamp
  std::time_t now = std::time(nullptr);
  String unix_time = String((long)now);   // convert to String

  String combinedData = zone + "," + temp + "," + noiseLevel + "," + clear + "," + unix_time;
  saveToSD(combinedData); // SAVES DATA TO SD
  delay(200);

  server.send(200, "text/plain", combinedData);
}
void handle_getBattery(){
  addCORS();
  delay(100);
  float battPercent = readFuelGaugeMeasurement();
  if(isnan(battPercent)){
    Serial.println("Failed to read battery percentage");
    server.send(500,"text/plain","Error");
    return;
  }
  //Get current Unix timestamp
  std::time_t now = std::time(nullptr);
  String unix_time = String((long)now);   // convert to String
  String combinedBatteryData = zone + "," + String(battPercentage) + "," + unix_time;
  server.send(200,"text/plain",combinedBatteryData); // read with a % other side
}
bool isDataFresh(){
  if((millis()-climateLastUpdate)<dataTimeOut)
  {
    return true;
  }
  return false;
}
void handle_sanityCheck(){
  addCORS();
  delay(200);
  if(isDataFresh()){
    server.send(200, "text/plain","Data OK");
    return;
  }
  else{
    server.send(500, "text/plain","Data Stale");
    return;
  }

  
}
void handle_getRGBC(){
  addCORS();
  delay(200);
  String rgbcData[4];
  rgbcData[0]=red;
  rgbcData[1]=green; 
  rgbcData[2]=blue;
  rgbcData[3]=clear;
  String combinedData = rgbcData[0] + "," + rgbcData[1] + "," + rgbcData[2] + "," + rgbcData[3];
  server.send(200, "text/plain", combinedData);
}
void handle_getTemp(){
  //sendCORSHeaders();
  addCORS();
  server.send(200, "text/plain", temp);
}
void handle_getHumidity(){
  addCORS();
  server.send(200, "text/plain", humid);
}
void handle_getPressure(){
  addCORS();
  server.send(200, "text/plain", pressure);
}
void handle_greenON()
{
  addCORS();
  Serial.println("GREEN ON");
  greenLED_status=true;
  server.send(200, "text/html","OK");
}
 
void handle_greenOFF()
{
  addCORS();
  Serial.println("GREEN OFF");
  greenLED_status=false;
  server.send(200, "text/html","OK");
}

void handle_blueON()
{
  addCORS();
  Serial.println("BLUE ON");
  blueLED_status=true;
  server.send(200, "text/html","OK");
}
void handle_blueOFF(){
  addCORS();
  Serial.println("BLUE OFF");
  blueLED_status=false;
  server.send(200, "text/html","OK");
}

void handle_lowPowerModeOn(){
  lowPowerMode = true;
  blueLED_status = false;
  redLED_status = false;
  greenLED_status = false;
  display.ssd1306_command(SSD1306_DISPLAYOFF); //lights and display off
  climateDelay = 1800000; // half an hour
  Serial.println("Activated low power mode");
  server.send(200, "text/plain", "Activated low power mode");
}

void handle_lowPowerModeOff(){
  lowPowerMode = false;
  climateDelay = 2000;
  display.ssd1306_command(SSD1306_DISPLAYON);
  Serial.println("Deactivated low power mode");
  server.send(200, "text/plain", "Deactivated low power mode");
}

void handle_autoLowPowerModeOn(){
  autoLowPower = true;
  Serial.println("Activated automatic power saving");
  server.send(200, "text/plain", "Activated automatic power saving");
}

void handle_autoLowPowerModeOff(){
  autoLowPower = false;
  Serial.println("Disabled automatic power saving");
  server.send(200, "text/plain", "Disabled automatic power saving");
}

void handle_getLowPower(){
  addCORS();
  if (lowPowerMode)
  {
    server.send(200, "text/plain", "true");
  }
  else{
    server.send(200, "text/plain", "false");
  }
}

void handle_NotFound() {
   server.send(404, "text/plain", "Not found");
}


float readFuelGaugeMeasurement(){
  float cellVoltage = maxlipo.cellVoltage();  //reads cell voltage
  if (isnan(cellVoltage)) {
    Serial.println("Failed to read cell voltage, check battery is connected!");
    delay(1000);
    return NAN;
  }
  
  float battPercent = (cellVoltage/4.2)*100;     //converts cell voltage reading to proportion of 4.2v
  
  if(battPercent >=100.0)
    battPercent = 100;
  battPercentage = battPercent;
  display.setCursor(0,18);                  //Start at top-left corner (Col=0, Row=18)
  display.print(F("Voltage: "));
  display.print(String(cellVoltage, 2));    //converts integer to String before invoking display() function
                                            //a simple print with cursor spec will position value at the
                                            //end of previous cursor position

  display.setCursor(0,30);                  //Col=0,Row=30
  display.print(F("Proportion: "));

  display.print(String(battPercent, 1)); 
  display.print(F("%"));
  display.display(); 
  delay(1000);  // save energy, dont query too often!
  if (battPercent < 20 && autoLowPower && !lowPowerMode){ //low power mode on low charge
    handle_lowPowerModeOn();
  }
   // save energy, dont query too often!
  batteryLastUpdate = millis();
  return battPercent;
}
/*
void handle_OnConnect(){
  Serial.println("Client Connected");
  server.send(200, "text/html", HTML()); 
}
 
void handle_redON(){
  Serial.println("RED ON");
  redLED_status=true;
  server.send(200, "text/html", HTML());
}
 
void handle_redOFF()
{
  Serial.println("RED OFF");
  redLED_status=false;
  server.send(200, "text/html", HTML());
}
 
void handle_greenON()
{
  Serial.println("GREEN ON");
  greenLED_status=true;
  server.send(200, "text/html", HTML());
}
 
void handle_greenOFF()
{
  Serial.println("GREEN OFF");
  greenLED_status=false;
  server.send(200, "text/html", HTML());
}
 
void handle_blueON()
{
  Serial.println("BLUE ON");
  blueLED_status=true;
  server.send(200, "text/html", HTML());
}
 
void handle_blueOFF(){
  Serial.println("BLUE OFF");
  blueLED_status=false;
  server.send(200, "text/html", HTML());
}
*/

 
String HTML(){
  String msg="<!DOCTYPE html> <html>\n";
  msg+="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  msg+="<title>LED Control</title>\n";
  msg+="<style>html{font-family:Helvetica; display:inline-block; margin:0px auto; text-align:center;}\n";
  msg+="body{margin-top: 50px;} h1{color: #444444; margin: 50px auto 30px;} h3{color:#444444; margin-bottom: 50px;}\n";
  msg+=".button{display:block; width:80px; background-color:#f48100; border:none; color:white; padding: 13px 30px; text-decoration:none; font-size:25px; margin: 0px auto 35px; cursor:pointer; border-radius:4px;}\n";
  msg+=".button-on{background-color:#f48100;}\n";
  msg+=".button-on:active{background-color:#f48100;}\n";
  msg+=".button-off{background-color:#26282d;}\n";
  msg+=".button-off:active{background-color:#26282d;}\n";
  msg+="</style>\n";
  msg+="</head>\n";
  msg+="<body>\n";
  msg+="<h1>Raspberry Pi Pico Web Server</h1>\n";
  msg+="<h3>Using Access Point (AP) Mode</h3>\n";
  
  if(redLED_status==false)
  {
    msg+="<p>RED LED Status: OFF</p><a class=\"button button-on\" href=\"/redON\">ON</a>\n";    
  }
  else
  {
    msg+="<p>RED LED Status: ON</p><a class=\"button button-off\" href=\"/redOFF\">OFF</a>\n";
  }
 
  if(greenLED_status==false)
  {
    msg+="<p>GREEN LED Status: OFF</p><a class=\"button button-on\" href=\"/greenON\">ON</a>\n";    
  }
  else
  {
    msg+="<p>GREEN Status: ON</p><a class=\"button button-off\" href=\"/greenOFF\">OFF</a>\n";
  }
 
  if(blueLED_status==false)
  {
    msg+="<p>BLUE Status: OFF</p><a class=\"button button-on\" href=\"/blueON\">ON</a>\n";    
  }
  else
  {
    msg+="<p>BLUE Status: ON</p><a class=\"button button-off\" href=\"/blueOFF\">OFF</a>\n";
  }
 
  msg+="</body>\n";
  msg+="</html>\n";
  return msg;
}

//displays string s on OLED display
void displayParameters(){
  display.clearDisplay();
  display.setCursor(0,10);                 //Start at top-left corner (Col=0, Row =10)
  display.setTextSize(1);                 //Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);    //Draw white text 
  display.print(F("Total connections: "));
  display.println(current_stations);
  display.println();
  display.print(F("SoftAP: "));
  display.println(WiFi.softAPIP());
   
  display.display();
}