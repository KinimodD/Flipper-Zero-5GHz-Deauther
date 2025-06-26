![stats](https://hackatime-badge.hackclub.com/U092FFBRR0Q/flipperzero-bw16-deauther)
![stats1](https://hackatime-badge.hackclub.com/U092FFBRR0Q/RTL8720dn-DeautherWithRTOS)

# Flipper Zero 5GHz Deauther
Flipper Zero 5GHz Deauther using the BW16 (RTL8720dn)

I made this project for the [Summer Hackclub](https://summer.hack.club/bc1)


## Features
### Deauthentication
- **2.4Ghz** & **5Ghz** Whole Network Deauthentication Attacks
- MAC Address Selector
- Hidden Network Selector
- 5 Networks Max Simultaneous Deauth
### Web Interface
- Captive Portal Support

And more features coming soon...

## Requirements

- Flipper Zero
- 4 Jumper Wires
- RTL8720dn Black version

## Hardware Connections:

![pinout](https://www.amebaiot.com/wp-content/uploads/2022/07/bw16_typec/P2.png)

### DO NOT CONNECT THE BW16 TO POWER WHILE STILL CONNECTED TO THE 5V FROM THE FLIPPER
Make sure to turn on 5V on GPIO on the Flipper Zero
| Flipper Zero | BW16 |
| ----------- | ----------- |
| 5V | 5V |
| TX | LOG_RX | 
| RX | LOG_TX | 
| GND | GND | 

---

## Screenshots

Main Screen

![main_screen](https://github.com/KinimodD/Flipper-Zero-5GHz-Deauther/blob/main/Screenshots/Main.png)

Deauth Screen

![deauth_screen](https://github.com/KinimodD/Flipper-Zero-5GHz-Deauther/blob/main/Screenshots/Deauth.png)


## DISCLAIMER
This project is for educational purposes only. Do not misuse or do illegal activities with this tool. All things done with this tool are done at your own risk


## Credits
For the BW16 code I based it on [tesa-klebeband's](https://github.com/tesa-klebeband) deauther:

https://github.com/tesa-klebeband/RTL8720dn-Deauther

and added RTOS and a serial communication protocol.


I also used this uart_demo and skeleton_app from [Derek Jamison](https://github.com/jamisonderek) to make the app:

https://github.com/jamisonderek/flipper-zero-tutorials/tree/main/ui/skeleton_app

https://github.com/jamisonderek/flipper-zero-tutorials/tree/main/gpio/uart_demo

For the DNS I used [gorebrau's](https://github.com/gorebrau) delfyRTL:
https://github.com/gorebrau/delfyRTL

For the serial interface I used this:
https://forum.arduino.cc/t/serial-input-basics-updated/382007/

## Licence
This project is licensed under the GNU License. Feel free to modify and contribute.
