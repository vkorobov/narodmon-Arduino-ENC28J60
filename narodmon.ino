 /*
Скетч для Arduino/ENC28J60 для отправки метеоданных на Народный мониторинг 
Автор: Виталий Коробов
Версия: 0.1 (2014.10.06)

Основано на:
Версия 2.0 (19.07.2014)
Автор: Гладышев Дмитрий (2012-2014)
http://student-proger.ru/2014/07/meteostanciya-2-0/


*/

#include <OneWire.h>
#include <Wire.h>
#include <dht.h>
#include <Adafruit_BMP085.h>
#include <BH1750.h>
#include <EtherCard.h> // https://github.com/jcw/ethercard
#include <avr/wdt.h> // Watchdog timer 

bool Debug = true; //режим отладки

//********************************************************************************************
byte mac[] = { 0x,0x,0x,0x,0x,0x }; //MAC-адрес Arduino
#define BMP085_EXIST 1          // наличие датчика атмосферного давления
#define DHT_EXIST 1             // наличие датчика влажности
#define DHT2_EXIST 1            // наличие второго датчика влажности
#define LIGHTMETER_EXIST 0      // наличие датчика освещённости
#define DS18B20_PIN 2           // пин подключения термодатчика DS18B20
#define DHTPIN 6                // пин подключения датчика влажности DHT22
#define DHT2PIN 7               // пин подключения второго датчика влажности DHT22
#define postingInterval 600000  // интервал между отправками данных в миллисекундах (10 минут)
//********************************************************************************************
const char website[] PROGMEM = "narodmon.ru";

char macbuf[13];


OneWire  ds(DS18B20_PIN);

byte Ethernet::buffer[350];
uint32_t timer;
Stash stash;
byte session;

//timing variable
int res = 0;

#if LIGHTMETER_EXIST == 1
  BH1750 lightMeter;
#endif

#if BMP085_EXIST == 1
  Adafruit_BMP085 bmp;
#endif

#if DHT_EXIST == 1
  dht DHT;
#endif

#if DHT2_EXIST == 1
  dht DHT2;
#endif

int HighByte, LowByte, TReading, SignBit,  Whole, Fract;
char replyBuffer[200];                          // буфер для отправки
int CountSensors;                               // количество найденных датчиков температуры


void setup(void) {
  wdt_disable();
  
  Serial.begin(9600);

 //Узнаём количество термодатчиков
  CountSensors = DsCount();
  if (Debug)
  {
    Serial.print("Found ");
    Serial.print(CountSensors);
    Serial.println(" sensors."); 
  }
  
  #if BMP085_EXIST == 1
  if (!bmp.begin()) {
	   Serial.println("Could not find a valid BMP085 sensor, check wiring!");
  }
  #endif

  //Initialize Ethernet
  initialize_ethernet();

    memset(macbuf, 0, sizeof(macbuf));

    //Конвертируем MAC-адрес
    for (int k=0; k<6; k++)
    {
      int b1=mac[k]/16;
      int b2=mac[k]%16;
      char c1[2],c2[2];

      if (b1>9) c1[0]=(char)(b1-10)+'A';
      else c1[0] = (char)(b1) + '0';
      if (b2>9) c2[0]=(char)(b2-10)+'A';
      else c2[0] = (char)(b2) + '0';

      c1[1]='\0';
      c2[1]='\0';

      strcat(macbuf,c1);
      strcat(macbuf,c2);
    }

    if (Debug)
    {
      Serial.print("MAC: ");
      Serial.println(macbuf);
    }
  // включаем Watchdog timer на 8 сек
//  wdt_enable (WDTO_8S);
}

void loop(void) {


  //if correct answer is not received then re-initialize ethernet module

  if (res > 220){
    initialize_ethernet(); 
  }
  
  res = res + 1;
  wdt_reset();

  ether.packetLoop(ether.packetReceive());
  
  //200 res = 10 seconds (50ms each res)
  if (res == 200) {

    meteodata();
    if (Debug)
    {
      Serial.println(replyBuffer);
      Serial.print(F("Content-Length: "));
      Serial.println(len(replyBuffer));
    }

    byte sd = stash.create();
    stash.print(replyBuffer);
    stash.save();
    Stash::prepare(PSTR("POST http://narodmon.ru/post.php HTTP/1.0" "\r\n"
      "Host: narodmon.ru" "\r\n"
      "Content-Type: application/x-www-form-urlencoded" "\r\n"
      "Content-Length: $D" "\r\n"
      "\r\n"
      "$H"),
    stash.size(), sd);

    // send the packet - this also releases all stash buffers once done
    session = ether.tcpSend(); 
    Serial.println("Send...");
  }
   const char* reply = ether.tcpReply(session);

  if (res > 200){
     Serial.print(res);
     Serial.print(" - ");
     Serial.println(reply);
  }
   
   if (reply != 0) {
     res = 0;
     Serial.println(reply);
//    digitalWrite(led, LOW);    // turn the LED off by making the voltage LOW
    Serial.println("Ready...");
   }

delay(500);

}

void initialize_ethernet(void){  
  for(;;){ // keep trying until you succeed 
    //Reinitialize ethernet module
    if (Debug)
    { 
      Serial.println("Reseting Ethernet...");
    }
    digitalWrite(5, LOW);

    if (ether.begin(sizeof Ethernet::buffer, mac,10) == 0){ 
      Serial.println( "Failed to access Ethernet controller");
      continue;
    }
    Serial.println("DHCP...");    
    if (!ether.dhcpSetup()){
      Serial.println("DHCP failed");
      continue;
    }

    if (Debug)
    {
      ether.printIp("IP:  ", ether.myip);
      ether.printIp("GW:  ", ether.gwip);  
      ether.printIp("DNS: ", ether.dnsip);  
    }

    Serial.println("DNS...");
    
    if (!ether.dnsLookup(website)) {
      Serial.println("DNS failed");
      continue;
    }

    ether.printIp("SRV: ", ether.hisip);
    digitalWrite(5, HIGH);
    //delay(500);
    Serial.println("Ethernet OK...");
    //reset init value
    res = 0;
    break;
  }
}

//Количество термодатчиков на шине
int DsCount()
{
  int count=0;
  bool thatsall = false;
  byte addr[8];
  do
  {
    if ( !ds.search(addr))
    {
      ds.reset_search();
      thatsall = true;
    }
    count++;
  } while(!thatsall);
  return (count-1);
}





void meteodata()
{
  float Pressure = 0;
  float Humidity = 0;
  float Temperature = 0;
  int Tc_100 ;
    byte i;
    byte present = 0;
    byte type_s;
    byte data[12];
    byte addr[8];
    float celsius;
    char temp[17];
  

  //формирование HTTP-запроса
    memset(replyBuffer, 0, sizeof(replyBuffer));
    strcpy(replyBuffer,"ID=");

    strcat(replyBuffer, macbuf);
    // replyBuffer += macbuf;
        
    ds.reset_search();
    //Теперь в цикле опрашиваем все датчики сразу

    for (int j=0; j<CountSensors; j++)
    {

      byte i;
      byte present = 0;
      byte data[12];
      byte addr[8];

      if ( !ds.search(addr)) 
      {
        ds.reset_search();
//        return;
      }

      ds.reset();
      ds.select(addr);
      ds.write(0x44,1);

      delay(1000);

      present = ds.reset();
      ds.select(addr);    
      ds.write(0xBE);

      for ( i = 0; i < 9; i++) // we need 9 bytes
      {
        data[i] = ds.read();
      }

      LowByte = data[0];
      HighByte = data[1];
      TReading = (HighByte << 8) + LowByte;
      SignBit = TReading & 0x8000;  // test most sig bit
      if (SignBit) // negative
      {
        TReading = (TReading ^ 0xffff) + 1; // 2's comp
      }
      Tc_100 = (6 * TReading) + TReading / 4;    // multiply by (100 * 0.0625) or 6.25
      Whole = Tc_100 / 100;  // separate off the whole and fractional portions
      Fract = Tc_100 % 100;

      strcat(replyBuffer,"&");


      char temp[17];
 
      //конвертируем адрес термодатчика
      for (int k=0; k<8; k++)
      {
        int b1=addr[k]/16;
        int b2=addr[k]%16;
        char c1[2],c2[2];

        if (b1>9) c1[0]=(char)(b1-10)+'A';
        else c1[0] = (char)(b1) + '0';
        if (b2>9) c2[0]=(char)(b2-10)+'A';
        else c2[0] = (char)(b2) + '0';

        c1[1]='\0';
        c2[1]='\0';

        strcat(replyBuffer, c1);
        strcat(replyBuffer, c2);
      }


      strcat(replyBuffer,"=");

      if (SignBit) //если температура отрицательная, добавляем знак минуса
      {
        strcat(replyBuffer,"-");

      }
      

    memset(temp, 0, sizeof(temp));

      itoa(Whole,temp);
      strcat(replyBuffer,temp);
      strcat(replyBuffer,".");
      if (Fract<10)
      {
        strcat(replyBuffer,"0");
      }
      itoa(Fract,temp);
      strcat(replyBuffer,temp);
    }
    if (Debug)
    {
      Serial.print("buf: ");
      Serial.println(replyBuffer);
    }

    memset(temp, 0, sizeof(temp));
 
    long p_100;
    
    #if LIGHTMETER_EXIST == 1
      // get Lux Temperature
      strcat(replyBuffer, "&");
      strcat(replyBuffer, macbuf);
      strcat(replyBuffer, "23=");
      int lux=lightMeter.readLightLevel();
      itoa(lux, temp);
    if (Debug)
    {
        Serial.print("Light: ");
        Serial.println(lux);
    }
    #endif

    #if BMP085_EXIST == 1
      // get BMP085 pressure
      strcat(replyBuffer, "&");
      strcat(replyBuffer, macbuf);
      strcat(replyBuffer, "771=");
      Pressure=bmp.readPressure();
      p_100 = Pressure/1.333;
  Whole = p_100 / 100;
  Fract = p_100 % 100;
  itoa(Whole, temp);
  strcat(replyBuffer, temp);
  strcat(replyBuffer, ".");
  if (Fract<10)
  {
    strcat(replyBuffer,"0");
  }
  itoa(Fract, temp);
  strcat(replyBuffer, temp);

      //ftoc(Pressure/1.333);

      // get BMP085 temperature
      strcat(replyBuffer, "&");
      strcat(replyBuffer, macbuf);
      strcat(replyBuffer, "772=");
      Temperature = bmp.readTemperature();
      ftoc(Temperature*100);

      if (Debug)
      {
        Serial.print("Pressure: ");
        Serial.println(Pressure);
        Serial.print("Temperature: ");
        Serial.println(Temperature);
      }
 
    #endif
    
    #if DHT_EXIST == 1
      DHT.read22(DHTPIN);
      Humidity = DHT.humidity;
      Temperature = DHT.temperature;

      // get DHT22 Humidity
      strcat(replyBuffer, "&");
      strcat(replyBuffer, macbuf);
      strcat(replyBuffer, "011=");
      ftoc(Humidity*100);
      
      // get DHT22 Temperature
      strcat(replyBuffer, "&");
      strcat(replyBuffer, macbuf);
      strcat(replyBuffer, "012=");
      ftoc(Temperature*100);

      if (Debug)
      {
        Serial.print(F("Humidity: "));
        Serial.println(Humidity);
        Serial.print(F("Temperature: "));
        Serial.println(Temperature);
      }
    #endif
    #if DHT2_EXIST == 1
      DHT2.read22(DHT2PIN);
      Humidity = DHT2.humidity;
      Temperature = DHT2.temperature;

      // get DHT22 Humidity
      strcat(replyBuffer, "&");
      strcat(replyBuffer, macbuf);
      strcat(replyBuffer, "021=");
      ftoc(Humidity*100);
      
      // get DHT22 Temperature
      strcat(replyBuffer, "&");
      strcat(replyBuffer, macbuf);
      strcat(replyBuffer, "022=");
      ftoc(Temperature*100);
      if (Debug)
      {
        Serial.print("Humidity: ");
        Serial.println(Humidity);
        Serial.print("Temperature: ");
        Serial.println(Temperature);
      }
    #endif


    strcat(replyBuffer,'\0');

    /*if (Debug)
    {
      Serial.print("Data,");
      Serial.println(replyBuffer);
    }*/

}

void ftoc (int i_100)
{
  char temp[3];
  Whole = i_100 / 100;
  Fract = i_100 % 100;
  itoa(Whole, temp);
  strcat(replyBuffer, temp);
  strcat(replyBuffer, ".");
  if (Fract<10)
  {
    strcat(replyBuffer,"0");
  }
  itoa(Fract, temp);
  strcat(replyBuffer, temp);

}

void reverse(char s[])
{
  int i, j;
  char c;
  
  for (i = 0, j = strlen(s)-1; i<j; i++, j--) 
  {
    c = s[i];
    s[i] = s[j];
    s[j] = c;
  }
}

void itoa(int n, char s[])
{
  int i, sign;
  
  if ((sign = n) < 0)       /* записываем знак */
    n = -n;                 /* делаем n положительным числом */
  i = 0;
  do {                      /* генерируем цифры в обратном порядке */
    s[i++] = n % 10 + '0';  /* берем следующую цифру */
  } while ((n /= 10) > 0);  /* удаляем */
  if (sign < 0)
    s[i++] = '-';
  s[i] = '\0';
  reverse(s);
}

int len(char *buf)
{
  int i=0; 
  do
  {
    i++;
  } while (buf[i]!='\0');
  return i;
}
