// ################################################################################
// Programm: M5_StiP_BLE_DMM_V05B-ino
// Aufgabe:  M5_StickC Plus als Client for ANENG  V05B DMM
// Autor:    R. Hoermann hoermann@vom-kuhberg.de
// Version:  2022-01-07
// Hardware: M5-Stick C PLUS
// Extras:   ANENG V05B DMM mit Bluetooth Interface
//

//           +---------------------------+   Y -- Bluetooth -- Y   +---------------+
//           |ESP32 StickCPlus    LED(#) |   |                 |   |ANENG V05B  DMM|
//           |      +------------------+ o---+                 +---o +-----------+ |
//           |      | LCD 240x135      | |                         | |LCD Display| |
//           |BtnA  | 64k Farben       | |                         | |Monochrom  | |
//           |      |                  | |                         | +-----------+ |
//           |      |                  | |                         | [SEL]   [H/Z] |
//           |      +------------------+ |                         |      (o)      |
//           |                 BtnB      |                         |     [ >B]     |
//           +---------------------------+                         |     [ U ]     |
//                                                                 | (O)       (O) |
//                                                                 |      (O)      |
//                                                                 +---------------+
// #################################################################################

#include <M5StickCPlus.h>
#include <BLEDevice.h>
#include <WebServer.h>

// The remote service UUID we wish to connect to.
#define  UUID_S "0000fff0-0000-1000-8000-00805f9b34fb" 
// The characteristic UUID of the remote service we are interested in=DATA= Handle 9
#define  UUID_C "0000fff4-0000-1000-8000-00805f9b34fb"  

static   BLEUUID UUID_SERV(UUID_S);
static   BLEUUID UUID_CHAR(UUID_C);

static   boolean b_doConnect = false;
static   boolean b_connected = false;
static   boolean b_doScan    = false;
static   boolean b_HTTPD     = false;

static   BLERemoteCharacteristic* pRemoteCharacteristic;
static   BLEAdvertisedDevice* myDevice;
int      ai_XOR[] = {0x41,0x21,0x73,0x55,0xa2,0xc1,0x32,0x71,0x66,0xaa,0x3b,0xd0,0xe2};
int      ai_DMM[10];       // Speicher fuer die 10 Bytes des DMM
long     l_ms0;            // Store the beginning of Scan for BLE Devices

String   s_SevSeg="";
String   s_DMM="no Value"; // Value
String   s_PRE="";         // Prefix M k _ m mikro n
String   s_UNIT="";        // 'C 'F V A Ohm Hz
String   s_OPT="";         // Optionen BT,HOLD,DC,AC

#define  WiFi_SSID_AP "DMM_ANENG" // <== CHANGE THIS !!
#define  WiFi_PASS_AP "87654321"  // <== CHANGE THIS !!

IPAddress ip_ap(192,168, 32, 1);    // fixe IP Config fuer AP-Mode
IPAddress ip_gw(192,168, 32, 1);    // Gateway
IPAddress ip_sm(255,255,255, 0);    // Subnetmaske

WebServer serv_HTTP(80);
#include  "html.h"

#define  pin_M5_LED 10  // OnBoard RED LED on M5 StickCPlus

int  msb(int lsb)
{
	int mm=128,ml=1,b=0; 
	for(int i=0;i<8;i++){if((lsb & mm)==mm){b+=ml;} mm=mm/2; ml=ml*2;} 
	return b;
}

void digit(int dig)
{
	s_SevSeg+=String(dig,HEX)+" ";
	if(dig>127){s_DMM+=".";}
	int d7=dig & 0x7f;
	if(d7==0)   {s_DMM+=" "; return;}
	if(d7==0x7d){s_DMM+="0"; return;}
	if(d7==0x05){s_DMM+="1"; return;}
	if(d7==0x5b){s_DMM+="2"; return;}
	if(d7==0x1f){s_DMM+="3"; return;}
	if(d7==0x27){s_DMM+="4"; return;}
	if(d7==0x3e){s_DMM+="5"; return;}
	if(d7==0x7e){s_DMM+="6"; return;}
	if(d7==0x15){s_DMM+="7"; return;}
	if(d7==0x7f){s_DMM+="8"; return;}
	if(d7==0x3f){s_DMM+="9"; return;}
	if(d7==0x77){s_DMM+="A"; return;}
	if(d7==0x4c){s_DMM+="u"; return;}
	if(d7==0x6a){s_DMM+="t"; return;}
	if(d7==0x4e){s_DMM+="o"; return;}
	if(d7==0x68){s_DMM+="L"; return;}
	s_DMM+="_"+String(d7,HEX)+"_";
}

void DMM_begin()
{
	M5.Lcd.fillScreen(BLACK); M5.Lcd.fillRect(0,0,240,24,YELLOW);
	M5.Lcd.setTextColor(BLACK); M5.Lcd.drawString("ANENG V05B DMM",14,2,4);
  M5.Lcd.fillEllipse(6,12,7,12,BLUE);
	M5.Lcd.drawLine( 2, 4,10,16,WHITE); M5.Lcd.drawLine(10,16, 6,20,WHITE);
  M5.Lcd.drawLine( 6,20, 6, 0,WHITE);
  M5.Lcd.drawLine( 6, 0,10, 8,WHITE); M5.Lcd.drawLine(10, 8, 2,16,WHITE);
	M5.Lcd.setTextColor(WHITE);
}

void decode_DMM()
{
	M5.Lcd.setTextColor(BLACK);
	M5.Lcd.drawCentreString(s_DMM,90,40,7);
	M5.Lcd.drawString(s_UNIT,162,64,4);
	M5.Lcd.drawString(s_PRE, 162,40,4);
	M5.Lcd.drawString(s_OPT,   8,98,2);
	M5.Lcd.setTextColor(WHITE);
	s_DMM=""; s_SevSeg=""; s_OPT="BT "; s_UNIT=""; s_PRE="";
	digit(16*(ai_DMM[3]%16)+ai_DMM[4]/16);
	digit(16*(ai_DMM[4]%16)+ai_DMM[5]/16);
	digit(16*(ai_DMM[5]%16)+ai_DMM[6]/16);
	digit(16*(ai_DMM[6]%16)+ai_DMM[7]/16);
	Serial.print(s_SevSeg+"=>"+s_DMM);
//	M5.Lcd.fillRect(0,40,240,80,BLACK);
  
  if(ai_DMM[3] & 0x40){s_OPT+= "HOLD "; }
  
  if(ai_DMM[8] & 0x80){s_PRE+= "nano";  }
  if(ai_DMM[8] & 0x40){s_UNIT+="Volt";  } 
  if(ai_DMM[8] & 0x20){s_OPT+= "( )";   } // ?
  if(ai_DMM[8] & 0x10){s_OPT+= "BT ";   } // ?
  if(ai_DMM[8] & 0x08){s_UNIT+="Farad"; }
	if(ai_DMM[8] & 0x04){s_OPT+= " -|>- ";}
  if(ai_DMM[8] & 0x02){s_OPT+= " :) ";  } // ? Speaker
  if(ai_DMM[8] & 0x01){s_PRE+= "micro"; }
	
	if(ai_DMM[9] & 0x80){s_UNIT+="Ohm";   }
	if(ai_DMM[9] & 0x40){s_PRE+= "kilo";  }
  if(ai_DMM[9] & 0x20){s_PRE+= "milli"; } // ?
	if(ai_DMM[9] & 0x10){s_PRE+= "Mega";  } 
  if(ai_DMM[9] & 0x08){s_UNIT+="Ampere";} // ?
	if(ai_DMM[9] & 0x04){s_UNIT+="Hz";    }
	if(ai_DMM[9] & 0x02){s_UNIT+="'F";    }
	if(ai_DMM[9] & 0x01){s_UNIT+="'C";    }
 
	if(s_DMM=="Auto"){s_PRE+="Auto";}
	if(s_UNIT=="Volt" ){M5.Lcd.setTextColor(BLUE);}
	if(s_UNIT=="Farad"){M5.Lcd.setTextColor(MAGENTA);}
	if(s_UNIT=="Ohm"  ){M5.Lcd.setTextColor(YELLOW);}
	if(s_UNIT=="'C"   ){M5.Lcd.setTextColor(RED);}
	if(s_UNIT=="'F"   ){M5.Lcd.setTextColor(CYAN);}
	M5.Lcd.drawCentreString(s_DMM,90,40,7);
	M5.Lcd.drawString(s_PRE, 162,40,4);
	M5.Lcd.drawString(s_UNIT,162,64,4);
	M5.Lcd.drawString(s_OPT,   8,98,2);
}

static void notifyCallback
(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)
{
	Serial.printf("%8.3f Notify for UUID_C Data[%2d] RAW[]=[",millis()/1000.0,length);
	for(int i=0;i<length;i++){Serial.printf("%02X,",pData[i]);}
	Serial.print("] XOR[]=[");
	for(int i=0;i<length;i++){ ai_DMM[i]=msb(pData[i]^ai_XOR[i]); Serial.printf("%02X,",ai_DMM[i]);}
	Serial.print("] ");
	decode_DMM();
	Serial.println();
}

class MyClientCallback : public BLEClientCallbacks
{
	void onConnect(BLEClient* pclient)
	{
		Serial.println("CallBack.onConnect()");
	}

	void onDisconnect(BLEClient* pclient)
	{
		Serial.println("CallBack.onDisconnect()");  b_connected = false;
		digitalWrite(pin_M5_LED,LOW); M5.Lcd.fillScreen(BLACK); 
		M5.Lcd.drawString("Connection lost!",0,0,4);
	}
};

bool connectToServer()
{
	Serial.printf("Forming a connection to %s\r\n",myDevice->getAddress().toString().c_str());
	BLEClient*  pClient  = BLEDevice::createClient();
	Serial.println(" - Created client");
	pClient->setClientCallbacks(new MyClientCallback());
	pClient->connect(myDevice);  // Connect to the remove BLE Server.
	Serial.println(" - Connected to server");

	BLERemoteService* pRemoteService = pClient->getService(UUID_SERV);
	if (pRemoteService == nullptr)
	{
		Serial.print("Failed to find our service UUID: ");
		Serial.println(UUID_SERV.toString().c_str());
		pClient->disconnect();
		return false;
	}
	Serial.printf(" - Found our service %s\r\n",UUID_S);

	Serial.printf(" - Check for UUID_C   %s\r\n",UUID_C);
	pRemoteCharacteristic = pRemoteService->getCharacteristic(UUID_CHAR);
	if (pRemoteCharacteristic == nullptr)
	{
		Serial.println("Failed to find our characteristic UUID: ");
		pClient->disconnect(); return false;
	}
	Serial.println(" - Found our UUID_C");

	if(pRemoteCharacteristic->canRead()) // Read the value of the characteristic.
	{
		std::string value = pRemoteCharacteristic->readValue(); 
		Serial.printf("The characteristic value was:%s\r\n",value.c_str());
	}

	if(pRemoteCharacteristic->canNotify())
	{
		Serial.println("Device can NOTIFY!! Register Notify-Callback for it"); delay(1000);
		pRemoteCharacteristic->registerForNotify(notifyCallback);
	}
	b_connected = true;
}

// Scan for BLE servers and find the first one that advertises the service we are looking for.
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks
{
	void onResult(BLEAdvertisedDevice advertisedDevice)
	{
		Serial.print("BLE Advertised Device found: ");
		Serial.println(advertisedDevice.toString().c_str());
//		We have found a device, let us now see if it contains the service we are looking for.
		if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(UUID_SERV))
		{
			BLEDevice::getScan()->stop();
			myDevice    = new BLEAdvertisedDevice(advertisedDevice);
			b_doConnect = true;
			b_doScan    = true;
			Serial.printf("UUID_S found, therefore Scan stopped! after %d ms",millis()-l_ms0);
			digitalWrite(pin_M5_LED,HIGH); // LED off !!
			DMM_begin();
		}
	}
};

void setup_WiFi_AP()
{
    uint16_t i_xc=M5.Lcd.width()/2;
    M5.Lcd.drawCentreString("setup WiFi as AP",i_xc,8,1);
    M5.Lcd.drawCentreString(String(WiFi_SSID_AP),i_xc,18,4);
    M5.Lcd.drawCentreString(String(WiFi_PASS_AP),i_xc,38,4);
    WiFi.disconnect(); delay(100);  // Eventuell noch bestehende Verbindungen abbrechen
    WiFi.mode(WIFI_AP);             // ESP spannt ein EIGENES WiFi auf
    WiFi.softAPConfig(ip_ap,ip_gw,ip_sm); // Zuerst IPConfig dann Name!!
    while(!WiFi.softAP(WiFi_SSID_AP,WiFi_PASS_AP)){Serial.write('.'); delay(500); }
    M5.Lcd.drawCentreString("IP="+WiFi.softAPIP().toString(),i_xc,60,1);
    
}

void serv_FAVICO()
{ serv_HTTP.send(200,"image/svg+xml",s_favicon); }

void serv_HTML()
{
  String s=s_html_template;
  s.replace("%dmmtxt%",s_DMM);
  serv_HTTP.send(200,"text/html",s);
}
void setup_HTTPD()
{
   setup_WiFi_AP();
   serv_HTTP.on("/",serv_HTML);
   serv_HTTP.on("/favicon.ico",serv_FAVICO);
   serv_HTTP.begin();
   b_HTTPD=true;
   delay(5000); DMM_begin();
    M5.Lcd.fillEllipse(235,128,6,12,GREEN);
}
void setup()
{
	M5.begin(); pinMode(pin_M5_LED,OUTPUT); digitalWrite(pin_M5_LED,LOW); // LED is lowactiv
	Serial.println("Starting ESP32 as Bluetooth Low Energie Client ..");
	Serial.printf("Scan for Service UUID=%s\r\n",UUID_S);
	M5.Lcd.setRotation(3); M5.Lcd.fillScreen(BLACK);
	M5.Lcd.drawString("BLE-Scanner",0,0,4);
	M5.Lcd.setCursor(0,24);
	M5.Lcd.println("Scan for Service UUID");
	M5.Lcd.println(UUID_S);
	l_ms0=millis();
	BLEDevice::init("");
	BLEScan* pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setInterval(1349);
	pBLEScan->setWindow(449);
	pBLEScan->setActiveScan(true);  // Specify that we want active scanning
	pBLEScan->start(5, false);      // AutoStop SCAN  after 5 seconds.
}

void loop()
{
	if(b_doConnect == true)
	{
		if(connectToServer())
			{ Serial.println("Now connected to the BLE Server.");    }
		else{ Serial.println("FAILED to connect to the server."); }
		b_doConnect = false;
	}
	if(!b_connected & b_doScan)
	{	l_ms0=millis();	BLEDevice::getScan()->start(0);}  // SCAN if NOT Connected
	M5.update();
	if(M5.BtnA.wasPressed()){DMM_begin();}
  if(M5.BtnB.wasPressed()){setup_HTTPD();}
  if(b_HTTPD){serv_HTTP.handleClient(); }       // Browseranfragen beantworten
}
