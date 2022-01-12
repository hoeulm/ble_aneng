// #################################################################################
// Program:  M5_StiP_BLE_UART_ANENG_WiFI.ino
// IDEBoard: M5StickC
// IDEConfig:NoOTA
// Version:  2022-01-07
// Hardware: M5-Stick C PLUS
// Extras:   ANENG V05B DMM mit Bluetooth Interface
//
//
//           +---------------------------+   Y -- Bluetooth -- Y   +---------------+
//           |ESP32 StickCPlus    LED(#) |   |                 |   |ANENG V05B  DMM|
//           |      +------------------+ o---+                 +---o +-----------+ |
//           |      | LCD 240x135      | |                         | |LCD Display| |
//           |BtnA  | 64k Farben       | |  Y--WiFi--Y             | |Monochrom  | |
//           |      |                  | |  |        |             | +-----------+ |
//           |      |                  | o--+  +---------------+   | [SEL]   [H/Z] |
//           |      +------------------+ |     | Browser       |   |      (o)      |
//           |                 BtnB      |     |http://v05b.dmm|   |     [ >B]     |
//           +---------------------------+     +---------------+   |     [ U ]     |
//                                                                 | (O)       (O) |
//                                                                 |      (O)      |
//                                                                 +---------------+
// #################################################################################

#include <M5StickCPlus.h>
#include <BLEDevice.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
// #include <DNSServer.h>

#include "html.h"

#include "dmm.h"
#define UUID_SERV    "0000fff0-0000-1000-8000-00805f9b34fb"  // The remote service UUID we wish to connect to.
#define UUID_CHAR_RX "0000fff3-0000-1000-8000-00805f9b34fb"  // RX Characteristic
#define UUID_CHAR_TX "0000fff4-0000-1000-8000-00805f9b34fb"  // TX Characteristic

static  BLEUUID serviceUUID(UUID_SERV);
static  BLEUUID charUUID_RX(UUID_CHAR_RX);
static  BLEUUID charUUID_TX(UUID_CHAR_TX);

static  BLEAddress *pServerAddress;
static  boolean b_doConnect = false;
static  boolean b_connected = false;
static  BLERemoteCharacteristic* pTXCharacteristic;
static  BLERemoteCharacteristic* pRXCharacteristic;

#define pin_M5_LED 10

const    uint8_t notificationOff[] = {0x0, 0x0};
const    uint8_t notificationOn[] = {0x1, 0x0};
bool     b_onoff = true;
bool     b_debug = false;

#define   WiFi_SSID_AP  "dmm-aneng"   // <== CHANGE THIS !!
#define   WiFi_PASS_AP  ""            // <== CHANGE THIS !!
#define   WiFi_HOSTNAME "anengv05b.dmm"

IPAddress ip_AP(192,168, 32,1);
IPAddress ip_GW(192,168, 32,1);
IPAddress ip_SM(255,255,255,0);

#define PORT_SOCK      7044            // Port for WebSocketsServer (=1B84h)
WebServer        serv_HTTP(80);
WebSocketsServer serv_SOCK(PORT_SOCK); // create a serv_SOCK serv_HTTP on defined Port

//DNSServer        serv_DNS;
//#define PORT_DNS         53
String  s_IP="";
long    l_lastchk,l_lastchg;           // Merker fuer last CHECK bzw last CHANGE

void setup_WiFi_AP()
{
	WiFi.disconnect(); delay(100); WiFi.mode(WIFI_AP);
  Serial.printf("Creating AP-WiFi-SSID='%s' Pass='%s' for Subnet %d.%d.%d.x\r\n",WiFi_SSID_AP,WiFi_PASS_AP,ip_AP[0],ip_AP[1],ip_AP[2]);
  WiFi.setHostname(WiFi_HOSTNAME); // muss vor WiFi.begin gesetzt werden!!
	WiFi.softAP(WiFi_SSID_AP,WiFi_PASS_AP);  delay(100); // without delay sometimes 192.168.4.1 !!!
	WiFi.softAPConfig(ip_AP,ip_GW,ip_SM); // muss NACH WiFi.begin gesetzt werden! Zuerst IPConfig dann Name!!
  s_IP=WiFi.softAPIP().toString(); 	Serial.printf("IP(Accesspoint)=%s\r\n",s_IP.c_str());
}

void setup_serv_SOCK()                 // Start a serv_SOCK
{
	serv_SOCK.begin();                 // start the serv_SOCK serv_HTTP
	serv_SOCK.onEvent(serv_SOCKEvent); // if there's an incomming serv_SOCK message, go to function 'serv_SOCKEvent'
	Serial.println("serv_SOCK started.");
}

void serv_SOCKEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght)  // When a serv_SOCK message is received
{
	switch (type)
	{
		case WStype_DISCONNECTED: { Serial.printf("[%u] Disconnected!\n", num);      break;}
		case WStype_CONNECTED:    { // if a new serv_SOCK connection is established
		                            IPAddress ip = serv_SOCK.remoteIP(num);
		                            Serial.printf("[%u] Socket-Connectd from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
		                          }
		                          break;
		case WStype_TEXT:         // if new text data is received
		                          Serial.printf("[%u] get Text: %s\r\n", num, payload);
		                          break;
	}
}
void serv_HTML()  { String s_html=s_html_template;  s_html.replace("%ip%",s_IP);  s_html.replace("%port%",String(PORT_SOCK));  serv_HTTP.send(200,"text/html",s_html);}
void serv_JSFUNC(){ String s_js=s_jsfunc;  s_js.replace("%ip%",s_IP);  s_js.replace("%port%",String(PORT_SOCK));  serv_HTTP.send(200,"text/javascript",s_js);}
void serv_FAVICO(){ serv_HTTP.send(200,"image/svg+xml",s_favicon); }
void serv_CSS()   { serv_HTTP.send(200,"text/css",s_css);}
void serv_CLIENT(){ String s=s_wsclt;  s.replace("%ip%",s_IP);  s.replace("%port%",String(PORT_SOCK));  serv_HTTP.send(200,"text/html",s);}

void setup_serv_HTTP()
{
  serv_HTTP.on("/",              serv_HTML);
  serv_HTTP.on("/myfunc.js",     serv_JSFUNC);
  serv_HTTP.on("/favicon.ico",   serv_FAVICO);
  serv_HTTP.on("/_css/style.css",serv_CSS);
  serv_HTTP.on("/ws",            serv_CLIENT);
  serv_HTTP.begin();
}

static void notifyCallback( BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)
{
	s_DmmPre=""; s_DmmVal=""; s_DmmUnit=""; s_DmmOpt=""; String s_Recv="";
	for(int i=0;i<length;i++){ai_DMM[i%10]=msb(pData[i]^ai_XOR[i%10]); s_Recv+=String(pData[i],HEX)+" ";}
	for(int i=0;i<4;i++){ai_Segm[i]=16*(ai_DMM[i+3]%16)+ai_DMM[i+4]/16; seg7_to_dec(ai_Segm[i]);}
	decode_flags();
	Serial.printf("%5d.%03d;",millis()/1000,millis()%1000);
	if(b_debug)
	{
		Serial.print("Notify callback for TX characteristic received.");
		Serial.print("\r\nRECV   =");  for(int i=0;i<length;i++){ Serial.printf("%02X  ",pData[i]); }
		Serial.print("\r\nXORKEY =");  for(int i=0;i<length;i++){ Serial.printf("%02X  ",ai_XOR[i%10]);}
		Serial.print("\r\nDATA=");     for(int i=0;i<length;i++){ Serial.printf("%02X  ",pData[i]^ai_XOR[i%10]);}
		Serial.print("\r\nMSB =");     for(int i=0;i<length;i++){ Serial.printf("%02X  ",ai_DMM[i%10]);}
		Serial.print("\r\nSEG7=              ");for(int i=0;i<4;i++){Serial.printf("%02X  ",ai_Segm[i]);}
		Serial.print("\r\nSegVal=                  ");
	}
	Serial.print(s_DmmVal.c_str()); Serial.print(";"+s_DmmPre+s_DmmUnit+"["+s_DmmOpt+"]");
	Serial.print("Send via SOCK:"); Serial.println(s_Recv);
  //if(s_DmmVal!=s_DmmValOld) 
  //{
    M5.Lcd.setTextColor(WHITE);M5.Lcd.drawRightString(s_DmmValOld,180,48,7);
    M5.Lcd.setTextColor(BLACK);M5.Lcd.drawRightString(s_DmmVal   ,180,48,7);
    M5.Lcd.drawString(s_DmmUnit,180,72,4);
    M5.Lcd.drawString(s_DmmPre ,180,48,4);
    s_DmmValOld=s_DmmVal;
	  serv_SOCK.sendTXT(0,s_Recv);
    l_lastchg=millis();
  //}
}

bool connectToServer(BLEAddress pAddress)
{
	Serial.print("Establishing a connection to device address: ");  Serial.println(pAddress.toString().c_str());
	BLEClient*  pClient  = BLEDevice::createClient();  Serial.println("  - Created client");
//	Connect to the remove BLE Server.
	pClient->connect(pAddress);  Serial.println("  - Connected to server");

//	Obtain a reference to the Nordic UART service on the remote BLE server.
	BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
	if (pRemoteService == nullptr)
	{
		Serial.print("Failed to find UART service UUID: ");
		Serial.println(serviceUUID.toString().c_str());
		return false;
	}
	Serial.println("  - Remote BLE service reference established");
//	Obtain a reference to the TX characteristic of the Nordic UART service on the remote BLE server.
	pTXCharacteristic = pRemoteService->getCharacteristic(charUUID_TX);
	if (pTXCharacteristic == nullptr)
	{
		Serial.print("Failed to find TX characteristic UUID: ");
		Serial.println(charUUID_TX.toString().c_str());
		return false;
	}
	Serial.println("  - Remote BLE TX characteristic reference established");
	// Read the value of the TX characteristic.
	std::string value = pTXCharacteristic->readValue();
	Serial.print("The characteristic value is currently: ");
	Serial.println(value.c_str());

	pTXCharacteristic->registerForNotify(notifyCallback);

//	Obtain a reference to the RX characteristic of the Nordic UART service on the remote BLE server.
	pRXCharacteristic = pRemoteService->getCharacteristic(charUUID_RX);
	if (pRXCharacteristic == nullptr)
	{
		Serial.print("Failed to find our characteristic UUID: ");
		Serial.println(charUUID_RX.toString().c_str());
		return false;
	}
	Serial.println(" - Remote BLE RX characteristic reference established");

//	Write to the the RX characteristic.
	String helloValue = "Hello Remote Server";
	pRXCharacteristic->writeValue(helloValue.c_str(), helloValue.length());
}

//	Scan for BLE servers and find the first one that advertises our UART service.
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks
{
	void onResult(BLEAdvertisedDevice advertisedDevice)  /**        Called for each advertising BLE server.    */
	{
		String s_dev=advertisedDevice.toString().c_str();
		Serial.println("BLE Advertised Device found !");
		s_dev.replace(", ","\r\n"); s_dev.replace("Address","Addr"); s_dev.replace("serviceUUID","UUID");
		Serial.println(s_dev);
		M5.Lcd.println(s_dev);
		if(advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(serviceUUID))
		{
			Serial.println("Found device with the desired UUID_SERV!");
			M5.Lcd.println("Found device with the desired UUID_SERV!");
			digitalWrite(pin_M5_LED,HIGH);   // LED OFF (lowactive)
			advertisedDevice.getScan()->stop();
			M5.Lcd.setTextColor(WHITE,BLACK); M5.Lcd.println("SCAN stopped!");
			pServerAddress = new BLEAddress(advertisedDevice.getAddress());
			b_doConnect = true;
		}
	}
};

void setup()
{
	M5.begin(); pinMode(pin_M5_LED,OUTPUT); digitalWrite(pin_M5_LED,LOW); // LED is lowactiv
	M5.Lcd.setRotation(3); M5.Lcd.fillScreen(BLACK);
	DMM_begin();
	M5.Lcd.setTextColor(WHITE,BLUE); M5.Lcd.drawString("BLE-Scan ",0,26,4);
	M5.Lcd.setCursor(0,46);  M5.Lcd.println("Scan for Service UUID"); M5.Lcd.println(UUID_SERV);
	Serial.println("#########################################################");
	Serial.println("# ESP32 als BLE UART Client for Aneng Digitalmultimeter #");
	Serial.println("#########################################################");
	Serial.printf("Scan for Service UUID=%s\r\n",UUID_SERV);

	BLEDevice::init("");
	BLEScan* pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setActiveScan(true);            // we want active scanning
	pBLEScan->start(30);                      // scan to run for max 30 seconds.
	l_lastchk=millis(); l_lastchg=millis();
	setup_WiFi_AP();
  M5.Lcd.fillRect(0,114,240,20,GREEN); M5.Lcd.setTextColor(BLACK); 
  M5.Lcd.drawString("o)WiFi "+s_IP,0,114,4);
  M5.Lcd.setTextColor(WHITE);
	setup_serv_SOCK();
  setup_serv_HTTP();
  // serv_DNS.start(53,"*",ip_AP);  // Den NameServer auf 53 legen und ALLE Anfragen mit meiner IP beantworten!!
  Serial.println("DNS Server ready");
  delay(1000);
  M5.Lcd.fillRect(0,26,240,88,WHITE);
}

void loop()
{
	if(b_doConnect == true)
	{
		if (connectToServer(*pServerAddress))
				{ Serial.println("We are now connected to the BLE Server.");   b_connected = true;  }
			else{ Serial.println("We have failed to connect to the server; there is nothin more we will do.");    }
		b_doConnect = false;
	}
	if(b_connected)
	{
		if (b_onoff)
				{ Serial.println("Notifications turned on");   pTXCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)notificationOn, 2, true);    }
			else{ Serial.println("Notifications turned off");  pTXCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)notificationOff, 2, true);   }
		b_onoff = b_onoff ? 0 : 1;// Toggle on/off value for notifications.
		String timeSinceBoot = "Time since boot: " + String(millis()/1000);// Set the characteristic's value to be the array of bytes that is actually a string
		pRXCharacteristic->writeValue(timeSinceBoot.c_str(), timeSinceBoot.length());
	}
	while((millis()-l_lastchk)<5000)
	{
	  M5.update();
    if(M5.BtnA.wasPressed()){serv_SOCK.sendTXT(0,"1b 84 71 55 a2 c1 d8 f6 66 2a");}
		delay(1);
		serv_SOCK.loop();
		serv_HTTP.handleClient();
//    serv_DNS.processNextRequest();              // Anfragen nach URL zu IP beantworten
	}
	if((millis()-l_lastchg)>10000){l_lastchg=millis(); Serial.printf("Uptime %d sec\r\n",millis()/1000);}
	l_lastchk=millis();
}
