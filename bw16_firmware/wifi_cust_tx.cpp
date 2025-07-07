#include "wifi_cust_tx.h"

/*
 * Transmits a raw 802.11 frame with a given length.
 * The frame must be valid and have a sequence number of 0 as it will be set automatically.
 * The frame check sequence is added automatically and must not be included in the length.
 * @param frame A pointer to the raw frame
 * @param size The size of the frame
*/
void wifi_tx_raw_frame(void* frame, size_t length) {
  void *ptr = (void *)**(uint32_t **)(rltk_wlan_info + 0x10);
  void *frame_control = alloc_mgtxmitframe((char*)ptr + 0xae0);

  if (frame_control != 0) {
    update_mgntframe_attrib(ptr, (char*)frame_control + 8);
    memset((void *)*(uint32_t *)((char*)frame_control + 0x80), 0, 0x68);
    uint8_t *frame_data = (uint8_t *)*(uint32_t *)((char*)frame_control + 0x80) + 0x28;
    memcpy(frame_data, frame, length);
    *(uint32_t *)((char*)frame_control + 0x14) = length;
    *(uint32_t *)((char*)frame_control + 0x18) = length;
    dump_mgntframe(ptr, frame_control);
  }
}

/*
 * Transmits a 802.11 deauth frame on the active channel
 * @param src_mac An array of bytes containing the mac address of the sender. The array has to be 6 bytes in size
 * @param dst_mac An array of bytes containing the destination mac address or FF:FF:FF:FF:FF:FF to broadcast the deauth
 * @param reason A reason code according to the 802.11 spec. Optional 
*/
void wifi_tx_deauth_frame(void* src_mac, void* dst_mac, uint16_t reason) {
  DeauthFrame frame;
  memcpy(&frame.source, src_mac, 6);
  memcpy(&frame.access_point, src_mac, 6);
  memcpy(&frame.destination, dst_mac, 6);
  frame.reason = reason;
  wifi_tx_raw_frame(&frame, sizeof(DeauthFrame));
}

/*
 * Transmits a very basic 802.11 beacon with the given ssid on the active channel
 * @param src_mac An array of bytes containing the mac address of the sender. The array has to be 6 bytes in size
 * @param dst_mac An array of bytes containing the destination mac address or FF:FF:FF:FF:FF:FF to broadcast the beacon
 * @param ssid '\0' terminated array of characters representing the SSID
*/
void wifi_tx_beacon_frame(void* src_mac, void* dst_mac, const char *ssid) {
  BeaconFrame frame;
  memcpy(&frame.source, src_mac, 6);
  memcpy(&frame.access_point, src_mac, 6);
  memcpy(&frame.destination, dst_mac, 6);
  for (int i = 0; ssid[i] != '\0'; i++) {
    frame.ssid[i] = ssid[i];
    frame.ssid_length++;
  }
  wifi_tx_raw_frame(&frame, 38 + frame.ssid_length);
}



/*
 * Transmits a 802.11 beacon with WPA2 encryption on the active channel
 * @param src_mac An array of bytes containing the mac address of the sender. The array has to be 6 bytes in size
 * @param dst_mac An array of bytes containing the destination mac address or FF:FF:FF:FF:FF:FF to broadcast the beacon
 * @param ssid '\0' terminated array of characters representing the SSID
*/
void wifi_tx_encrypted_beacon_frame(void* src_mac, void* dst_mac, const char *ssid, uint8_t channel) {
    uint8_t beacon_frame[512];
    int pos = 0;
    
    // 802.11 Header (24 bytes)
    beacon_frame[pos++] = 0x80; // Frame Control: Beacon
    beacon_frame[pos++] = 0x00; // Frame Control flags
    beacon_frame[pos++] = 0x00; // Duration
    beacon_frame[pos++] = 0x00; // Duration
    
    // Destination MAC (broadcast)
    memcpy(&beacon_frame[pos], dst_mac, 6);
    pos += 6;
    
    // Source MAC
    memcpy(&beacon_frame[pos], src_mac, 6);
    pos += 6;
    
    // BSSID (same as source)
    memcpy(&beacon_frame[pos], src_mac, 6);
    pos += 6;
    
    // Sequence Control
    beacon_frame[pos++] = 0x00;
    beacon_frame[pos++] = 0x00;
    
    // Beacon Frame Body
    // Timestamp (8 bytes)
    for (int i = 0; i < 8; i++) {
        beacon_frame[pos++] = 0x00;
    }
    
    // Beacon Interval (100ms = 0x0064)
    beacon_frame[pos++] = 0x64;
    beacon_frame[pos++] = 0x00;
    
    // Capability Information (Privacy bit set for WPA2)
    beacon_frame[pos++] = 0x11; // ESS + Privacy
    beacon_frame[pos++] = 0x14; // Short preamble + PBCC + Channel agility
    
    // SSID Information Element
    beacon_frame[pos++] = 0x00; // SSID IE tag
    uint8_t ssid_len = strlen(ssid);
    beacon_frame[pos++] = ssid_len; // SSID length
    memcpy(&beacon_frame[pos], ssid, ssid_len);
    pos += ssid_len;
    
    // Supported Rates IE
    beacon_frame[pos++] = 0x01; // Supported Rates IE tag
    beacon_frame[pos++] = 0x08; // Length
    beacon_frame[pos++] = 0x82; // 1 Mbps (basic)
    beacon_frame[pos++] = 0x84; // 2 Mbps (basic)
    beacon_frame[pos++] = 0x8b; // 5.5 Mbps (basic)
    beacon_frame[pos++] = 0x96; // 11 Mbps (basic)
    beacon_frame[pos++] = 0x24; // 18 Mbps
    beacon_frame[pos++] = 0x30; // 24 Mbps
    beacon_frame[pos++] = 0x48; // 36 Mbps
    beacon_frame[pos++] = 0x6c; // 54 Mbps
    
    // DS Parameter Set IE (current channel)
    beacon_frame[pos++] = 0x03; // DS Parameter Set IE tag
    beacon_frame[pos++] = 0x01; // Length
    beacon_frame[pos++] = channel; // Current channel
    
    // RSN Information Element (WPA2)
    beacon_frame[pos++] = 0x30; // RSN IE tag
    beacon_frame[pos++] = 0x14; // Length (20 bytes)
    beacon_frame[pos++] = 0x01; // RSN version (1)
    beacon_frame[pos++] = 0x00; // RSN version
    
    // Group Cipher Suite (CCMP)
    beacon_frame[pos++] = 0x00; // OUI
    beacon_frame[pos++] = 0x0F;
    beacon_frame[pos++] = 0xAC;
    beacon_frame[pos++] = 0x04; // CCMP
    
    // Pairwise Cipher Suite Count
    beacon_frame[pos++] = 0x01; // Count (1)
    beacon_frame[pos++] = 0x00;
    
    // Pairwise Cipher Suite (CCMP)
    beacon_frame[pos++] = 0x00; // OUI
    beacon_frame[pos++] = 0x0F;
    beacon_frame[pos++] = 0xAC;
    beacon_frame[pos++] = 0x04; // CCMP
    
    // AKM Suite Count
    beacon_frame[pos++] = 0x01; // Count (1)
    beacon_frame[pos++] = 0x00;
    
    // AKM Suite (PSK)
    beacon_frame[pos++] = 0x00; // OUI
    beacon_frame[pos++] = 0x0F;
    beacon_frame[pos++] = 0xAC;
    beacon_frame[pos++] = 0x02; // PSK
    
    // RSN Capabilities
    beacon_frame[pos++] = 0x00;
    beacon_frame[pos++] = 0x00;
    
    // Extended Supported Rates IE (if needed for 802.11g)
    beacon_frame[pos++] = 0x32; // Extended Supported Rates IE tag
    beacon_frame[pos++] = 0x04; // Length
    beacon_frame[pos++] = 0x0c; // 6 Mbps
    beacon_frame[pos++] = 0x12; // 9 Mbps
    beacon_frame[pos++] = 0x18; // 12 Mbps
    beacon_frame[pos++] = 0x60; // 48 Mbps
    
    wifi_tx_raw_frame(beacon_frame, pos);
}