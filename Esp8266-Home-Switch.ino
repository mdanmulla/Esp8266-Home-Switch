// =====================================================================
//  SMART HOME CONTROLLER v1  NodeMCU ESP8266
//  6 Relays  6 Switches  Blynk  Web UI  Auto-AP  Schedule
//  Weather via Open-Meteo  Category Cards
//  Cyan/Dark Cyberpunk theme  Weekly kWh  Per-device Schedule
// =====================================================================

#define BLYNK_TEMPLATE_ID "TEMPLATE ID" // <-- YOUR BLYNK TEMPLATE ID
#define BLYNK_TEMPLATE_NAME "YOUR TEMPLATE NAME" // <-- YOUR BLYNK TEMPLATE NAME
#define BLYNK_AUTH_TOKEN "YOUR AUTH TOKEN HERE" // <-- YOUR BLYNK AUTH TOKEN HERE

const char* DEFAULT_WIFI_SSID = "";
const char* DEFAULT_WIFI_PASS = "";

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>
#include <ArduinoOTA.h>

// Include your external HTML file here
#include "webui.h"

#define RELAY_ON  LOW
#define RELAY_OFF HIGH
#define DEVICE_COUNT 6

// !! CUSTOMIZE pin numbers for your 6-relay + 3-switch board !!
const uint8_t RELAY_PIN[DEVICE_COUNT]  = {5,4,14,12,16,255};   // D1,D2,D5,D6,D0
const uint8_t SWITCH_PIN[DEVICE_COUNT] = {255,13,3,1,255,255}; // D7,RX,TX
const int     VPIN[DEVICE_COUNT]       = {V1,V2,V3,V4,V5,V6};

//  EEPROM map (6-device layout, addresses retain legacy spacing)
#define EEPROM_SIZE    4096
#define EEPROM_KEY     0xB0        // legacy EEPROM layout key
#define ADDR_KEY       0
#define ADDR_STATES    1           // device count bytes
#define ADDR_MODES     9           // device count bytes
#define ADDR_NAMES     17          // 20 bytes per device
#define ADDR_SCHED     177         // device count bytes
#define ADDR_NIGHT_H   185
#define ADDR_NIGHT_M   186
#define ADDR_MORN_H    187
#define ADDR_MORN_M    188
#define ADDR_TZ        189
#define ADDR_AP_MODE   190
#define ADDR_CATEGORIES 191     // 6 device category values
#define ADDR_AP_SSID   196         // 16 bytes
#define ADDR_AP_PASS   212         // 16 bytes
#define ADDR_AP_IP1    228
#define ADDR_AP_IP2    229
#define ADDR_AP_IP3    230
#define ADDR_AP_IP4    231
//  Extended block 1
#define ADDR_EXT_KEY   232         // 0xBF
#define ADDR_WX_LAT    233         // 9 bytes
#define ADDR_WX_LON    242         // 10 bytes
#define ADDR_WX_CITY   252         // 31 bytes
#define ADDR_WIFI_SSID 283         // 31 bytes
#define ADDR_WIFI_PASS 314         // 63 bytes
//  Extended block 2
#define ADDR_EXT2_KEY  377         // 0xCE
#define ADDR_WATTS     378         // 2 bytes per device
#define ADDR_AP_HIDDEN 394         // 1 byte
//  Extended block 3  weekly kWh
#define ADDR_EXT3_KEY  395         // 0xDD
#define ADDR_KWH       396         // 4 bytes per device
#define ADDR_WEEK_TS   428         // 4 bytes
//  Extended block 4  per-device schedule times
#define ADDR_EXT4_KEY    432       // 0xEE
#define ADDR_SCHED_ON_H  433       // device count bytes
#define ADDR_SCHED_ON_M  441       // legacy address spacing retained
#define ADDR_SCHED_OFF_H 449       // legacy address spacing retained
#define ADDR_SCHED_OFF_M 457       // legacy address spacing retained
#define ADDR_SWITCH_ENABLED 465    // global physical switch enable
#define ADDR_DEVICE_EN_KEY 466     // per-device dashboard switch enable key
#define ADDR_DEVICE_ENABLED 467    // six device enable bytes
#define ADDR_CONFIRM_ON_KEY 473    // confirmation setting format key
#define ADDR_CONFIRM_ON 474        // six confirmation setting bytes
#define ADDR_WIFI_RESET_KEY 480    // hotspot-first WiFi credential reset marker
#define WIFI_RESET_VALUE 0xD1
#define ADDR_CLOCK_MANUAL 485      // persistent manual-clock marker
#define ADDR_CLOCK_EPOCH 486       // persistent clock epoch (4 bytes)
#define ADDR_STATIC_KEY 490
#define ADDR_STATIC_ENABLED 491
#define ADDR_STATIC_IP 492         // 4 bytes
#define ADDR_STATIC_GATEWAY 496    // 4 bytes
#define ADDR_STATIC_SUBNET 500     // 4 bytes
#define ADDR_STATIC_DNS 504        // 4 bytes
// TOTAL legacy layout: 474 + 6 = 480 bytes < 512

#define AUTH_MAX_USERS 4
#define AUTH_USER_NAME_LEN 20
#define AUTH_PASS_LEN 32
#define DEFAULT_ADMIN_USER "admin"
#define AUTH_BLOCK_SIZE 53
#define ADDR_AUTH_KEY 512
#define ADDR_AUTH_ENABLED 513
#define ADDR_AUTH_USERS 514
#define ADDR_SWITCH_PERMISSIONS 800 // user 1-3 x six switches, admin always allowed

#define NAME_LEN     20
#define AP_SSID_LEN  16
#define AP_PASS_LEN  16
#define WX_CITY_LEN  31
#define WX_LAT_LEN   9
#define WX_LON_LEN   10
#define WIFI_SSID_LEN 31
#define WIFI_PASS_LEN 63

//  Globals
bool  applianceState[DEVICE_COUNT] = {false};
bool  momentaryMode[DEVICE_COUNT]  = {false};
bool  schedEnabled[DEVICE_COUNT]   = {false};
char  appName[DEVICE_COUNT][NAME_LEN+1];
int   lastSwitchState[DEVICE_COUNT] = {HIGH,HIGH,HIGH,HIGH,HIGH,HIGH};
int   switchCandidate[DEVICE_COUNT] = {HIGH,HIGH,HIGH,HIGH,HIGH,HIGH};
unsigned long switchDebounceAt[DEVICE_COUNT] = {0};
unsigned long switchLastChangeAt[DEVICE_COUNT] = {0};
bool switchInputEnabled = false;
bool dashboardSwitchEnabled[DEVICE_COUNT] = {true,true,true,true,true,true};
bool confirmBeforeOff[DEVICE_COUNT] = {false,false,false,false,false,false};
bool  relayStatesDirty=false;
unsigned long relaySaveAt=0;
bool  energyDirty=false;
unsigned long energySaveAt=0;
uint32_t timerEndMs[DEVICE_COUNT] = {0};
uint8_t deviceCategory[DEVICE_COUNT] = {0,1,1,2,0,3};
const char* CATEGORY_NAMES[]={"lighting","cooling","heating","appliance","entertainment"};

// Power tracking
uint16_t  deviceWatts[DEVICE_COUNT]  = {0};
unsigned long onSinceMs[DEVICE_COUNT]= {0};
uint32_t  storedWh[DEVICE_COUNT]     = {0};
uint32_t  weekStartUnix   = 0;

// Per-device schedule times
int schedOnHour[DEVICE_COUNT]  = {18,18,18,18,18,18};
int schedOnMin[DEVICE_COUNT]   = { 0, 0, 0, 0, 0, 0};
int schedOffHour[DEVICE_COUNT] = { 6, 6, 6, 6, 6, 6};
int schedOffMin[DEVICE_COUNT]  = { 0, 0, 0, 0, 0, 0};

float tzOffset      = 5.75f;
int   nightOnHour=18, nightOnMin=0;
int   mornOffHour=6,  mornOffMin=0;
bool  timeAvailable=false, timeIsManual=false;
bool  apModeEnabled=false;
bool  apHidden=false;
bool  apMode=false;
bool  wifiConnected=false;

char apSSID[AP_SSID_LEN+1]   = "SmartHome_AP";
char apPassword[AP_PASS_LEN+1]= "12345678";
IPAddress apIP(192,168,4,1);

char wifiSSID[WIFI_SSID_LEN+1];
char wifiPass[WIFI_PASS_LEN+1];
IPAddress wifiStaticIP;
IPAddress wifiGateway;
IPAddress wifiSubnet;
IPAddress wifiDNS;
bool wifiStaticEnabled=false;

char wxCity[WX_CITY_LEN+1] = {0};
char wxLat[WX_LAT_LEN+1]   = {0};
char wxLon[WX_LON_LEN+1]   = {0};

bool dashboardAuthEnabled = false;
char dashboardUsers[AUTH_MAX_USERS][AUTH_USER_NAME_LEN+1];
char dashboardPasses[AUTH_MAX_USERS][AUTH_PASS_LEN+1];
String dashboardSessions[4];
String dashboardAdminSessions[4];
String dashboardSessionUsers[4];
unsigned long dashboardSessionStartedAt[4] = {0};
const unsigned long DASHBOARD_SESSION_TIMEOUT_MS = 12UL * 60UL * 60UL * 1000UL;
bool switchPermissions[AUTH_MAX_USERS][DEVICE_COUNT];

BlynkTimer       blynkTimer;
ESP8266WebServer server(80);
DNSServer        dnsServer;
bool             dnsRunning = false;
int lastScheduleMinute = -1;
time_t lastScheduleCheckTime = 0;
bool ntpSyncInProgress = false;
unsigned long ntpSyncStartedAt = 0;
unsigned long ntpRetryAt = 0;
unsigned long blynkRetryAt = 0;
unsigned long wifiReconnectAt = 0;
unsigned long wifiReconnectDelayMs = 1000;
bool wifiStationProbe = false;
unsigned long wifiReconnectProbeStart = 0;

// Login rate limiting (Brute-force protection)
uint8_t loginFailCount = 0;
unsigned long loginLockoutUntil = 0;

//  EEPROM
void saveDashboardAuth();
void eepromLoad(){
  EEPROM.begin(EEPROM_SIZE);
  if(EEPROM.read(ADDR_KEY)!=EEPROM_KEY){
    EEPROM.write(ADDR_KEY,EEPROM_KEY);
    for(int i=0;i<DEVICE_COUNT;i++){
      EEPROM.write(ADDR_STATES+i,0);
      EEPROM.write(ADDR_MODES+i,0);
      EEPROM.write(ADDR_SCHED+i,0);
    }
    const char* def[DEVICE_COUNT]={"Appliance 1","Appliance 2","Appliance 3","Appliance 4",
                                   "Appliance 5","Appliance 6"};
    for(int i=0;i<DEVICE_COUNT;i++)
      for(int j=0;j<NAME_LEN;j++)
        EEPROM.write(ADDR_NAMES+i*NAME_LEN+j,j<(int)strlen(def[i])?def[i][j]:0);
    EEPROM.write(ADDR_NIGHT_H,18); EEPROM.write(ADDR_NIGHT_M,0);
    EEPROM.write(ADDR_MORN_H,6);   EEPROM.write(ADDR_MORN_M,0);
    EEPROM.write(ADDR_TZ,(byte)((5.75f+12.0f)*4.0f+0.5f));
    EEPROM.write(ADDR_AP_MODE,0); EEPROM.write(ADDR_AP_HIDDEN,0);
    EEPROM.write(ADDR_SWITCH_ENABLED,0);
    for(int i=0;i<DEVICE_COUNT;i++) EEPROM.write(ADDR_CATEGORIES+i,deviceCategory[i]);
    const char* ds="SmartHome_AP"; const char* dp="12345678";
    for(int i=0;i<AP_SSID_LEN;i++) EEPROM.write(ADDR_AP_SSID+i,i<(int)strlen(ds)?ds[i]:0);
    for(int i=0;i<AP_PASS_LEN;i++) EEPROM.write(ADDR_AP_PASS+i,i<(int)strlen(dp)?dp[i]:0);
    EEPROM.write(ADDR_AP_IP1,192);EEPROM.write(ADDR_AP_IP2,168);
    EEPROM.write(ADDR_AP_IP3,4);  EEPROM.write(ADDR_AP_IP4,1);
    EEPROM.commit();
  }
  for(int i=0;i<DEVICE_COUNT;i++){
    applianceState[i]=EEPROM.read(ADDR_STATES+i)==1;
    momentaryMode[i] =EEPROM.read(ADDR_MODES+i)==1;
    schedEnabled[i]  =EEPROM.read(ADDR_SCHED+i)==1;
    for(int j=0;j<NAME_LEN;j++) appName[i][j]=(char)EEPROM.read(ADDR_NAMES+i*NAME_LEN+j);
    appName[i][NAME_LEN]='\0';
  }
  nightOnHour =EEPROM.read(ADDR_NIGHT_H); nightOnMin =EEPROM.read(ADDR_NIGHT_M);
  mornOffHour =EEPROM.read(ADDR_MORN_H);  mornOffMin =EEPROM.read(ADDR_MORN_M);
  tzOffset    =(float)EEPROM.read(ADDR_TZ)/4.0f-12.0f;
  if(nightOnHour>23) nightOnHour=18;
  if(nightOnMin>59) nightOnMin=0;
  if(mornOffHour>23) mornOffHour=6;
  if(mornOffMin>59) mornOffMin=0;
  tzOffset=constrain(tzOffset,-12.0f,14.0f);
  apModeEnabled=EEPROM.read(ADDR_AP_MODE)==1;
  apHidden     =EEPROM.read(ADDR_AP_HIDDEN)==1;
  switchInputEnabled=EEPROM.read(ADDR_SWITCH_ENABLED)==1;
  if(EEPROM.read(ADDR_DEVICE_EN_KEY)==0xCF){
    for(int i=0;i<DEVICE_COUNT;i++) dashboardSwitchEnabled[i]=EEPROM.read(ADDR_DEVICE_ENABLED+i)==1;
  }else{
    for(int i=0;i<DEVICE_COUNT;i++) dashboardSwitchEnabled[i]=true;
    EEPROM.write(ADDR_DEVICE_EN_KEY,0xCF);
    for(int i=0;i<DEVICE_COUNT;i++) EEPROM.write(ADDR_DEVICE_ENABLED+i,1);
  }
  if(EEPROM.read(ADDR_CONFIRM_ON_KEY)==0xD0){
    for(int i=0;i<DEVICE_COUNT;i++) confirmBeforeOff[i]=EEPROM.read(ADDR_CONFIRM_ON+i)==1;
  }else{
    for(int i=0;i<DEVICE_COUNT;i++) confirmBeforeOff[i]=false;
    EEPROM.write(ADDR_CONFIRM_ON_KEY,0xD0);
    for(int i=0;i<DEVICE_COUNT;i++) EEPROM.write(ADDR_CONFIRM_ON+i,0);
  }
  EEPROM.commit();
  for(int i=0;i<DEVICE_COUNT;i++){
    uint8_t cat=EEPROM.read(ADDR_CATEGORIES+i);
    if(cat>4) cat=deviceCategory[i];
    deviceCategory[i]=cat;
  }
  auto rdStr=[](int addr,char* buf,int len){
    for(int i=0;i<len;i++){char c=(char)EEPROM.read(addr+i);if(!c){buf[i]='\0';return;}buf[i]=c;}
    buf[len]='\0';
  };
  for(int i=0;i<AUTH_MAX_USERS;i++){
    dashboardUsers[i][0]='\0';
    dashboardPasses[i][0]='\0';
  }
  for(int i=0;i<AUTH_MAX_USERS;i++)
    for(int s=0;s<DEVICE_COUNT;s++)
      switchPermissions[i][s]=false;
  dashboardAuthEnabled=EEPROM.read(ADDR_AUTH_KEY)==0xA1&&EEPROM.read(ADDR_AUTH_ENABLED)==1;
  if(EEPROM.read(ADDR_AUTH_KEY)==0xA1){
    for(int i=0;i<AUTH_MAX_USERS;i++){
      int base=ADDR_AUTH_USERS+i*AUTH_BLOCK_SIZE;
      for(int j=0;j<AUTH_USER_NAME_LEN;j++){
        dashboardUsers[i][j]=(char)EEPROM.read(base+1+j);
      }
      dashboardUsers[i][AUTH_USER_NAME_LEN]='\0';
      for(int j=0;j<AUTH_PASS_LEN;j++){
        dashboardPasses[i][j]=(char)EEPROM.read(base+1+AUTH_USER_NAME_LEN+j);
      }
      dashboardPasses[i][AUTH_PASS_LEN]='\0';
    }
  }else{
    strcpy(dashboardUsers[0],"admin");
    dashboardPasses[0][0]='\0';
    EEPROM.write(ADDR_AUTH_KEY,0xA1);
    EEPROM.write(ADDR_AUTH_ENABLED,0);
    saveDashboardAuth();
  }
  if(strlen(dashboardUsers[0])==0 || strcmp(dashboardUsers[0],"admin")!=0){
    for(int j=AUTH_MAX_USERS-1;j>0;j--){
      strcpy(dashboardUsers[j],dashboardUsers[j-1]);
      strcpy(dashboardPasses[j],dashboardPasses[j-1]);
    }
    strcpy(dashboardUsers[0],"admin");
  }
  if(EEPROM.read(ADDR_SWITCH_PERMISSIONS)==0xE5){
    for(int i=0;i<AUTH_MAX_USERS;i++)
      for(int s=0;s<DEVICE_COUNT;s++)
        switchPermissions[i][s]=EEPROM.read(ADDR_SWITCH_PERMISSIONS+1+i*DEVICE_COUNT+s)==1;
  }else{
    EEPROM.write(ADDR_SWITCH_PERMISSIONS,0xE5);
    for(int i=0;i<AUTH_MAX_USERS;i++)
      for(int s=0;s<DEVICE_COUNT;s++)
        EEPROM.write(ADDR_SWITCH_PERMISSIONS+1+i*DEVICE_COUNT+s,0);
  }
  if(strlen(apSSID) < 3)
      strncpy(apSSID,"SmartHome_AP",AP_SSID_LEN);

  if(strlen(apPassword) < 8)
      strncpy(apPassword,"12345678",AP_PASS_LEN);
  if(strlen(apSSID)==0) strncpy(apSSID,"SmartHome_AP",AP_SSID_LEN);
  if(strlen(apPassword)==0) strncpy(apPassword,"12345678",AP_PASS_LEN);
  apIP[0]=EEPROM.read(ADDR_AP_IP1); apIP[1]=EEPROM.read(ADDR_AP_IP2);
  apIP[2]=EEPROM.read(ADDR_AP_IP3); apIP[3]=EEPROM.read(ADDR_AP_IP4);
  wifiSSID[0]='\0';
  wifiStaticIP=IPAddress(0,0,0,0);
  wifiGateway=IPAddress(0,0,0,0);
  wifiSubnet=IPAddress(255,255,255,0);
  wifiDNS=IPAddress(0,0,0,0);
  wifiPass[0]='\0';
  if(EEPROM.read(ADDR_EXT_KEY)==0xBF){
    rdStr(ADDR_WX_LAT, wxLat,  WX_LAT_LEN);
    rdStr(ADDR_WX_LON, wxLon,  WX_LON_LEN);
    rdStr(ADDR_WX_CITY,wxCity, WX_CITY_LEN);
    rdStr(ADDR_WIFI_SSID,wifiSSID,WIFI_SSID_LEN);
    rdStr(ADDR_WIFI_PASS,wifiPass,WIFI_PASS_LEN);
  }
  if(EEPROM.read(ADDR_WIFI_RESET_KEY)!=WIFI_RESET_VALUE){
    wifiSSID[0]='\0';
    wifiPass[0]='\0';
    for(int i=0;i<WIFI_SSID_LEN;i++) EEPROM.write(ADDR_WIFI_SSID+i,0);
    for(int i=0;i<WIFI_PASS_LEN;i++) EEPROM.write(ADDR_WIFI_PASS+i,0);
    EEPROM.write(ADDR_WIFI_RESET_KEY,WIFI_RESET_VALUE);
    EEPROM.commit();
  }
  if(EEPROM.read(ADDR_STATIC_KEY)==0xD2){
    wifiStaticEnabled=EEPROM.read(ADDR_STATIC_ENABLED)==1;
    wifiStaticIP=IPAddress(EEPROM.read(ADDR_STATIC_IP),EEPROM.read(ADDR_STATIC_IP+1),EEPROM.read(ADDR_STATIC_IP+2),EEPROM.read(ADDR_STATIC_IP+3));
    wifiGateway=IPAddress(EEPROM.read(ADDR_STATIC_GATEWAY),EEPROM.read(ADDR_STATIC_GATEWAY+1),EEPROM.read(ADDR_STATIC_GATEWAY+2),EEPROM.read(ADDR_STATIC_GATEWAY+3));
    wifiSubnet=IPAddress(EEPROM.read(ADDR_STATIC_SUBNET),EEPROM.read(ADDR_STATIC_SUBNET+1),EEPROM.read(ADDR_STATIC_SUBNET+2),EEPROM.read(ADDR_STATIC_SUBNET+3));
    wifiDNS=IPAddress(EEPROM.read(ADDR_STATIC_DNS),EEPROM.read(ADDR_STATIC_DNS+1),EEPROM.read(ADDR_STATIC_DNS+2),EEPROM.read(ADDR_STATIC_DNS+3));
    if(!wifiSubnet[0]) wifiSubnet=IPAddress(255,255,255,0);
  }
  if(EEPROM.read(ADDR_CLOCK_MANUAL)==0xD2||EEPROM.read(ADDR_CLOCK_MANUAL)==0xD3){
    uint32_t savedEpoch=(uint32_t)EEPROM.read(ADDR_CLOCK_EPOCH)
                       |((uint32_t)EEPROM.read(ADDR_CLOCK_EPOCH+1)<<8)
                       |((uint32_t)EEPROM.read(ADDR_CLOCK_EPOCH+2)<<16)
                       |((uint32_t)EEPROM.read(ADDR_CLOCK_EPOCH+3)<<24);
    if(savedEpoch>1000000000UL){
      struct timeval tv={(time_t)savedEpoch,0};
      settimeofday(&tv,nullptr);
      timeAvailable=true;
      timeIsManual=(EEPROM.read(ADDR_CLOCK_MANUAL)==0xD2);
    }
  }
  if(strlen(wifiSSID)==0) strncpy(wifiSSID,DEFAULT_WIFI_SSID,WIFI_SSID_LEN);
  if(strlen(wifiPass)==0) strncpy(wifiPass,DEFAULT_WIFI_PASS,WIFI_PASS_LEN);
  if(EEPROM.read(ADDR_EXT2_KEY)==0xCE){
    for(int i=0;i<DEVICE_COUNT;i++)
      deviceWatts[i]=(uint16_t)EEPROM.read(ADDR_WATTS+i*2)|((uint16_t)EEPROM.read(ADDR_WATTS+i*2+1)<<8);
  }
  if(EEPROM.read(ADDR_EXT3_KEY)==0xDD){
    for(int i=0;i<DEVICE_COUNT;i++){
      storedWh[i]=((uint32_t)EEPROM.read(ADDR_KWH+i*4))
                 |((uint32_t)EEPROM.read(ADDR_KWH+i*4+1)<<8)
                 |((uint32_t)EEPROM.read(ADDR_KWH+i*4+2)<<16)
                 |((uint32_t)EEPROM.read(ADDR_KWH+i*4+3)<<24);
    }
    weekStartUnix=((uint32_t)EEPROM.read(ADDR_WEEK_TS))
                 |((uint32_t)EEPROM.read(ADDR_WEEK_TS+1)<<8)
                 |((uint32_t)EEPROM.read(ADDR_WEEK_TS+2)<<16)
                 |((uint32_t)EEPROM.read(ADDR_WEEK_TS+3)<<24);
  }
  if(EEPROM.read(ADDR_EXT4_KEY)==0xEE){
    for(int i=0;i<DEVICE_COUNT;i++){
      schedOnHour[i] =EEPROM.read(ADDR_SCHED_ON_H+i);
      schedOnMin[i]  =EEPROM.read(ADDR_SCHED_ON_M+i);
      schedOffHour[i]=EEPROM.read(ADDR_SCHED_OFF_H+i);
      schedOffMin[i] =EEPROM.read(ADDR_SCHED_OFF_M+i);
      if(schedOnHour[i]>23) schedOnHour[i]=18;
      if(schedOnMin[i]>59) schedOnMin[i]=0;
      if(schedOffHour[i]>23) schedOffHour[i]=6;
      if(schedOffMin[i]>59) schedOffMin[i]=0;
    }
  }
  for(int i=0;i<DEVICE_COUNT;i++) if(applianceState[i]) onSinceMs[i]=millis();
}

void eepromSaveCore(){
  for(int i=0;i<DEVICE_COUNT;i++){
    EEPROM.write(ADDR_STATES+i,applianceState[i]?1:0);
    EEPROM.write(ADDR_MODES+i, momentaryMode[i]?1:0);
    EEPROM.write(ADDR_SCHED+i, schedEnabled[i]?1:0);
  }
  EEPROM.write(ADDR_NIGHT_H,(byte)nightOnHour); EEPROM.write(ADDR_NIGHT_M,(byte)nightOnMin);
  EEPROM.write(ADDR_MORN_H,(byte)mornOffHour);  EEPROM.write(ADDR_MORN_M,(byte)mornOffMin);
  EEPROM.write(ADDR_TZ,(byte)((tzOffset+12.0f)*4.0f+0.5f));
  EEPROM.write(ADDR_AP_MODE,apModeEnabled?1:0);
  EEPROM.write(ADDR_AP_HIDDEN,apHidden?1:0);
  EEPROM.write(ADDR_SWITCH_ENABLED,switchInputEnabled?1:0);
  EEPROM.write(ADDR_DEVICE_EN_KEY,0xCF);
  for(int i=0;i<DEVICE_COUNT;i++) EEPROM.write(ADDR_DEVICE_ENABLED+i,dashboardSwitchEnabled[i]?1:0);
  EEPROM.write(ADDR_CONFIRM_ON_KEY,0xD0);
  for(int i=0;i<DEVICE_COUNT;i++) EEPROM.write(ADDR_CONFIRM_ON+i,confirmBeforeOff[i]?1:0);
  for(int i=0;i<DEVICE_COUNT;i++) EEPROM.write(ADDR_CATEGORIES+i,deviceCategory[i]);
  EEPROM.commit();
}
void eepromSaveStates(){
  for(int i=0;i<DEVICE_COUNT;i++) EEPROM.write(ADDR_STATES+i,applianceState[i]?1:0);
  EEPROM.commit();
}
void eepromSaveName(int i){
  for(int j=0;j<NAME_LEN;j++) EEPROM.write(ADDR_NAMES+i*NAME_LEN+j,j<(int)strlen(appName[i])?appName[i][j]:0);
  EEPROM.commit();
}
void eepromSaveAP(){
  auto wrStr=[](int addr,const char* s,int len){
    for(int i=0;i<len;i++) EEPROM.write(addr+i,i<(int)strlen(s)?s[i]:0);
  };
  wrStr(ADDR_AP_SSID,apSSID,AP_SSID_LEN);
  wrStr(ADDR_AP_PASS,apPassword,AP_PASS_LEN);
  EEPROM.write(ADDR_AP_IP1,apIP[0]); EEPROM.write(ADDR_AP_IP2,apIP[1]);
  EEPROM.write(ADDR_AP_IP3,apIP[2]); EEPROM.write(ADDR_AP_IP4,apIP[3]);
  EEPROM.commit();
}
void eepromSaveExt(){
  auto wrStr=[](int addr,const char* s,int len){
    for(int i=0;i<len;i++) EEPROM.write(addr+i,i<(int)strlen(s)?s[i]:0);
  };
  EEPROM.write(ADDR_EXT_KEY,0xBF);
  wrStr(ADDR_WX_LAT, wxLat,  WX_LAT_LEN);
  wrStr(ADDR_WX_LON, wxLon,  WX_LON_LEN);
  wrStr(ADDR_WX_CITY,wxCity, WX_CITY_LEN);
  wrStr(ADDR_WIFI_SSID,wifiSSID,WIFI_SSID_LEN);
  wrStr(ADDR_WIFI_PASS,wifiPass,WIFI_PASS_LEN);
  EEPROM.write(ADDR_WIFI_RESET_KEY,WIFI_RESET_VALUE);
  EEPROM.write(ADDR_STATIC_KEY,0xD2);
  EEPROM.write(ADDR_STATIC_ENABLED,wifiStaticEnabled?1:0);
  for(int i=0;i<4;i++) EEPROM.write(ADDR_STATIC_IP+i,wifiStaticIP[i]);
  for(int i=0;i<4;i++) EEPROM.write(ADDR_STATIC_GATEWAY+i,wifiGateway[i]);
  for(int i=0;i<4;i++) EEPROM.write(ADDR_STATIC_SUBNET+i,wifiSubnet[i]);
  for(int i=0;i<4;i++) EEPROM.write(ADDR_STATIC_DNS+i,wifiDNS[i]);
  EEPROM.commit();
}
void eepromSaveWatts(){
  EEPROM.write(ADDR_EXT2_KEY,0xCE);
  for(int i=0;i<DEVICE_COUNT;i++){
    EEPROM.write(ADDR_WATTS+i*2,   deviceWatts[i]&0xFF);
    EEPROM.write(ADDR_WATTS+i*2+1,(deviceWatts[i]>>8)&0xFF);
  }
  EEPROM.commit();
}
void eepromSaveKWh(){
  EEPROM.write(ADDR_EXT3_KEY,0xDD);
  for(int i=0;i<DEVICE_COUNT;i++){
    EEPROM.write(ADDR_KWH+i*4,   storedWh[i]&0xFF);
    EEPROM.write(ADDR_KWH+i*4+1,(storedWh[i]>>8)&0xFF);
    EEPROM.write(ADDR_KWH+i*4+2,(storedWh[i]>>16)&0xFF);
    EEPROM.write(ADDR_KWH+i*4+3,(storedWh[i]>>24)&0xFF);
  }
  EEPROM.write(ADDR_WEEK_TS,   weekStartUnix&0xFF);
  EEPROM.write(ADDR_WEEK_TS+1,(weekStartUnix>>8)&0xFF);
  EEPROM.write(ADDR_WEEK_TS+2,(weekStartUnix>>16)&0xFF);
  EEPROM.write(ADDR_WEEK_TS+3,(weekStartUnix>>24)&0xFF);
  EEPROM.commit();
}
void eepromSaveSchedTimes(){
  EEPROM.write(ADDR_EXT4_KEY,0xEE);
  for(int i=0;i<DEVICE_COUNT;i++){
    EEPROM.write(ADDR_SCHED_ON_H+i, (byte)schedOnHour[i]);
    EEPROM.write(ADDR_SCHED_ON_M+i, (byte)schedOnMin[i]);
    EEPROM.write(ADDR_SCHED_OFF_H+i,(byte)schedOffHour[i]);
    EEPROM.write(ADDR_SCHED_OFF_M+i,(byte)schedOffMin[i]);
  }
  EEPROM.commit();
}

//  Appliance
void setAppliance(int idx,bool on,bool fromBlynk=false){
  if(idx<0||idx>=DEVICE_COUNT) return;
  if(on&&!dashboardSwitchEnabled[idx]) return;
  if(applianceState[idx]==on) return;
  applianceState[idx]=on;
  if(RELAY_PIN[idx]!=255) digitalWrite(RELAY_PIN[idx],on?RELAY_ON:RELAY_OFF);
  if(on){
    onSinceMs[idx]=millis();
  } else {
    if(onSinceMs[idx]>0&&deviceWatts[idx]>0){
      unsigned long secs=(millis()-onSinceMs[idx])/1000;
      storedWh[idx]+=(uint32_t)((uint64_t)deviceWatts[idx]*secs/3600);
      energyDirty=true;
      energySaveAt=millis()+60000; // Throttled: 1 minute
    }
    timerEndMs[idx]=0; onSinceMs[idx]=0;
  }
  relayStatesDirty=true;
  relaySaveAt=millis()+60000; // Throttled: 1 minute
  if(Blynk.connected()&&!fromBlynk) Blynk.virtualWrite(VPIN[idx],on?1:0);
}

//  Time
void syncNTP(){
  if(timeIsManual||WiFi.status()!=WL_CONNECTED) return;
  unsigned long nowMs=millis();
  time_t now=time(nullptr);
  if(ntpSyncInProgress){
    if(now>=1000000000UL){
      if(!timeAvailable) lastScheduleMinute=-1;
      timeAvailable=true;
      ntpSyncInProgress=false;
      ntpRetryAt=nowMs+3600000UL;
      saveClock(false);
      // Serial.println("NTP OK");
      return;
    }
    if((unsigned long)(nowMs-ntpSyncStartedAt)>=30000UL){
      ntpSyncInProgress=false;
      ntpRetryAt=nowMs+60000UL;
      // Serial.println("NTP request timed out; retry scheduled");
    }
    return;
  }
  if(ntpRetryAt!=0&&(long)(nowMs-ntpRetryAt)<0) return;
  configTime((long)(tzOffset*3600.0f),0,"pool.ntp.org","time.nist.gov","0.pool.ntp.org");
  ntpSyncInProgress=true;
  ntpSyncStartedAt=nowMs;
  if(now>=1000000000UL){
    if(!timeAvailable) lastScheduleMinute=-1;
    timeAvailable=true;
    ntpSyncInProgress=false;
    ntpRetryAt=nowMs+3600000UL;
    saveClock(false);
    // Serial.println("NTP request started with valid system time");
  }
}
void saveClock(bool manual){
  time_t now=time(nullptr);
  if(now<1000000000UL) return;
  uint32_t epoch=(uint32_t)now;
  EEPROM.write(ADDR_CLOCK_MANUAL,manual?0xD2:0xD3);
  for(int i=0;i<4;i++) EEPROM.write(ADDR_CLOCK_EPOCH+i,(uint8_t)(epoch>>(i*8)));
  EEPROM.commit();
}
bool validCalendarTime(int yr,int mo,int dy,int hr,int mn,int sc){
  if(yr<2000||yr>2099||mo<1||mo>12||hr<0||hr>23||mn<0||mn>59||sc<0||sc>59) return false;
  const int daysInMonth[12]={31,28,31,30,31,30,31,31,30,31,30,31};
  int maxDay=daysInMonth[mo-1];
  bool leap=(yr%4==0&&(yr%100!=0||yr%400==0));
  if(mo==2&&leap) maxDay=29;
  return dy>=1&&dy<=maxDay;
}
bool setManualTime(int yr,int mo,int dy,int hr,int mn,int sc){
  if(!validCalendarTime(yr,mo,dy,hr,mn,sc)) return false;
  struct tm t; memset(&t,0,sizeof(t));
  t.tm_year=yr-1900;t.tm_mon=mo-1;t.tm_mday=dy;t.tm_hour=hr;t.tm_min=mn;t.tm_sec=sc;
  time_t tt=mktime(&t); struct timeval tv={tt,0}; settimeofday(&tv,nullptr);
  timeAvailable=true; timeIsManual=true;
  lastScheduleMinute=-1;
  saveClock(true);
  return true;
}
bool setPhoneTime(uint32_t epoch){
  if(epoch<1000000000UL) return false;
  configTime((long)(tzOffset*3600.0f),0,"pool.ntp.org","time.nist.gov","0.pool.ntp.org");
  struct timeval tv={(time_t)epoch,0};
  settimeofday(&tv,nullptr);
  timeAvailable=true;
  timeIsManual=true;
  lastScheduleMinute=-1;
  saveClock(true);
  return true;
}
String getTimeStr(){
  if(!timeAvailable) return "--:--:--";
  time_t n=time(nullptr); struct tm* t=localtime(&n);
  char b[9]; sprintf(b,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec); return b;
}
String getDateStr(){
  if(!timeAvailable) return "No time";
  time_t n=time(nullptr); struct tm* t=localtime(&n);
  const char* d[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  const char* m[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  char b[28]; sprintf(b,"%s %d %s %04d",d[t->tm_wday],t->tm_mday,m[t->tm_mon],t->tm_year+1900);
  return b;
}
String getDateTimeJSON(){
  if(!timeAvailable) return "\"\"";
  time_t n=time(nullptr); struct tm* t=localtime(&n);
  char b[22]; sprintf(b,"%04d-%02d-%02dT%02d:%02d:%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);
  return "\""+String(b)+"\"";
}

//  Timers / Schedule / Switches / WiFi
void checkTimers(){
  for(int i=0;i<DEVICE_COUNT;i++)
    if(timerEndMs[i]>0&&(int32_t)(millis()-timerEndMs[i])>=0){timerEndMs[i]=0;setAppliance(i,false);}
}
void checkSchedule(){
  if(!timeAvailable) return;
  time_t n=time(nullptr); struct tm* t=localtime(&n); if(!t) return;
  int h=t->tm_hour,m=t->tm_min,cm=h*60+m;
  time_t now = time(nullptr);

if(lastScheduleCheckTime == 0){
    lastScheduleCheckTime = now;
}

lastScheduleMinute = cm;
  for(int i=0;i<DEVICE_COUNT;i++){
    if(!schedEnabled[i]) continue;
int onMinute =
    schedOnHour[i] * 60 +
    schedOnMin[i];

int offMinute =
    schedOffHour[i] * 60 +
    schedOffMin[i];

if(cm == onMinute)
    setAppliance(i,true);

if(cm == offMinute)
    setAppliance(i,false);
  }
}
void checkWeekReset(){
  if(!timeAvailable) return;
  time_t now=time(nullptr);
  if(weekStartUnix==0){ weekStartUnix=(uint32_t)now; eepromSaveKWh(); }
  else if((uint32_t)now-weekStartUnix>=604800UL){
    for(int i=0;i<DEVICE_COUNT;i++) storedWh[i]=0;
    weekStartUnix=(uint32_t)now; eepromSaveKWh();
    // Serial.println("Weekly kWh reset");
  }
}
void checkSwitches(){
  const unsigned long SWITCH_DEBOUNCE_MS=150;
  const unsigned long SWITCH_RETRIGGER_MS=600;
  for(int i=0;i<DEVICE_COUNT;i++){
    if(!switchInputEnabled||SWITCH_PIN[i]==255) continue;
    int cur=digitalRead(SWITCH_PIN[i]);
    if(cur!=switchCandidate[i]){
      switchCandidate[i]=cur;
      switchDebounceAt[i]=millis()+SWITCH_DEBOUNCE_MS;
    }else if(cur!=lastSwitchState[i]&&switchDebounceAt[i]!=0&&millis()>=switchDebounceAt[i]){
      switchDebounceAt[i]=0;
      unsigned long now=millis();
      if((long)(now-switchLastChangeAt[i])<SWITCH_RETRIGGER_MS){
        switchDebounceAt[i]=now+(SWITCH_RETRIGGER_MS-(now-switchLastChangeAt[i]));
        continue;
      }
      lastSwitchState[i]=cur;
      switchLastChangeAt[i]=now;
      setAppliance(i,cur==LOW);
    }
  }
}

void startAccessPoint(){
  if(apMode) return;
  WiFi.mode(WiFi.status()==WL_CONNECTED ? WIFI_AP_STA : WIFI_AP);
  if(!WiFi.softAPConfig(apIP,apIP,IPAddress(255,255,255,0))){
    //Serial.println("AP IP configuration failed");
    }
  bool started=WiFi.softAP(apSSID,apPassword,6,apHidden?1:0);
  if(!started){
    apMode=false;
    // Serial.println("AP start failed; will retry");
    return;
  }
  apMode=true;
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsRunning=dnsServer.start(53,"*",apIP);
  // Serial.print("AP"); Serial.print(apHidden?" (hidden)":""); Serial.print(": "); Serial.println(WiFi.softAPIP());
}
void stopAccessPoint(){
  if(!apMode) return;
  if(dnsRunning){dnsServer.stop();dnsRunning=false;}
  WiFi.softAPdisconnect(true); apMode=false; 
  if(WiFi.status()==WL_CONNECTED)
    WiFi.mode(WIFI_STA);
  else
    WiFi.mode(WIFI_OFF);
}
void applyConfiguredWiFiAddress(){
  if(wifiStaticEnabled && wifiStaticIP[0]){
    WiFi.config(wifiStaticIP,wifiGateway,wifiSubnet,wifiDNS);
  }else{
    WiFi.config(IPAddress(0,0,0,0),IPAddress(0,0,0,0),IPAddress(0,0,0,0),IPAddress(0,0,0,0));
  }
}
void checkWiFiAndAP(){
  static bool previousWiFiConnected=false;
  wifiConnected=(WiFi.status()==WL_CONNECTED);
  if(wifiConnected){
    wifiStationProbe=false;
    if(!previousWiFiConnected){
      ntpSyncInProgress=false;
      ntpRetryAt=0;
      blynkRetryAt=0;
    }
    previousWiFiConnected=true;
    wifiReconnectAt=0;
    wifiReconnectDelayMs=1000;
    if (WiFi.status() == WL_CONNECTED &&
    !Blynk.connected() &&
    (blynkRetryAt == 0 || (long)(millis() - blynkRetryAt) >= 0))
{
    blynkRetryAt = millis() + 60000UL; // retry every 60 sec
    Blynk.connect(2000);                  // non-blocking
}
    if(!timeIsManual && Blynk.connected())
    {
        syncNTP();
    }
    if(!apModeEnabled&&apMode) stopAccessPoint();
    else if(apModeEnabled&&!apMode) startAccessPoint();
  } else {
    previousWiFiConnected=false;
    if(!apMode) startAccessPoint();

    if(strlen(wifiSSID)==0){
      wifiStationProbe=false;
    }else if(wifiStationProbe){
      if(WiFi.status()==WL_CONNECTED){
        wifiStationProbe=false;
        wifiConnected=true;
        wifiReconnectAt=0;
        wifiReconnectDelayMs=1000;
        // Serial.println("WiFi reconnected");
      }else if((long)(millis()-wifiReconnectProbeStart)>5000){
        wifiStationProbe=false;
        WiFi.disconnect(true);
        WiFi.mode(apMode?WIFI_AP:WIFI_STA);
        wifiReconnectAt=millis()+wifiReconnectDelayMs;
        if(wifiReconnectDelayMs<300000UL) wifiReconnectDelayMs=min(wifiReconnectDelayMs*2UL,300000UL);
        // Serial.println("WiFi reconnect timed out; retry backoff increased");
      }
    }else if(wifiReconnectAt==0||(long)(millis()-wifiReconnectAt)>=0){
      wifiReconnectAt=0;
      wifiReconnectProbeStart=millis();
      wifiStationProbe=true;
      WiFi.mode(apMode?WIFI_AP_STA:WIFI_STA);
      applyConfiguredWiFiAddress();
      WiFi.begin(wifiSSID,wifiPass);
      // Serial.println("WiFi reconnect probe...");
    }
  }
  if(WiFi.status() == WL_CONNECTED)
  {
    MDNS.update();
  }
}

//  Blynk
BLYNK_CONNECTED(){for(int i=0;i<DEVICE_COUNT;i++)Blynk.virtualWrite(VPIN[i],applianceState[i]?1:0);}
BLYNK_WRITE(V1){setAppliance(0,param.asInt()==1,true);}
BLYNK_WRITE(V2){setAppliance(1,param.asInt()==1,true);}
BLYNK_WRITE(V3){setAppliance(2,param.asInt()==1,true);}
BLYNK_WRITE(V4){setAppliance(3,param.asInt()==1,true);}
BLYNK_WRITE(V5){setAppliance(4,param.asInt()==1,true);}
BLYNK_WRITE(V6){setAppliance(5,param.asInt()==1,true);}

//  Handlers
String dashboardRequestToken(){
  if(server.hasArg("token")) return server.arg("token");
  String authorization=server.header("Authorization");
  if(authorization.startsWith("Bearer ")) return authorization.substring(7);
  return "";
}

bool readIntegerArg(const String& name,long minimum,long maximum,int& value){
  if(!server.hasArg(name)) return false;
  String raw=server.arg(name);
  if(!raw.length()||raw.length()>12) return false;
  const char* start=raw.c_str();
  char* end=nullptr;
  long parsed=strtol(start,&end,10);
  if(end==start||*end!='\0'||parsed<minimum||parsed>maximum) return false;
  value=(int)parsed;
  return true;
}

bool readFloatArg(const String& name,float minimum,float maximum,float& value){
  if(!server.hasArg(name)) return false;
  String raw=server.arg(name);
  if(!raw.length()||raw.length()>24) return false;
  const char* start=raw.c_str();
  char* end=nullptr;
  float parsed=strtof(start,&end);
  if(end==start||*end!='\0'||!(parsed>=minimum&&parsed<=maximum)) return false;
  value=parsed;
  return true;
}

void clearDashboardSession(int slot){
  if(slot<0||slot>=4) return;
  dashboardSessions[slot]="";
  dashboardAdminSessions[slot]="";
  dashboardSessionUsers[slot]="";
  dashboardSessionStartedAt[slot]=0;
}

int dashboardFindSessionSlot(const String& token){
  if(!token.length()) return -1;
  unsigned long now=millis();
  for(int i=0;i<4;i++){
    if(!dashboardSessions[i].length()) continue;
    if((unsigned long)(now-dashboardSessionStartedAt[i])>=DASHBOARD_SESSION_TIMEOUT_MS){
      clearDashboardSession(i);
      continue;
    }
    if(dashboardSessions[i]==token) return i;
  }
  return -1;
}

void invalidateDashboardSessionsForUser(const char* username){
  for(int i=0;i<4;i++){
    if(dashboardSessions[i].length()&&dashboardSessionUsers[i].equals(username)) clearDashboardSession(i);
  }
}

bool dashboardAuthorized(){
  if(!dashboardAuthEnabled) return true;
  String token=dashboardRequestToken();
  return dashboardFindSessionSlot(token)>=0;
}

bool dashboardAdminAuthorized(){
  if(!dashboardAuthEnabled) return true;
  String token=dashboardRequestToken();
  int slot=dashboardFindSessionSlot(token);
  return slot>=0&&dashboardAdminSessions[slot].length()>0;
}

int dashboardSessionUserIndex(){
  String token=dashboardRequestToken();
  int slot=dashboardFindSessionSlot(token);
  if(slot>=0) for(int u=0;u<AUTH_MAX_USERS;u++)
    if(dashboardSessionUsers[slot]==dashboardUsers[u]) return u;
  return -1;
}

bool dashboardCanControlSwitch(int id){
  if(!dashboardAuthEnabled||dashboardAdminAuthorized()) return true;
  int user=dashboardSessionUserIndex();
  return user>0&&id>=0&&id<DEVICE_COUNT&&switchPermissions[user][id];
}

bool dashboardAdminNeedsSetup(){
  return strlen(dashboardUsers[0])==0 || strlen(dashboardPasses[0])==0;
}

String jsonEscape(const char* value){
  String escaped;
  if(!value) return escaped;
  escaped.reserve(strlen(value));
  for(const unsigned char* p=(const unsigned char*)value;*p;p++){
    switch(*p){
      case '"': escaped+="\\\""; break;
      case '\\': escaped+="\\\\"; break;
      case '\b': escaped+="\\b"; break;
      case '\f': escaped+="\\f"; break;
      case '\n': escaped+="\\n"; break;
      case '\r': escaped+="\\r"; break;
      case '\t': escaped+="\\t"; break;
      default:
        if(*p<0x20){char hex[7];snprintf(hex,sizeof(hex),"\\u%04x",*p);escaped+=hex;}
        else escaped+=(char)*p;
    }
  }
  return escaped;
}

void dashboardRequireAuth(){
  if(!dashboardAuthorized()){
    server.sendHeader("WWW-Authenticate","Dashboard session");
    server.send(401,"text/plain","unauthorized");
  }
}

void dashboardRequireAdmin(){
  if(!dashboardAuthorized()){
    dashboardRequireAuth();
  }else if(!dashboardAdminAuthorized()){
    server.send(403,"text/plain","admin access required");
  }
}

void saveDashboardAuth(){
  EEPROM.write(ADDR_AUTH_KEY,0xA1);
  EEPROM.write(ADDR_AUTH_ENABLED,dashboardAuthEnabled?1:0);
  for(int i=0;i<AUTH_MAX_USERS;i++){
    int base=ADDR_AUTH_USERS+i*AUTH_BLOCK_SIZE;
    EEPROM.write(base,(byte)(strlen(dashboardUsers[i])>0?1:0));
    for(int j=0;j<AUTH_USER_NAME_LEN;j++) EEPROM.write(base+1+j,j<(int)strlen(dashboardUsers[i])?dashboardUsers[i][j]:0);
    for(int j=0;j<AUTH_PASS_LEN;j++) EEPROM.write(base+1+AUTH_USER_NAME_LEN+j,j<(int)strlen(dashboardPasses[i])?dashboardPasses[i][j]:0);
  }
  EEPROM.commit();
}

void saveSwitchPermissions(){
  EEPROM.write(ADDR_SWITCH_PERMISSIONS,0xE5);
  for(int i=0;i<AUTH_MAX_USERS;i++)
    for(int s=0;s<DEVICE_COUNT;s++)
      EEPROM.write(ADDR_SWITCH_PERMISSIONS+1+i*DEVICE_COUNT+s,switchPermissions[i][s]?1:0);
  EEPROM.commit();
}

void handleDashboardLogin(){
  if((long)(millis()-loginLockoutUntil)<0){
    server.send(429,"text/plain","too many failed attempts - try again later");return;
  }
  if(!server.hasArg("username")||!server.hasArg("password")){
    server.send(400,"text/plain","bad");return;
  }
  String username=server.arg("username");
  String password=server.arg("password");
  for(int i=0;i<AUTH_MAX_USERS;i++){
    if(strlen(dashboardUsers[i]) && username.equals(dashboardUsers[i]) && password.equals(dashboardPasses[i])){
      char tokenBuffer[33];
      snprintf(tokenBuffer,sizeof(tokenBuffer),"%08lx%08lx%08lx%08lx",
               (unsigned long)ESP.random(),(unsigned long)ESP.random(),
               (unsigned long)ESP.random(),(unsigned long)ESP.random());
      String token=tokenBuffer;
      int slot=-1;
      unsigned long oldestAge=0,now=millis();
      for(int j=0;j<4;j++){
        if(dashboardSessions[j].length()&&
           (unsigned long)(now-dashboardSessionStartedAt[j])>=DASHBOARD_SESSION_TIMEOUT_MS)
          clearDashboardSession(j);
        if(!dashboardSessions[j].length()){slot=j;break;}
        unsigned long age=(unsigned long)(now-dashboardSessionStartedAt[j]);
        if(slot<0||age>oldestAge){slot=j;oldestAge=age;}
      }
      if(slot<0) slot=0;
      dashboardSessions[slot]=token;
      dashboardAdminSessions[slot]="";
      dashboardSessionUsers[slot]=dashboardUsers[i];
      dashboardSessionStartedAt[slot]=now;
      if(i==0) dashboardAdminSessions[slot]=token;
      loginFailCount = 0; // Reset on success
      server.sendHeader("Cache-Control","no-store");
      server.send(200,"application/json","{\"token\":\""+token+"\",\"username\":\""+jsonEscape(username.c_str())+"\"}");
      return;
    }
  }
  loginFailCount++;
  if(loginFailCount >= 5){
    loginFailCount = 0;
    loginLockoutUntil = millis() + 30000UL;
    // Serial.println("Login lockout triggered (5 failed attempts)");
  }
  server.send(401,"application/json","{\"error\":\"Invalid username or password\"}");
}

void handleDashboardLogout(){
  String token=dashboardRequestToken();
  for(int i=0;i<4;i++){
    if(dashboardSessions[i]==token){
      clearDashboardSession(i);
      server.send(200,"application/json","{\"ok\":true}");
      return;
    }
  }
  server.send(200,"application/json","{\"ok\":true}");
}

void handleDashboardSetup(){
  if(!dashboardAdminNeedsSetup() || !server.hasArg("username") || !server.hasArg("password")){
    server.send(400,"text/plain","setup is not available");return;
  }
  if(!server.arg("username").equals(DEFAULT_ADMIN_USER)){server.send(403,"text/plain","invalid setup details");return;}
  String password=server.arg("password");
  if(password.length()<8){server.send(400,"text/plain","password must be at least 8 characters");return;}
  if(password.length()>AUTH_PASS_LEN){server.send(400,"text/plain","password too long");return;}
  strncpy(dashboardUsers[0],"admin",AUTH_USER_NAME_LEN);
  dashboardUsers[0][AUTH_USER_NAME_LEN]='\0';
  strncpy(dashboardPasses[0], password.c_str(), AUTH_PASS_LEN);
  dashboardPasses[0][AUTH_PASS_LEN]='\0';
  dashboardAuthEnabled=true;
  saveDashboardAuth();
  // Serial.println("=== ADMIN SETUP SAVED ===");
  // Serial.print("User: ");
  // Serial.println(dashboardUsers[0]);
  // Serial.print("Pass length: ");
  // Serial.println(strlen(dashboardPasses[0]));
  server.send(200,"text/plain","ok");
}

void handleDashboardAuthStatus(){
  if(dashboardAdminNeedsSetup()){
    server.send(200,"application/json","{\"enabled\":true,\"setup\":true}");
    return;
  }
  if(dashboardAuthorized()){
    server.send(200,"application/json",dashboardAuthEnabled?"{\"enabled\":true}":"{\"enabled\":false}");
  } else server.send(401,"application/json","{\"enabled\":true}");
}

void handleDashboardAuthSave(){
  if(!dashboardAdminAuthorized()){
    dashboardRequireAuth();return;
  }
  bool permissionsChanged=false;
  bool previousAuthEnabled=dashboardAuthEnabled;
  for(int i=1;i<AUTH_MAX_USERS;i++){
    if(!server.hasArg("user"+String(i)))continue;
    String user=server.arg("user"+String(i));user.trim();
    if(!user.length())continue;
    for(int j=i+1;j<AUTH_MAX_USERS;j++){
      if(server.hasArg("user"+String(j))&&user.equals(server.arg("user"+String(j)))){server.send(400,"text/plain","duplicate username");return;}
    }
    for(int j=1;j<AUTH_MAX_USERS;j++){
      if(j!=i&&strlen(dashboardUsers[j])&&user.equals(dashboardUsers[j])){server.send(400,"text/plain","duplicate username");return;}
    }
  }
  for(int i=0;i<AUTH_MAX_USERS;i++){
    String userArg="user"+String(i),passArg="pass"+String(i);
    if(server.hasArg(userArg)){
      String user=server.arg(userArg);user.trim();
      if(i==0){
        if(!user.equals("admin")){server.send(400,"text/plain","admin username cannot be changed");return;}
        strncpy(dashboardUsers[i],"admin",AUTH_USER_NAME_LEN);
        dashboardUsers[i][AUTH_USER_NAME_LEN]='\0';
        if(server.hasArg(passArg)&&server.arg(passArg).length()>=8){
          String pass=server.arg(passArg);
          if(pass.length()>AUTH_PASS_LEN){server.send(400,"text/plain","password too long");return;}
          if(pass!=dashboardPasses[i]) invalidateDashboardSessionsForUser(dashboardUsers[i]);
          strncpy(dashboardPasses[i],pass.c_str(),AUTH_PASS_LEN);
          dashboardPasses[i][AUTH_PASS_LEN]='\0';
        }
        continue;
      }
      if(user.length()==0){
        invalidateDashboardSessionsForUser(dashboardUsers[i]);
        dashboardUsers[i][0]='\0';
        dashboardPasses[i][0]='\0';
        for(int s=0;s<DEVICE_COUNT;s++) switchPermissions[i][s]=false;
        permissionsChanged=true;
        continue;
      }
      if(user.length()>AUTH_USER_NAME_LEN){server.send(400,"text/plain","username too long");return;}
      bool newUser=strlen(dashboardUsers[i])==0;
      String pass=server.arg(passArg);
      bool setPass=server.hasArg(passArg)&&pass.length()>0;
      if(newUser&&(!setPass||pass.length()<8)){
        server.send(400,"text/plain","password must be at least 8 characters");return;
      }
      if(setPass&&pass.length()>AUTH_PASS_LEN){server.send(400,"text/plain","password too long");return;}
      if(setPass&&pass.length()<8){server.send(400,"text/plain","password must be at least 8 characters");return;}
      bool renamed=!newUser&&strcmp(dashboardUsers[i],user.c_str())!=0;
      if(newUser||renamed){
        if(renamed) invalidateDashboardSessionsForUser(dashboardUsers[i]);
        for(int s=0;s<DEVICE_COUNT;s++) switchPermissions[i][s]=false;
        permissionsChanged=true;
      }
      if(!newUser&&setPass&&pass!=dashboardPasses[i])
        invalidateDashboardSessionsForUser(dashboardUsers[i]);
      strncpy(dashboardUsers[i],user.c_str(),AUTH_USER_NAME_LEN);dashboardUsers[i][AUTH_USER_NAME_LEN]='\0';
      if(setPass){
        strncpy(dashboardPasses[i],pass.c_str(),AUTH_PASS_LEN);dashboardPasses[i][AUTH_PASS_LEN]='\0';
      }
    }
  }
  if(server.hasArg("enabled")) dashboardAuthEnabled=server.arg("enabled").toInt()==1;
  if(dashboardAuthEnabled){
    bool hasUser=false;
    for(int i=0;i<AUTH_MAX_USERS;i++) if(strlen(dashboardUsers[i])>0&&strlen(dashboardPasses[i])>0){hasUser=true;break;}
    if(!hasUser){dashboardAuthEnabled=false;server.send(400,"text/plain","Add at least one username and password");return;}
  }
  if(previousAuthEnabled!=dashboardAuthEnabled)
    for(int i=0;i<4;i++) clearDashboardSession(i);
  saveDashboardAuth();
  if(permissionsChanged) saveSwitchPermissions();
  server.send(200,"text/plain","ok");
}

void handleDashboardUsers(){
  if(!dashboardAdminAuthorized()){dashboardRequireAuth();return;}
  String j; j.reserve(300); j="{\"users\":[";
  bool first=true;
  for(int i=0;i<AUTH_MAX_USERS;i++){
    if(i>0&&!strlen(dashboardUsers[i]))continue;
    j+=String(first?"":",")+"{\"index\":"+String(i)+",\"username\":\""+jsonEscape(dashboardUsers[i])+"\",\"admin\":"+String(i==0?"true":"false")+"}";
    first=false;
  }
  j+="]}";
  server.send(200,"application/json",j);
}

void handleSwitchPermissions(){
  if(!dashboardAdminAuthorized()){dashboardRequireAuth();return;}
  int user;
  if(!readIntegerArg("user",1,AUTH_MAX_USERS-1,user)){server.send(400,"text/plain","invalid user");return;}
  bool changed=false;
  bool nextPermissions[DEVICE_COUNT];
  for(int s=0;s<DEVICE_COUNT;s++){
    nextPermissions[s]=switchPermissions[user][s];
    String key="s"+String(s);
    if(server.hasArg(key)){
      int allowed;
      if(!readIntegerArg(key,0,1,allowed)){server.send(400,"text/plain","invalid permission");return;}
      nextPermissions[s]=allowed==1;
    }
  }
  for(int s=0;s<DEVICE_COUNT;s++){
    if(switchPermissions[user][s]!=nextPermissions[s]){switchPermissions[user][s]=nextPermissions[s];changed=true;}
  }
  if(changed) saveSwitchPermissions();
  server.send(200,"text/plain","ok");
}

void handleSwitchPermissionsStatus(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  int user=dashboardSessionUserIndex();
  String j="{\"permissions\":{";
  if(dashboardAdminAuthorized()){
    for(int u=0;u<AUTH_MAX_USERS;u++){
      j+=String(u?",":"")+"\""+String(u)+"\":[";
      for(int s=0;s<DEVICE_COUNT;s++) j+=(s?",":"")+String(switchPermissions[u][s]?1:0);
      j+="]";
    }
  }else{
    j+="\"mine\":[";
    for(int s=0;s<DEVICE_COUNT;s++) j+=(s?",":"")+String(dashboardCanControlSwitch(s)?1:0);
    j+="]";
  }
  j+="}}";
  server.send(200,"application/json",j);
}

void handleRoot(){ server.sendHeader("Cache-Control","no-cache,no-store"); server.send_P(200,"text/html",WEB_HTML); }

void handleStatus(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  String j; j.reserve(4096);
  j="{\"authenabled\":"+String(dashboardAuthEnabled?"true":"false")+",\"admin\":"+String(dashboardAdminAuthorized()?"true":"false")+",";
  auto jb=[&](const char* k,bool v){j+="\"";j+=k;j+="\":";j+=v?"true":"false";j+=",";};
  auto ji=[&](const char* k,int v){j+="\"";j+=k;j+="\":";j+=v;j+=",";};
  auto jl=[&](const char* k,unsigned long v){j+="\"";j+=k;j+="\":";j+=v;j+=",";};
  auto jf=[&](const char* k,float v){j+="\"";j+=k;j+="\":";j+=v;j+=",";};
  auto js=[&](const char* k,const String& v){j+="\"";j+=k;j+="\":\"";j+=jsonEscape(v.c_str());j+="\",";};
  j+="\"access\":[";
  for(int i=0;i<DEVICE_COUNT;i++) j+=(i?",":"")+String(dashboardCanControlSwitch(i)?1:0);
  j+="],";
  jb("wifi",WiFi.status()==WL_CONNECTED);
  jb("blynk",Blynk.connected());
  jb("ntp",timeAvailable&&!timeIsManual);
  jb("manual",timeIsManual);
  js("ip",WiFi.localIP().toString());
  js("wifissid",wifiSSID);
  jb("wifistatic",wifiStaticEnabled);
  js("wifistaticip",(wifiStaticIP[0]==0)?"":wifiStaticIP.toString());
  js("wifigateway",(wifiGateway[0]==0)?"":wifiGateway.toString());
  js("wifisubnet",(wifiSubnet[0]==0)?"":wifiSubnet.toString());
  js("wifidns",(wifiDNS[0]==0)?"":wifiDNS.toString());
  js("time",getTimeStr()); js("date",getDateStr());
  j+="\"datetime\":"; j+=getDateTimeJSON(); j+=",";
  ji("rssi",WiFi.RSSI());
  jf("tz",tzOffset);
  jb("apEnabled",apModeEnabled); jb("apActive",apMode); jb("apHidden",apHidden);
  jb("switchesEnabled",switchInputEnabled);
  ji("night_h",nightOnHour); ji("night_m",nightOnMin);
  ji("morn_h",mornOffHour);  ji("morn_m",mornOffMin);
  jl("uptime",millis()/1000);
  jl("heap",ESP.getFreeHeap());
  js("wx_city",wxCity); js("wx_lat",wxLat); js("wx_lon",wxLon);
  j+="\"relays\":[";  for(int i=0;i<DEVICE_COUNT;i++){j+=applianceState[i]?1:0;if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"modes\":[";  for(int i=0;i<DEVICE_COUNT;i++){j+=momentaryMode[i]?1:0;if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"deviceenabled\":["; for(int i=0;i<DEVICE_COUNT;i++){j+=dashboardSwitchEnabled[i]?1:0;if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"confirmon\":["; for(int i=0;i<DEVICE_COUNT;i++){j+=confirmBeforeOff[i]?1:0;if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"sched\":[";  for(int i=0;i<DEVICE_COUNT;i++){j+=schedEnabled[i]?1:0;if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"timers\":["; for(int i=0;i<DEVICE_COUNT;i++){long rem=0;if(timerEndMs[i]>0){long d2=(long)(timerEndMs[i]-millis());rem=d2>0?d2/1000:0;}j+=rem;if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"watts\":[";  for(int i=0;i<DEVICE_COUNT;i++){j+=deviceWatts[i];if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"onsince\":[";for(int i=0;i<DEVICE_COUNT;i++){unsigned long s=onSinceMs[i]>0?(millis()-onSinceMs[i])/1000:0;j+=s;if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"names\":[";
  for(int i=0;i<DEVICE_COUNT;i++){j+='"';j+=jsonEscape(appName[i]);j+='"';if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"categories\":[";
  for(int i=0;i<DEVICE_COUNT;i++){j+=deviceCategory[i];if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"storedwh\":["; for(int i=0;i<DEVICE_COUNT;i++){j+=storedWh[i];if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"onsince_wh\":[";
  for(int i=0;i<DEVICE_COUNT;i++){
    uint32_t live=0;
    if(onSinceMs[i]>0&&deviceWatts[i]>0){unsigned long s=(millis()-onSinceMs[i])/1000;live=(uint32_t)((uint64_t)deviceWatts[i]*s/3600);}
    j+=live;if(i<DEVICE_COUNT-1)j+=",";
  }
  j+="],\"weekstart\":"; j+=(unsigned long)weekStartUnix; j+=",";
  j+="\"son_h\":["; for(int i=0;i<DEVICE_COUNT;i++){j+=schedOnHour[i]; if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"son_m\":["; for(int i=0;i<DEVICE_COUNT;i++){j+=schedOnMin[i]; if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"soff_h\":[";for(int i=0;i<DEVICE_COUNT;i++){j+=schedOffHour[i];if(i<DEVICE_COUNT-1)j+=",";}
  j+="],\"soff_m\":[";for(int i=0;i<DEVICE_COUNT;i++){j+=schedOffMin[i]; if(i<DEVICE_COUNT-1)j+=",";}
  j+="]}";
  server.sendHeader("Cache-Control","no-cache");
  server.send(200,"application/json",j);
}

void handleSchedTimes(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  int id;
  if(!readIntegerArg("id",0,DEVICE_COUNT-1,id)){server.send(400,"text/plain","bad");return;}
  if(!dashboardCanControlSwitch(id)){server.send(403,"text/plain","no switch access");return;}
  int onh=schedOnHour[id],onm=schedOnMin[id],offh=schedOffHour[id],offm=schedOffMin[id];
  if(server.hasArg("onh")&&!readIntegerArg("onh",0,23,onh)){server.send(400,"text/plain","bad time");return;}
  if(server.hasArg("onm")&&!readIntegerArg("onm",0,59,onm)){server.send(400,"text/plain","bad time");return;}
  if(server.hasArg("offh")&&!readIntegerArg("offh",0,23,offh)){server.send(400,"text/plain","bad time");return;}
  if(server.hasArg("offm")&&!readIntegerArg("offm",0,59,offm)){server.send(400,"text/plain","bad time");return;}
  bool changed=schedOnHour[id]!=onh||schedOnMin[id]!=onm||schedOffHour[id]!=offh||schedOffMin[id]!=offm;
  schedOnHour[id]=onh;schedOnMin[id]=onm;schedOffHour[id]=offh;schedOffMin[id]=offm;
  if(changed){lastScheduleMinute=-1;eepromSaveSchedTimes();}
  server.send(200,"text/plain","ok");
}
void handleToggle(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  int id,st;
  if(!readIntegerArg("id",0,DEVICE_COUNT-1,id)||!readIntegerArg("state",0,1,st)){
    server.send(400,"text/plain","bad");return;
  }
  if(!dashboardCanControlSwitch(id)){server.send(403,"text/plain","no switch access");return;}
  if(st==1&&!dashboardSwitchEnabled[id]){server.send(409,"text/plain","disabled");return;}
  setAppliance(id,st==1); server.send(200,"text/plain","ok");
}
void handleAll(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  int st;
  if(!readIntegerArg("state",0,1,st)){server.send(400,"text/plain","bad state");return;}
  for(int i=0;i<DEVICE_COUNT;i++){
    if(dashboardCanControlSwitch(i)&&(st==0||dashboardSwitchEnabled[i]))setAppliance(i,st==1);
  }
  server.send(200,"text/plain","ok");
}
void handleTimer(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  int id,min;
  if(!readIntegerArg("id",0,DEVICE_COUNT-1,id)||!readIntegerArg("minutes",0,720,min)){
    server.send(400,"text/plain","invalid timer settings");return;
  }
  if(!dashboardCanControlSwitch(id)){server.send(403,"text/plain","no switch access");return;}
  if(min>0&&!dashboardSwitchEnabled[id]){server.send(403,"text/plain","disabled");return;}
  if(min<=0)timerEndMs[id]=0;
else{
    unsigned long durationMs = (unsigned long)min * 60000UL;
    timerEndMs[id] = millis() + durationMs;
    if(!applianceState[id]) setAppliance(id,true);
}
  server.send(200,"text/plain","ok");
}
void handleMode(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int id,mode;
  if(!readIntegerArg("id",0,DEVICE_COUNT-1,id)||!readIntegerArg("mode",0,1,mode)){
    server.send(400,"text/plain","bad");return;
  }
  momentaryMode[id]=mode==1;eepromSaveCore();server.send(200,"text/plain","ok");
}
void handleDeviceEnabled(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int id,enabledValue;
  if(!readIntegerArg("id",0,DEVICE_COUNT-1,id)||!readIntegerArg("enabled",0,1,enabledValue)){
    server.send(400,"text/plain","bad");return;
  }
  bool enabled=enabledValue==1;
  if(!enabled)setAppliance(id,false);
  dashboardSwitchEnabled[id]=enabled;
  eepromSaveCore();
  server.send(200,"text/plain","ok");
}
void handleConfirmOn(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int id,enabled;
  if(!readIntegerArg("id",0,DEVICE_COUNT-1,id)||!readIntegerArg("enabled",0,1,enabled)){
    server.send(400,"text/plain","bad");return;
  }
  confirmBeforeOff[id]=enabled==1;
  eepromSaveCore();
  server.send(200,"text/plain","ok");
}
void handleName(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int id;
  if(!readIntegerArg("id",0,DEVICE_COUNT-1,id)||!server.hasArg("name")){server.send(400,"text/plain","bad");return;}
  String nm=server.arg("name");nm.trim();if(nm.length()==0)nm="Appliance "+String(id+1);
  if(nm.length()>NAME_LEN){server.send(400,"text/plain","name too long");return;}
  if(nm!=appName[id]){strncpy(appName[id],nm.c_str(),NAME_LEN);appName[id][NAME_LEN]='\0';eepromSaveName(id);}
  server.send(200,"text/plain","ok");
}
void handleCategory(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int id,category;
  if(!readIntegerArg("id",0,DEVICE_COUNT-1,id)||!readIntegerArg("category",0,4,category)){
    server.send(400,"text/plain","bad");return;
  }
  if(deviceCategory[id]!=(uint8_t)category){deviceCategory[id]=(uint8_t)category;eepromSaveCore();}
  server.send(200,"text/plain","ok");
}
void handleSched(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int nh=nightOnHour,nm=nightOnMin,mh=mornOffHour,mm=mornOffMin;
  if(server.hasArg("night_h")&&!readIntegerArg("night_h",0,23,nh)){server.send(400,"text/plain","bad time");return;}
  if(server.hasArg("night_m")&&!readIntegerArg("night_m",0,59,nm)){server.send(400,"text/plain","bad time");return;}
  if(server.hasArg("morn_h")&&!readIntegerArg("morn_h",0,23,mh)){server.send(400,"text/plain","bad time");return;}
  if(server.hasArg("morn_m")&&!readIntegerArg("morn_m",0,59,mm)){server.send(400,"text/plain","bad time");return;}
  bool changed=nh!=nightOnHour||nm!=nightOnMin||mh!=mornOffHour||mm!=mornOffMin;
  if(changed){nightOnHour=nh;nightOnMin=nm;mornOffHour=mh;mornOffMin=mm;lastScheduleMinute=-1;eepromSaveCore();}
  server.send(200,"text/plain","ok");
}
void handleSchedApp(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  int id,enabled;
  if(!readIntegerArg("id",0,DEVICE_COUNT-1,id)||!readIntegerArg("enabled",0,1,enabled)){
    server.send(400,"text/plain","bad");return;
  }
  if(!dashboardCanControlSwitch(id)){server.send(403,"text/plain","no switch access");return;}
  if(schedEnabled[id]!=(enabled==1)){schedEnabled[id]=enabled==1;eepromSaveCore();}
  server.send(200,"text/plain","ok");
}
void handleTZ(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  float newOffset;
  if(!readFloatArg("tz",-12.0f,14.0f,newOffset)){server.send(400,"text/plain","bad timezone");return;}
  tzOffset=newOffset;
  eepromSaveCore();ntpSyncInProgress=false;ntpRetryAt=0;syncNTP();server.send(200,"text/plain","ok");
}
void handleForceNTP(){ if(!dashboardAuthorized()){dashboardRequireAuth();return;} if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;} ntpSyncInProgress=false;ntpRetryAt=0;syncNTP();server.send(200,"text/plain","ok"); }
void handleTimeMode(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  if(!server.hasArg("manual")){server.send(400,"text/plain","bad");return;}
  bool m=server.arg("manual").toInt()==1;
  if(!m){timeIsManual=false;ntpSyncInProgress=false;ntpRetryAt=0;syncNTP();}
  server.send(200,"text/plain","ok");
}
void handleSetTime(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int yr,mo,dy,hr,mn,sc;
  if(!readIntegerArg("yr",2000,2099,yr)||!readIntegerArg("mo",1,12,mo)||
     !readIntegerArg("dy",1,31,dy)||!readIntegerArg("hr",0,23,hr)||
     !readIntegerArg("mn",0,59,mn)||!readIntegerArg("sc",0,59,sc)||
     !setManualTime(yr,mo,dy,hr,mn,sc)){
    server.send(400,"text/plain","invalid date or time");return;
  }
  server.send(200,"text/plain","ok");
}
void handleSyncPhoneTime(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  int epoch;
  if(!readIntegerArg("epoch",1000000000L,2147483647L,epoch)||
     !setPhoneTime((uint32_t)epoch)){
    server.send(400,"text/plain","invalid time");return;
  }
  server.send(200,"text/plain","ok");
}
void handleAPMode(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int enabled;
  if(!readIntegerArg("enabled",0,1,enabled)){server.send(400,"text/plain","bad");return;}
  bool en=enabled==1;
  if(apModeEnabled!=en){apModeEnabled=en;if(!en&&apMode&&wifiConnected)stopAccessPoint();eepromSaveCore();}
  server.send(200,"text/plain","ok");
}
void handleAPManual(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int enabled;
  if(!readIntegerArg("on",0,1,enabled)){server.send(400,"text/plain","bad");return;}
  bool on=enabled==1;
  if(on&&!apMode) startAccessPoint();
  else if(!on&&apMode) stopAccessPoint();
  server.send(200,"text/plain","ok");
}
void handleAPHidden(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int hidden;
  if(!readIntegerArg("hidden",0,1,hidden)){server.send(400,"text/plain","bad");return;}
  apHidden=hidden==1;
  eepromSaveCore();
  if(apMode){stopAccessPoint();startAccessPoint();}
  server.send(200,"text/plain","ok");
}
void handleSwitches(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int enabled;
  if(!readIntegerArg("enabled",0,1,enabled)){server.send(400,"text/plain","bad");return;}
  bool ns = (enabled == 1);
  if (ns != switchInputEnabled) {
    switchInputEnabled = ns;
    eepromSaveCore();
  }
  server.send(200,"text/plain","ok");
}
void handleAPSettings(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  if(!server.hasArg("ssid")||!server.hasArg("pass")){server.send(400,"text/plain","bad");return;}
  String newSSID=server.arg("ssid"); newSSID.trim();
  String newPass=server.arg("pass"); newPass.trim();
  if(newSSID.length()==0||newSSID.length()>AP_SSID_LEN){server.send(400,"text/plain","invalid ssid");return;}
  if(newPass.length()<8||newPass.length()>AP_PASS_LEN){server.send(400,"text/plain","password must be 8-16 chars");return;}
  int ip1,ip2,ip3,ip4;
  if(!readIntegerArg("ip1",1,254,ip1)||!readIntegerArg("ip2",0,254,ip2)||
     !readIntegerArg("ip3",0,254,ip3)||!readIntegerArg("ip4",1,254,ip4)){
    server.send(400,"text/plain","invalid AP IP");return;
  }
  strncpy(apSSID,newSSID.c_str(),sizeof(apSSID)); apSSID[AP_SSID_LEN]='\0';
  strncpy(apPassword,newPass.c_str(),sizeof(apPassword)); apPassword[AP_PASS_LEN]='\0';
  apIP=IPAddress(ip1,ip2,ip3,ip4);
  eepromSaveAP();if(apMode){stopAccessPoint();startAccessPoint();}server.send(200,"text/plain","ok");
}
void handleSetWiFi(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  if(!server.hasArg("ssid")){server.send(400,"text/plain","bad");return;}
  String ssid=server.arg("ssid"),pass=server.arg("pass");
  ssid.trim();
  if(!ssid.length()||ssid.length()>WIFI_SSID_LEN||pass.length()>WIFI_PASS_LEN){
    server.send(400,"text/plain","invalid WiFi credentials");return;
  }
  int staticValue=0;
  if(server.hasArg("static")&&!readIntegerArg("static",0,1,staticValue)){
    server.send(400,"text/plain","invalid IP mode");return;
  }
  bool useStatic=staticValue==1;
  IPAddress newIP=wifiStaticIP,newGateway=wifiGateway,newSubnet=wifiSubnet,newDNS=wifiDNS;
  if(useStatic){
    if(!server.hasArg("ip")||!newIP.fromString(server.arg("ip"))||
       !server.hasArg("gateway")||!newGateway.fromString(server.arg("gateway"))||
       !server.hasArg("subnet")||!newSubnet.fromString(server.arg("subnet"))){
      server.send(400,"text/plain","invalid fixed IP settings");return;
    }
    if(server.hasArg("dns")&&server.arg("dns").length()){
      if(!newDNS.fromString(server.arg("dns"))){server.send(400,"text/plain","invalid DNS address");return;}
    }else{
      newDNS=newGateway;
    }
    if(!newIP[0]){server.send(400,"text/plain","invalid fixed IP");return;}
  }
  strncpy(wifiSSID,ssid.c_str(),sizeof(wifiSSID));wifiSSID[WIFI_SSID_LEN]='\0';
  strncpy(wifiPass,pass.c_str(),sizeof(wifiPass));wifiPass[WIFI_PASS_LEN]='\0';
  wifiStaticEnabled=useStatic;
  if(useStatic){wifiStaticIP=newIP;wifiGateway=newGateway;wifiSubnet=newSubnet;wifiDNS=newDNS;}
  else wifiStaticIP=IPAddress(0,0,0,0);
  eepromSaveExt();WiFi.disconnect(true);delay(500);applyConfiguredWiFiAddress();WiFi.begin(wifiSSID,wifiPass);
  wifiStationProbe=true;wifiReconnectProbeStart=millis();
  wifiReconnectAt=0;wifiReconnectDelayMs=1000;
  server.send(200,"text/plain","ok");
}
void handleForgetWiFi(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  wifiSSID[0]='\0';
  wifiPass[0]='\0';
  eepromSaveExt();
  WiFi.disconnect(false);
  wifiStationProbe=false;wifiReconnectAt=0;
  if(apModeEnabled&&!apMode) startAccessPoint();
  server.send(200,"text/plain","ok");
}
void handleWxLoc(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  if(!server.hasArg("lat")||!server.hasArg("lon")){server.send(400,"text/plain","bad");return;}
  strncpy(wxLat, server.arg("lat").c_str(),  sizeof(wxLat));  wxLat[WX_LAT_LEN]='\0';
  strncpy(wxLon, server.arg("lon").c_str(),  sizeof(wxLon));  wxLon[WX_LON_LEN]='\0';
  strncpy(wxCity,server.arg("city").c_str(), sizeof(wxCity)); wxCity[WX_CITY_LEN]='\0';
  eepromSaveExt(); server.send(200,"text/plain","ok");
}
void handlePowerCfg(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  uint16_t nextWatts[DEVICE_COUNT];
  bool changed=false;
  for(int i=0;i<DEVICE_COUNT;i++){
    nextWatts[i]=deviceWatts[i];
    String key="w"+String(i);
    if(server.hasArg(key)){
      int watts;
      if(!readIntegerArg(key,0,9999,watts)){server.send(400,"text/plain","invalid watt value");return;}
      nextWatts[i]=(uint16_t)watts;
      changed|=nextWatts[i]!=deviceWatts[i];
    }
  }
  if(changed){for(int i=0;i<DEVICE_COUNT;i++)deviceWatts[i]=nextWatts[i];eepromSaveWatts();}
  server.send(200,"text/plain","ok");
}
void handleRestart(){ if(!dashboardAuthorized()){dashboardRequireAuth();return;} if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;} server.send(200,"text/plain","restarting"); delay(500); ESP.restart(); }

//  WiFi scan
bool scanStarted=false,scanDone=false;
void handleWiFiScan(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  static unsigned long scanLastAt = 0;
  if (scanStarted && (long)(millis() - scanLastAt) > 30000) {
    scanStarted = false;
    scanDone = false;
    WiFi.scanDelete();
  }
  if(!scanStarted){
    scanDone=false;
    scanStarted=true;
    scanLastAt = millis();
    WiFi.scanDelete();
    WiFi.scanNetworksAsync([](int n){scanDone=true;});
  }
  server.send(200,"application/json","{\"status\":\"scanning\"}");
}
void handleWiFiScanResult(){
  if(!dashboardAuthorized()){dashboardRequireAuth();return;}
  if(!dashboardAdminAuthorized()){server.send(403,"text/plain","admin access required");return;}
  int n=WiFi.scanComplete();
  if(n==-2){
    WiFi.scanDelete();
    scanStarted=false; scanDone=false;
    server.send(503,"application/json","{\"status\":\"error\"}");
    return;
  }
  if(!scanDone||n<0){server.send(200,"application/json","{\"status\":\"scanning\"}");return;}
  String j; j.reserve(1400); j="{\"status\":\"done\",\"networks\":[";
  bool first=true;
  for(int i=0;i<n;i++){
    String ssid=WiFi.SSID(i);
    if(ssid.length()==0) continue;
    if(!first)j+=","; first=false;
    j+="{\"ssid\":\""; j+=jsonEscape(ssid.c_str());
    j+="\",\"rssi\":"; j+=WiFi.RSSI(i);
    j+=",\"enc\":"; j+=(WiFi.encryptionType(i)==ENC_TYPE_NONE?"false":"true");
    j+=",\"ch\":"; j+=WiFi.channel(i); j+="}";
  }
  j+="]}";
  scanStarted=false; scanDone=false;
  WiFi.scanDelete();
  server.send(200,"application/json",j);
}

void handleCaptivePortal(){
  String uri=server.uri();
  if(uri.startsWith("/api/")){server.send(404,"text/plain","not found");return;}
  server.sendHeader("Cache-Control","no-cache,no-store");
  server.send_P(200,"text/html",WEB_HTML);
}

//  Setup
void setup(){
  Serial.begin(115200);
  Serial.end();

    randomSeed(
    ESP.getCycleCount() ^
    micros() ^
    ESP.getFreeHeap()
    );
  // Serial.println("\n=== Smart Home v1.1  NodeMCU ESP8266  6 Relay ===");
  // for(int i=0;i<DEVICE_COUNT;i++){
  //   if(SWITCH_PIN[i]==0||SWITCH_PIN[i]==2||SWITCH_PIN[i]==10)
  //     Serial.println("WARNING: GPIO0/GPIO2/GPIO15 are boot/UART-sensitive; connected switches can prevent boot or affect serial flashing.");
  //   if(RELAY_PIN[i]==0||RELAY_PIN[i]==2||RELAY_PIN[i]==15||RELAY_PIN[i]==10)
  //     Serial.println("WARNING: GPIO0/GPIO2/GPIO15/GPIO10 are ESP8266 boot-strap pins; relay modules can prevent boot.");
  // }
  eepromLoad();

  // Drive every relay HIGH first so active-low relay modules stay off
  // while the ESP8266 boots. This prevents relay backfeeding and startup surges.
  for(int i=0;i<DEVICE_COUNT;i++){
    if(RELAY_PIN[i]!=255){
      pinMode(RELAY_PIN[i],OUTPUT);
      digitalWrite(RELAY_PIN[i],RELAY_OFF);
    }
  }
  for(int i=0;i<DEVICE_COUNT;i++){
    if(SWITCH_PIN[i]!=255){
      pinMode(SWITCH_PIN[i],SWITCH_PIN[i]==16?INPUT:INPUT_PULLUP);
      lastSwitchState[i]=digitalRead(SWITCH_PIN[i]);
      switchCandidate[i]=lastSwitchState[i];
    }
  }
  for(int i=0;i<DEVICE_COUNT;i++){
    if(RELAY_PIN[i]!=255){
      digitalWrite(RELAY_PIN[i],applianceState[i]?RELAY_ON:RELAY_OFF);
    }
  }

  WiFi.persistent(false);
  WiFi.setOutputPower(17.5);
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.hostname("smart-home");
  if(strlen(wifiSSID)>0){applyConfiguredWiFiAddress();WiFi.begin(wifiSSID,wifiPass);wifiStationProbe=true;wifiReconnectProbeStart=millis();}
  Blynk.config(BLYNK_AUTH_TOKEN);
  
  wifiConnected=(WiFi.status()==WL_CONNECTED);

  if(wifiConnected){
    Blynk.connect(100);
    blynkRetryAt=millis()+15000UL;
    ntpRetryAt=0; // syncNTP will fire from timer
  } else {
    // Serial.println("WiFi not yet connected - AP will start via checkWiFiAndAP");
  }
  if(apModeEnabled) startAccessPoint();

  if(MDNS.begin("home")) {
    MDNS.addService("http","tcp",80);
  }

  // Setup OTA Updates
  ArduinoOTA.setHostname("smart-home");
  ArduinoOTA.setPassword("12345678"); // <-- CHANGE THIS

  server.on("/",                   HTTP_GET, handleRoot);
  server.on("/api/auth/status",    HTTP_GET, handleDashboardAuthStatus);
  server.on("/api/auth/login",     HTTP_POST,handleDashboardLogin);
  server.on("/api/auth/logout",    HTTP_POST,handleDashboardLogout);
  server.on("/api/auth/setup",     HTTP_POST,handleDashboardSetup);
  server.on("/api/auth/users",     HTTP_GET, handleDashboardUsers);
  server.on("/api/auth/users",     HTTP_POST,handleDashboardAuthSave);
  server.on("/api/auth/permissions",HTTP_GET, handleSwitchPermissionsStatus);
  server.on("/api/auth/permissions",HTTP_POST,handleSwitchPermissions);
  server.on("/api/status",         HTTP_GET, handleStatus);
  server.on("/api/toggle",         HTTP_POST,handleToggle);
  server.on("/api/all",            HTTP_POST,handleAll);
  server.on("/api/timer",          HTTP_POST,handleTimer);
  server.on("/api/mode",           HTTP_POST,handleMode);
  server.on("/api/device_enabled", HTTP_POST,handleDeviceEnabled);
  server.on("/api/confirm_on",     HTTP_POST,handleConfirmOn);
  server.on("/api/name",           HTTP_POST,handleName);
  server.on("/api/category",       HTTP_POST,handleCategory);
  server.on("/api/sched",          HTTP_POST,handleSched);
  server.on("/api/sched_app",      HTTP_POST,handleSchedApp);
  server.on("/api/sched_times",    HTTP_POST,handleSchedTimes);
  server.on("/api/tz",             HTTP_POST,handleTZ);
  server.on("/api/forceNTP",       HTTP_POST,handleForceNTP);
  server.on("/api/timemode",       HTTP_POST,handleTimeMode);
  server.on("/api/settime",        HTTP_POST,handleSetTime);
  server.on("/api/synctime",       HTTP_POST,handleSyncPhoneTime);
  server.on("/api/apmode",         HTTP_POST,handleAPMode);
  server.on("/api/apmanual",       HTTP_POST,handleAPManual);
  server.on("/api/aphidden",       HTTP_POST,handleAPHidden);
  server.on("/api/switches",       HTTP_POST,handleSwitches);
  server.on("/api/apsettings",     HTTP_POST,handleAPSettings);
  server.on("/api/setwifi",        HTTP_POST,handleSetWiFi);
  server.on("/api/forgetwifi",     HTTP_POST,handleForgetWiFi);
  server.on("/api/wxloc",          HTTP_POST,handleWxLoc);
  server.on("/generate_204",       HTTP_GET,handleRoot);
  server.on("/fwlink",             HTTP_GET,handleRoot);
  server.on("/hotspot-detect.html",HTTP_GET,handleRoot);
  server.on("/connectivity-check.html",HTTP_GET,handleRoot);
  server.on("/ncsi.txt",HTTP_GET,handleRoot);
  server.onNotFound(handleCaptivePortal);
  server.on("/api/powercfg",       HTTP_POST,handlePowerCfg);
  server.on("/api/restart",        HTTP_POST,handleRestart);
  server.on("/api/wifiscan",       HTTP_POST,handleWiFiScan);
  server.on("/api/wifiscanresult", HTTP_GET, handleWiFiScanResult);
  server.begin();
  // Serial.println("Server up  http://home.local  or  http://"+(apMode?WiFi.softAPIP():WiFi.localIP()).toString());

  blynkTimer.setInterval(100L,      checkSwitches);
  blynkTimer.setInterval(1000L,    checkTimers);
  blynkTimer.setInterval(1000L,    checkSchedule);
  blynkTimer.setInterval(5000L,    checkWiFiAndAP);
  blynkTimer.setInterval(3600000L, syncNTP);
  blynkTimer.setInterval(3600000L, checkWeekReset);
}

void loop(){
  static unsigned long lastClockSave=0;
  if(timeAvailable&&millis()-lastClockSave>3600000UL){
    lastClockSave=millis();
    saveClock(timeIsManual);
  }
  if(relayStatesDirty&&relaySaveAt!=0&&(long)(millis()-relaySaveAt)>=0){
    relayStatesDirty=false;
    relaySaveAt=0;
    eepromSaveStates();
  }
  if(energyDirty&&energySaveAt!=0&&(long)(millis()-energySaveAt)>=0){
    energyDirty=false;
    energySaveAt=0;
    eepromSaveKWh();
  }
  if(dnsRunning && apMode) dnsServer.processNextRequest();
  if (Blynk.connected()) {
      Blynk.run();
  }
  ArduinoOTA.handle(); // Added OTA handler
  server.handleClient();
  blynkTimer.run();

static unsigned long lastHeapCheck = 0;

if (millis() - lastHeapCheck > 60000UL)
{
    lastHeapCheck = millis();

    if (ESP.getFreeHeap() < 5000)
    {
        ESP.restart();
    }
}

  yield();
}