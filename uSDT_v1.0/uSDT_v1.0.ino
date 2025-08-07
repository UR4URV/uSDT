//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
//
//  Author of this development: UR4URV
//  Date: 30.07.2025
//
//  A library from the https://github.com/kgoba/ft8_lib repository was used to encode FT8.
//
//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------

#include <WiFi.h>
#include <WiFiUdp.h>
#include <NetworkClient.h>
#include <NTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
//
#include <SPI.h>
#include <Wire.h>
#include <si5351.h>
#include <RTClib.h>
#include <TimeLib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <Encoder.h>

//---------------------------------

const char *ssid = "<...>";     //WiFi SSID
const char *password = "<...>"; //WiFi password

char callsign[10] = "<...>";    //Your callsign
char location[10] = "<...>";    //Your QTH

//---------------------------------

#define FIRMWARE_VERSION 1.0
#define MCU_MODEL "ESP32C3"

#define I2C_SDA 8
#define I2C_SCL 9
//
#define ENCODER_PIN_A 0 
#define ENCODER_PIN_B 1
#define BUTTON_PIN 2
//
#define RELAY_PIN 7
#define RF_OUTPUT_PIN 6
#define RF_METER_PIN A5
//
#define AUDIO_OUTPUT_PIN 4
#define AUDIO_INPUT_PIN A3
//
#define ONEWIRE_PIN 10
//
#define UART_RX 20
#define UART_TX 21

#define SCREEN_WIDTH 128    // OLED display width, in pixels (default: 128)
#define SCREEN_HEIGHT 64    // OLED display height, in pixels (default: 32)
#define SCREEN_ROTATION 0   // Rotates text on OLED 1=90 degrees, 2=180 degrees
//
#define OLED_RESET     -1   // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C // See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

//---------------------------------

Encoder encoder_button(ENCODER_PIN_A, ENCODER_PIN_B);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

RTC_DS1307 rtc;
DateTime date_time;

Si5351 si5351;

OneWire ds(ONEWIRE_PIN);
//
byte onewire_addr[8];
byte onewire_data[9];
float onewire_temp = 0;
bool status_onewire = false;

WebServer web_server(80);

WiFiUDP ntp_udp;
NTPClient ntp_client(ntp_udp, "pool.ntp.org", 10800, 60000); //europe.pool.ntp.org

#define SERIAL_SPEED 115200
#define SERIAL_RESERVE 100
String serial_data = "";
bool serial_end = false;

bool status_oled = false;
bool status_ntp = false;
bool status_rtc = false;
bool status_si5351 = false;

//Variable frequencies by default
int freq_main = 14074;    // Freq in kHz
int freq_correct = 1080;  // Freq in Hz

//Module si5351 calibration factor
int32_t cal_factor = 175800;

int level_rx = -20; // dBm
int level_tx = 150; // dBm  (value: 0...255)

int time_shift = 0;     //Shift time for send messages in sec (default: 0)
int time_interval = 30; //Interval fir send messages in sec (default: 15s or 30s)

#define COUNT_DEFAULT 3
int count_tx = 0;
bool status_tx = false;
bool show_status_tx = false;

char rem_callsign[10];

char mode_tx[10] = "FT8";

#define FT8_SYMBOL_COUNT 79    // Symbol count in message
#define FT8_TONE_SPACING 625   // ~6.25 Hz
#define FT8_TONE_DELAY   159   // Delay value for FT8
//
char ft8_message[FT8_SYMBOL_COUNT];
uint8_t ft8_tones[FT8_SYMBOL_COUNT];

#include <JTEncode.h>
JTEncode jtencode;
//
#define WSPR_SYMBOL_COUNT 162  // Symbol count in message
#define WSPR_TONE_SPACING 146  // ~1.46 Hz
#define WSPR_TONE_DELAY   683  // Delay value for WSPR
//
uint8_t wspr_tones[WSPR_SYMBOL_COUNT];

//---------------------------------
//---------------------------------
int encoder(char *message, uint8_t *tones, int is_ft4);

//---------------------------------
//---------------------------------
void ft8_encode_message() {
  Serial.print("FT8 message: ");
  Serial.println(ft8_message);
  //
  //jtencode.ft8_encode(ft8_message, ft8_tones);
  encoder(ft8_message, ft8_tones, 0);
}

//---------------------------------
void wspr_encode_message() {
  Serial.print("Encoded WSPR message... ");
  //
  jtencode.wspr_encode(callsign, location, level_tx, wspr_tones);
  //
  Serial.println("done.");
}

//---------------------------------
void ft8_tx(uint8_t *ft8_tones)
{
  int symbol_count = FT8_SYMBOL_COUNT;
  int tone_spacing = FT8_TONE_SPACING;
  int tone_delay = FT8_TONE_DELAY;

  Serial.print("Start TX mode FT8... ");

  digitalWrite(RELAY_PIN, HIGH);
  //
  si5351.set_clock_pwr(SI5351_CLK0, 1);
  si5351.output_enable(SI5351_CLK0, 1);
  //
  analogWrite(RF_OUTPUT_PIN, level_tx);  

  for (uint8_t i = 0; i < symbol_count; i++)
  {
    si5351.set_freq(((freq_main * 1000) + freq_correct) * 100ULL + (ft8_tones[i] * tone_spacing), SI5351_CLK0);
    delay(tone_delay);
  }

  analogWrite(RF_OUTPUT_PIN, 0);
  //
  si5351.set_clock_pwr(SI5351_CLK0, 0);
  si5351.output_enable(SI5351_CLK0, 0);
  //
  digitalWrite(RELAY_PIN, LOW);

  Serial.println("done.");
}

//---------------------------------
void wspr_tx(uint8_t *wspr_tones)
{
  int symbol_count = WSPR_SYMBOL_COUNT;
  int tone_spacing = WSPR_TONE_SPACING;
  int tone_delay = WSPR_TONE_DELAY;

  Serial.print("Start TX mode WSPR... ");

  digitalWrite(RELAY_PIN, HIGH);

  si5351.set_clock_pwr(SI5351_CLK0, 1);
  si5351.output_enable(SI5351_CLK0, 1);
  //
  for (uint8_t i = 0; i < symbol_count; i++)
  {
    si5351.set_freq(((freq_main * 1000) + freq_correct) * 100ULL + (wspr_tones[i] * tone_spacing), SI5351_CLK0);
    delay(tone_delay);
  }

  // Turn off the output
  si5351.set_clock_pwr(SI5351_CLK0, 0);
  si5351.output_enable(SI5351_CLK0, 0);

  digitalWrite(RELAY_PIN, LOW);

  Serial.println("done.");
}

//---------------------------------
void get_rtc() {
  if (rtc.isrunning()) {
    date_time = rtc.now();
    //
    status_rtc = true;
    Serial.print("Get RTC timestamp: ");   
    Serial.println(date_time.timestamp());
  } else {
    status_rtc = false;
    Serial.println("RTC is NOT running!");
  }
}

//---------------------------------
void set_rtc(DateTime dt) {
  Serial.print("Set RTC timestamp: ");   
  Serial.println(dt.timestamp());
  //
  rtc.adjust(dt);
}

//---------------------------------
void get_ntp(bool update_rtc) {
  Serial.print("Update from NTP server: ");
  if (ntp_client.update()) {
    status_ntp = true;
    Serial.println("OK");
    //
    if(ntp_client.isTimeSet()) {
      if (update_rtc && status_rtc) {
        set_rtc(ntp_client.getEpochTime());
      }
      //
      Serial.print("Time from NTP server: ");
      Serial.println(ntp_client.getFormattedTime());
    } else {
      status_ntp = false;
      Serial.println("Not get time from NTP server!");
    }
  } else {
    Serial.println("ERROR");
  }
}

//---------------------------------------
//---------------------------------------
void web_main_html() {
  String message = "<!DOCTYPE html><html><head>\n";
  message += "<title>uSDT_v1.0</title>\n";
  message += "<meta http-equiv=\"refresh\" content=\"15\">\n";
  //
  message += "<style>\n";
  message += "body {background-color: lightgray;}\n";
  message += "table {border-spacing: 15px;}\n";
  message += "tr,td {border:1px solid black; border-radius: 10px;}\n";
  message += "td {padding: 15px; height: 300px; width: 500px;}\n";
  message += ".input {padding: 2px; font-size: 14px;}\n";
  message += ".button {padding: 12px 35px; text-align: center; font-size: 16px;}\n";
  message += "</style>\n";
  //
  message += "</head><body>\n";
  message += "<table><tr><td>\n";
  message += "<form action=\"/config\">\n";
  //
  message += "Select mode TX: \n";
  message += "<select id=\"mode_tx\" name=\"mode_tx\">\n";
  message += "<option value=\"FT8\" selected>FT8</option>\n";
  message += "<option value=\"WSPR\">WSPR</option>\n";
  message += "</select><br>\n";
  //
  message += "Select band: \n";
  message += "<select id=\"band\" name=\"band\">\n";
  message += "<option value=\"3573\">80m</option>\n";
  message += "<option value=\"5357\">60m</option>\n";
  message += "<option value=\"7074\">40m</option>\n";
  message += "<option value=\"10136\">30m</option>\n";
  message += "<option value=\"14074\" selected>20m</option>\n";
  message += "</select><br>\n";
  //
  message += "Frequency: ";
  message += "<input class=\"input\" type=\"number\" id=\"freq_main\" name=\"freq_main\" min=\"1000\" max=\"100000\" step=\"1\" value=\"";
  message += freq_main;
  message += "\"> kHz<br>\n";
  message += "<i><small>(Freq for FT8: 3573, 5357, 7074, 10136, 14074 kHz)</small></i><br>\n";
   message += "<i><small>(Freq for WSPR: 3568.6, 5287.2, 5364.7, 7038.6, 10138.7, 13553.9, 14095.6 kHz)</small></i><br>\n"; 
  message += "<br>\n";
  //
  message += "Frequency correct: ";
  message += "<input class=\"input\" type=\"number\" id=\"freq_correct\" name=\"freq_correct\" min=\"0\" max=\"5000\" step=\"1\" value=\"";
  message += freq_correct;
  message += "\"> Hz<br>\n";
  message += "<i><small>(Freq correct: 0..5000Hz)</small></i>\n";
  message += "<br><br>\n";
  //
  message += "Calibration factor: ";
  message += "<input class=\"input\" type=\"number\" id=\"cal_factor\" name=\"cal_factor\" step=\"1\" value=\"";
  message += cal_factor;
  message += "\"><br><br>\n";
  //
  message += "Time shift: ";
  message += "<select class=\"input\" id=\"time_shift\" name=\"time_shift\">\n";
  message += "<option value=\"0\""; if (time_shift == 0) message += " selected"; message += ">0 sec</option>\n";
  message += "<option value=\"15\""; if (time_shift == 15) message += " selected"; message += ">15 sec</option>\n"; 
  message += "</select><br>\n";

  message += "Time interval: ";
  message += "<select class=\"input\" id=\"time_interval\" name=\"time_interval\">\n";
  message += "<option value=\"15\""; if (time_interval == 15) message += " selected"; message += ">only 15 sec</option>\n";
  message += "<option value=\"30\""; if (time_interval == 30) message += " selected"; message += ">only 30 sec</option>\n";
  message += "<option value=\"60\""; if (time_interval == 60) message += " selected"; message += ">only 60 sec</option>\n";
  message += "<option value=\"120\""; if (time_interval == 120) message += " selected"; message += ">only 120 sec</option>\n";
  message += "<option value=\"180\""; if (time_interval == 180) message += " selected"; message += ">only 180 sec</option>\n";
  message += "</select><br><br>\n";
  //
  message += "Count transmit messages: ";  
  message += "<input class=\"input\" type=\"number\" id=\"count_tx\" name=\"count_tx\" min=\"0\" max=\"50\" step=\"1\" value=\"";
  message += count_tx;
  message += "\"><br><br>\n";
  //
  message += "Module status: ";
  if (status_tx) {
    message += "<b style=\"color:red;\">TX</b>";
  } else {
    message += "<b style=\"color:blue;\">RX</b>";
  };
  message += "<br><br>\n";
  //
  message += "Status NTP: ";
  if (status_ntp) {
    message += "<b style=\"color:green;\">OK</b>";
  } else {
    message += "<b style=\"color:red;\">ERROR</b>";
  };
  message += "<br>\n";
  //
  message += "Status RTC: ";
  if (status_rtc) {
    message += "<b style=\"color:green;\">OK</b>";
  } else {
    message += "<b style=\"color:red;\">ERROR</b>";
  };
  message += "<br>\n";
  //
  message += "Temperature: ";
  if (status_onewire) {
    message += onewire_temp;
    message += " C";
  } else {
    message += "onewire device not found!";
  };
  message += "<br><br>\n";
  //
  message += "<input class=\"button\" type=\"submit\" value=\"Save\">\n";
  message += "<input class=\"button\" type=\"reset\" value=\"Reset\">\n";
  message += "</form>\n";
  //
  message += "</td><td>\n";
  //
  message += "<form action=\"/config\">\n";
  //
  message += "Callsign: ";
  message += "<input class=\"input\" type=\"text\" id=\"callsign\" name=\"callsign\" size=\"10\" value=\"";
  message += callsign;
  message += "\"> ";
  message += "<a href=\"https://pskreporter.info/pskmap?callsign="; message += callsign; message += "&search=Find\" target=\"_blank\">Show in pskreporter.info</a>";
  message += "<br>\n";
  //
  message += "Location: ";
  message += "<input class=\"input\" type=\"text\" id=\"location\" name=\"location\" size=\"8\" value=\"";
  message += location;
  message += "\"><br><br>\n";
  //
  message += "Remote callsign: ";
  message += "<input class=\"input\" type=\"text\" id=\"rem_callsign\" name=\"rem_callsign\" size=\"10\" value=\"";
  message += rem_callsign;
  message += "\"> ";
  message += "<a href=\"https://www.qrzcq.com/call/"; message += rem_callsign; message += "\" target=\"_blank\">Show in qrzcq.com</a>";
  message += "<br><br>\n";
  //
  message += "Level RX signal: ";
  message += "<input class=\"input\" type=\"number\" id=\"level_rx\" name=\"level_rx\" min=\"-50\" max=\"50\" step=\"1\" value=\"";
  message += level_rx;
  message += "\"> (dB)<br>\n";
  //
  message += "Level TX signal: ";
  message += "<input class=\"input\" type=\"number\" id=\"level_tx\" name=\"level_tx\" min=\"0\" max=\"255\" step=\"1\" value=\"";
  message += level_tx;
  message += "\"> (dB) (value: 0...255)<br><br>\n";
  //

  String txt_level_rx;
  if (level_rx < 0) txt_level_rx = "-"; else txt_level_rx = "+";
  if ((level_rx > -10) && (level_rx < 10)) txt_level_rx += "0";
  txt_level_rx += abs(level_rx);

  String web_msg[8];
  web_msg[1] = String("") + "CQ " + callsign + " " + location;
  web_msg[2] = String("") + rem_callsign + " " + callsign + " " + location;
  web_msg[3] = String("") + rem_callsign + " " + callsign + " " + txt_level_rx;
  web_msg[4] = String("") + rem_callsign + " " + callsign + " R" + txt_level_rx;
  web_msg[5] = String("") + rem_callsign + " " + callsign + " RRR";
  web_msg[6] = String("") + rem_callsign + " " + callsign + " RR73";
  web_msg[7] = String("") + rem_callsign + " " + callsign + " 73";
  //
  message += "Message FT8:<br>\n";
  for (int i = 1; i < 8; i++) {
    message = message + "<input type=\"radio\" id=\"radio_" + i + "\" name=\"select_message\" value=\"";
    message = message + web_msg[i] + "\" onchange=\"input_message.value=select_message.value\">";
    message = message + "<label for=\"radio_" + i + "\">" + web_msg[i] + "</label><br>\n";
  }
  //
  message += "<input type=\"radio\" id=\"radio_0\" name=\"select_message\" value=\"\" checked onchange=\"input_message.value=select_message.value\">\n";
  message += "<input class=\"input\" type=\"text\" id=\"input_message\" name=\"input_message\" size=\"20\" value=\"";  
  message += ft8_message;
  message += "\"><br>\n";
  message += "<i><small>(Max 13 characters for message)</small></i>\n";
  message += "<br><br><br><br><br>\n";
  //
  message += "<input class=\"button\" type=\"submit\" value=\"Save\">\n";
  message += "<input class=\"button\" type=\"reset\" value=\"Reset\">\n";
  message += "</form>\n";
  //
  message += "</td></tr></table>\n";
  message += "<br>\n";
  //
  message += "Time: ";
  message += date_time.timestamp();
  message += "<br>\n";
  //
  message += "</body></html>";
  //
  web_server.send(200, "text/html", message);
}

//---------------------------------------
void web_config_html() {
  if (web_server.method() == HTTP_GET) {
    for (uint8_t i = 0; i < web_server.args(); i++) {
      if (web_server.argName(i) == "mode_tx") {
        web_server.arg(i).toCharArray(mode_tx, 10);
        //
        Serial.print("Set mode TX: ");
        Serial.println(mode_tx);
      }
      if (web_server.argName(i) == "freq_main") {
        freq_main = web_server.arg(i).toInt();
        //
        Serial.print("Set freq_main: ");
        Serial.print(freq_main);
        Serial.println(" kHz");
      }
      if (web_server.argName(i) == "freq_correct") {
        freq_correct = web_server.arg(i).toInt();
        //
        Serial.print("Set freq_correct: ");
        Serial.print(freq_correct);
        Serial.println(" kHz");
      }
      //
      if (web_server.argName(i) == "cal_factor") {
        cal_factor = web_server.arg(i).toInt();
        //
        Serial.print("Set calibration factor: "); 
        Serial.println(cal_factor);
      }
      //
      if (web_server.argName(i) == "time_shift") {
        time_shift = web_server.arg(i).toInt();
        //
        Serial.print("Set time shift: "); 
        Serial.println(time_shift);
      }
      //
      if (web_server.argName(i) == "time_interval") {
        time_interval = web_server.arg(i).toInt();
        //
        Serial.print("Set time interval: "); 
        Serial.println(time_interval);
      }
      //
      if (web_server.argName(i) == "count_tx") {
        count_tx = web_server.arg(i).toInt();
        //
        if (count_tx > 0) { status_tx = true; }
        //
        Serial.print("Set count TX messages: "); 
        Serial.println(count_tx);
      }
      //
      if (web_server.argName(i) == "callsign") {
        web_server.arg(i).toCharArray(callsign, 8);
        //
        Serial.print("Set callsign: "); 
        Serial.println(callsign);        
      }
      if (web_server.argName(i) == "location") {
        web_server.arg(i).toCharArray(location, 6);
        //
        Serial.print("Set location: "); 
        Serial.println(location); 
      }
      //
      if (web_server.argName(i) == "rem_callsign") {
        web_server.arg(i).toCharArray(rem_callsign, 8);
        //
        Serial.print("Set remote callsign: "); 
        Serial.println(rem_callsign); 
      }
      if (web_server.argName(i) == "level_rx") {
        level_rx = web_server.arg(i).toInt();
        //
        Serial.print("Set message RX level: "); 
        Serial.print(level_rx);
        Serial.println(" dB");
      }
      if (web_server.argName(i) == "level_tx") {
        level_tx = web_server.arg(i).toInt();
        //
        Serial.print("Set message TX level: "); 
        Serial.print(level_tx);
        Serial.println(" dB");
      }
      //
      if (web_server.argName(i) == "input_message") {
        web_server.arg(i).toCharArray(ft8_message, FT8_SYMBOL_COUNT);
        //
        ft8_encode_message();
        //
        // Serial.print("Set FT8 message: "); 
        // Serial.println(ft8_message);
      }
    }
    //
    String message = "<!DOCTYPE html><html><head>\n";
    message += "<title>uSDT_v1.0</title>\n";
    message += "<meta http-equiv=\"refresh\" content=\"0; url=/\"/>\n";
    message += "</head><body>\n";
    message += "</body></html>";
    //
    web_server.send(200, "text/html", message);
  }
}

//---------------------------------------
void web_not_found_html() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += web_server.uri();
  message += "\nMethod: ";
  message += (web_server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += web_server.args();
  message += "\n";

  for (uint8_t i = 0; i < web_server.args(); i++) {
    message += " " + web_server.argName(i) + ": " + web_server.arg(i) + "\n";
  }

  web_server.send(404, "text/plain", message);
}

//---------------------------------------
void web_init() {
  Serial.print("Init MDNS responder: ");
  if (MDNS.begin("uSDT_v1.0")) {
    Serial.println("OK");
  } else {
    Serial.println("ERROR");    
  }

  web_server.on("/", web_main_html);
  web_server.on("/config", web_config_html);
  web_server.onNotFound(web_not_found_html);
  
  Serial.print("Init HTTP server... ");
  web_server.begin();
  Serial.println("done.");
}

//---------------------------------------
void serial_print_time() {
  Serial.print("[");
  if (date_time.hour() < 10) Serial.print("0");
  Serial.print(date_time.hour());
  //
  Serial.print(":");
  //
  if (date_time.minute() < 10) Serial.print("0");
  Serial.print(date_time.minute());
  //
  Serial.print(":");
  //
  if (date_time.second() < 10) Serial.print("0");
  Serial.print(date_time.second());
  Serial.print("] ");
}

//---------------------------------------
void display_print(uint8_t col, uint8_t lin, const GFXfont *font, uint8_t textSize, const char *msg) {
  display.setFont(font);
  display.setTextSize(textSize);
  display.setCursor(col,lin);
  display.print(msg);
}

//---------------------------------------
void show_main() {
  display.clearDisplay();

  display_print(0, 0, NULL, 2, mode_tx);
  //
  if (show_status_tx) {
    display_print(100, 0, NULL, 2, "TX");
  } else {
    display_print(100, 0, NULL, 2, "RX");
  }

  display_print(0, 16, NULL, 2, "");
  display.print(freq_main);
  display.print(" kHz");

  int hours = date_time.hour();
  int minutes = date_time.minute();
  int seconds = date_time.second();
  //
  display_print(0, 35, NULL, 1, "Time RTC: ");
  if (hours < 10) display.print("0");
  display.print(hours);
  display.print(":");
  if (minutes < 10) display.print("0");
  display.print(minutes);
  display.print(":");
  if (seconds < 10) display.print("0");
  display.print(seconds);

  //if (status_tx) {
    display_print(0, 45, NULL, 1, "Count TX: ");
    display.print(count_tx);
    display_print(0, 55, NULL, 1, "Ms:");
    display.print(ft8_message);
  //}

  display.display();
}

//---------------------------------------
void get_serial() {
  while (Serial.available()) {
    char serial_char = (char)Serial.read();
    serial_data += serial_char;
    //
    //if (serial_char == ';' || serial_char == '\n') {
    if (serial_char == ';') {
      serial_end = true;
    }
  }

  if (serial_end) {
    //Serial.println(serial_data);

    String freq_string = String(freq_main);
    int freq_len = freq_string.length();

    //Get ID
    if((serial_data[0] == 'I') && (serial_data[1] == 'D') && (serial_data[2] == ';')) {
      Serial.print("ID020;");
    }

    //Get freq
    if((serial_data[0] == 'F') && (serial_data[1] == 'A') && (serial_data[2] == ';')) {
      //Specify the frequency in Hz (11-digit).
      Serial.print("FA");
      for (int i = 0; i < (8 - freq_len); i++) {
        Serial.print("0");
      }
      Serial.print(freq_main);
      Serial.print("000;");
    }

    //Set freq
    if((serial_data[0] == 'F') && (serial_data[1] == 'A') && (serial_data[13] == ';')) {
      Serial.print(serial_data);
      //
      serial_data.remove(0, 2);
      serial_data.remove(11);
      //Serial.print(serial_data);
      //
      freq_main = serial_data.toInt() / 1000;
    }

    //Set mode RX
    if((serial_data[0] == 'R') && (serial_data[1] == 'X') && (serial_data[2] == ';')) {
      Serial.print("RX0;");
      //
      count_tx = 0;
      status_tx = false;
    }

    //Set mode TX;
    if((serial_data[0] == 'T') && (serial_data[1] == 'X') && (serial_data[2] == ';')) {
      Serial.print("TX0;");
      //
      count_tx = 1;
      status_tx = true;
    }

    //Set mode TX0 or TX1 or TX2 (default mode TX0)
    if((serial_data[0] == 'T') && (serial_data[1] == 'X') && (serial_data[3] == ';')) {
      Serial.print("TX0;");
      //
      //count_tx = 1;
      status_tx = true;
      //
      ft8_encode_message();
      ft8_tx(ft8_tones);
      //
      count_tx = 0;
      status_tx = false;
    }

    //Power status
    if((serial_data[0] == 'P') && (serial_data[1] == 'S') && (serial_data[2] == ';')) Serial.print("PS1;");
    if((serial_data[0] == 'P') && (serial_data[1] == 'S') && (serial_data[2] == '1')) Serial.print("");


    //Retrieves the transceiver status.
    if((serial_data[0] == 'I') && (serial_data[1] == 'F') && (serial_data[2] == ';')) {
      //Specify the frequency in Hz (11-digit).
      Serial.print("IF");
      for (int i = 0; i < (8 - freq_len); i++) {
        Serial.print("0");
      }
      Serial.print(freq_main);
      Serial.print("000");
      //
      Serial.print("00000");    //5 spaces for the TS-480.
      Serial.print("00000");    //RIT/ XIT frequency ±9990 in Hz
      Serial.print("000");      //0: RIT OFF, 1: RIT ON //0: XIT OFF, 1: XIT ON //Always 0 for the TS-480 (Memory channel bank number).
      Serial.print("00");       //Memory channel number (00 ~ 99).
      if (status_tx) { Serial.print("1"); } else { Serial.print("0"); }  //0: RX, 1: TX
      Serial.print("00000000;");
    }

    //Sets or reads the Auto Information (AI) function ON/ OFF.
    if((serial_data[0] == 'A') && (serial_data[1] == 'I') && (serial_data[2] == ';')) Serial.print("AI0;");
    if((serial_data[0] == 'A') && (serial_data[1] == 'I') && (serial_data[3] == ';')) Serial.print("AI0;");

    //Recalls or reads the operating mode status.
    if((serial_data[0] == 'M') && (serial_data[1] == 'D') && (serial_data[2] == ';')) Serial.print("MD0;");
    if((serial_data[0] == 'M') && (serial_data[1] == 'D') && (serial_data[3] == ';')) Serial.print("MD0;");

    //Sets or reads the AF gain.
    if((serial_data[0] == 'A') && (serial_data[1] == 'G') && (serial_data[2] == '0')) Serial.print("AG0;");

    //Sets or reads the XIT function status.
    if((serial_data[0] == 'X') && (serial_data[1] == 'T') && (serial_data[2] == ';')) Serial.print("XT0;");
    if((serial_data[0] == 'X') && (serial_data[1] == 'T') && (serial_data[3] == ';')) Serial.print("XT0;");

    //Sets or reads the RIT function status.
    if((serial_data[0] == 'R') && (serial_data[1] == 'T') && (serial_data[2] == ';')) Serial.print("RT0;");
    if((serial_data[0] == 'R') && (serial_data[1] == 'T') && (serial_data[3] == ';')) Serial.print("RT0;");

    //Clears the RIT offset frequency.
    //if((serial_data[0] == 'R') && (serial_data[1] == 'C') && (serial_data[2] == ';')) Serial.print("RC;");

    //
    //if((serial_data[0] == 'F') && (serial_data[1] == 'L') && (serial_data[2] == '0')) Serial.print("FL0;");

    //Reads the transceiver status.
    if((serial_data[0] == 'R') && (serial_data[1] == 'S') && (serial_data[2] == ';')) Serial.print("RS0;");

    //Sets or reads the VOX function status.
    if((serial_data[0] == 'V') && (serial_data[1] == 'X') && (serial_data[2] == ';')) Serial.print("VX0;");
    if((serial_data[0] == 'V') && (serial_data[1] == 'X') && (serial_data[3] == ';')) Serial.print("VX0;");

    // clear the string:
    serial_data = "";
    serial_end = false;
  }
}

//---------------------------------------
void onewire_scan() {
  Serial.print("Scan onewire devices... ");
  if (ds.search(onewire_addr)) {
    Serial.println("done");
    //
    Serial.print("Found device ROM: ");
    for(byte b = 0; b < 8; b++) {
      Serial.write(' ');
      Serial.print(onewire_addr[b], HEX);
    }
    Serial.println();
    //
    Serial.print("Check CRC device: ");
    if (OneWire::crc8(onewire_addr, 7) == onewire_addr[7]) {
      Serial.println("valid");
      //
      Serial.print("Chip type: ");
      switch (onewire_addr[0]) {
        case 0x10:
          Serial.println("DS18S20");  // or old DS1820
          break;
        case 0x28:
          Serial.println("DS18B20");
          break;
        case 0x22:
          Serial.println("DS1822");
          break;
        default:
          Serial.println("is not a DS18x20 family device.");
          //
          status_onewire = false;
      } 
      //
      status_onewire = true;
    } else {
      Serial.println("not valid!");
      //
      status_onewire = false;
    }
  } else {
    Serial.println("not found!");
    //
    status_onewire = false;
  }
}

//---------------------------------------
void onewire_init_temp() {
  ds.reset();
  ds.select(onewire_addr);
  ds.write(0x44, 1);  // start conversion, with parasite power on at the end
}

//---------------------------------------
void onewire_get_temp() {
  ds.reset();
  ds.select(onewire_addr);    
  ds.write(0xBE);  // Read Scratchpad
  //
  //Serial.print("Onewire data: ");
  for (byte b = 0; b < 9; b++) {  // we need 9 bytes
    onewire_data[b] = ds.read();
    //Serial.print(onewire_data[b], HEX);
    //Serial.print(" ");
  }
  Serial.println();
  //
  // Serial.print("Onewire CRC data: ");
  // Serial.print(OneWire::crc8(onewire_data, 8), HEX);
  // Serial.println();

  int16_t onewire_raw = (onewire_data[1] << 8) | onewire_data[0];

  if (onewire_addr[0] == 0x10) {
    onewire_raw = onewire_raw << 3; // 9 bit resolution default
    if (onewire_data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      onewire_raw = (onewire_raw & 0xFFF0) + 12 - onewire_data[6];
    }
  } else {
    byte onewire_cfg = (onewire_data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (onewire_cfg == 0x00) onewire_raw = onewire_raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (onewire_cfg == 0x20) onewire_raw = onewire_raw & ~3; // 10 bit res, 187.5 ms
    else if (onewire_cfg == 0x40) onewire_raw = onewire_raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  onewire_temp = (float)onewire_raw / 16.0;
}

//---------------------------------------
void get_encoder_button() {
  long encoder_pos = encoder_button.readAndReset();
  //
  switch (encoder_pos) {
    case 1:
      //Serial.println(encoder_pos);
      freq_main++;
      break;
    case -1:
      //Serial.println(encoder_pos);
      freq_main--;
      break;
  }
}

//---------------------------------------
//---------------------------------------
void setup() {
  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  //
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  //
  pinMode(RF_OUTPUT_PIN, OUTPUT);
  analogWrite(RF_OUTPUT_PIN, 0);
  //
  pinMode(RF_METER_PIN, INPUT);
  //
  pinMode(AUDIO_OUTPUT_PIN, OUTPUT);
  digitalWrite(AUDIO_OUTPUT_PIN, LOW);
  //
  pinMode(AUDIO_INPUT_PIN, INPUT);

  Serial.begin(SERIAL_SPEED);
  delay(1000);
  Serial.println("");
  //
  serial_data.reserve(SERIAL_RESERVE);

  Serial.print("Init I2C-wire bus: ");
  if (Wire.begin(I2C_SDA,I2C_SCL)) {
    Serial.println("OK");
  } else {
    Serial.println("ERROR");
  }

  Serial.print("Init OLED display: ");
  if (display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    display.clearDisplay();
    //
    display.setTextColor(SSD1306_WHITE);
    if (SCREEN_ROTATION > 0) display.setRotation(SCREEN_ROTATION);
    //
    status_oled = true;
    Serial.println("OK");
  } else {
    status_oled = false;
    Serial.println("ERROR");
  }

  //Show info
  if (status_oled) {
    display.clearDisplay();
    display_print(0, 0, NULL, 2, "uSDT");
    display_print(80, 8, NULL, 1, "v");
    display.print(FIRMWARE_VERSION);
    display_print(0, 16, NULL, 2, "UR4URV");
    display_print(0, 35, NULL, 1, "MCU: ");
    display.print(MCU_MODEL);
    display_print(0, 45, NULL, 1, "Clock RTC: DS1307");    
    display_print(0, 55, NULL, 1, "Generator: SI5351");
    //
    display.display();
    //
    delay(3000);
  }

  Serial.println("Set WiFi mode: client");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  //
  Serial.print("Connect to: ");
  Serial.print(ssid);
  Serial.println(" ");
  //
  if (status_oled) {
    display.clearDisplay();
    display_print(0, 0, NULL, 1, "WiFi: ");
    display_print(40, 0, NULL, 1, ssid);
    display.display();
  }
  //
  int connect_try = 5;
  while (WiFi.status() != WL_CONNECTED) {
    if (connect_try > 0) {
      connect_try --;
      //
      Serial.print(".");
      delay(500);
    } else {
      Serial.print(" not connected.");
      //
      if (status_oled) {
        display_print(0, 8, NULL, 1, "WiFi is not connected.");
        display.display();
      }
      //
      break;      
    }
  }
  Serial.println("");
  //
  String wifi_ip = WiFi.localIP().toString();
  Serial.print("IP address: ");
  Serial.println(wifi_ip);
  //
  if (status_oled) {
    display_print(0, 16, NULL, 1, "IP: ");
    display_print(30, 16, NULL, 1, wifi_ip.c_str());
    display.display();
  }

  web_init();
  
  Serial.println("");
  Serial.print("Start RTC clock: ");  
  if (rtc.begin()) {
    status_rtc = true;
    Serial.println("OK");
  } else {
    status_rtc = false;
    Serial.println("ERROR");
  }

  Serial.print("Start NTP client... ");
  ntp_client.begin();
  Serial.println("done.");

  get_ntp(true); //default true
  //
  if (status_oled) {
    display_print(0, 35, NULL, 1, "Status NTP:");
    if (status_ntp) {
      display_print(90, 35, NULL, 1, "OK");
    } else {
      display_print(90, 35, NULL, 1, "ERROR");
    }
    display.display();
  }

  get_rtc();
  //
  if (status_oled) {
    display_print(0, 45, NULL, 1, "Status RTC:");
    if (status_rtc) {
      display_print(90, 45, NULL, 1, "OK");
    } else {
      display_print(90, 45, NULL, 1, "ERROR");
    }
    display.display();
  }

  Serial.print("Init si5351 module: ");
  if (si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, cal_factor)) {
    si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);    // Set for maximum power
    si5351.set_clock_pwr(SI5351_CLK0, 0);                    // Disable the clock initially
    //
    status_si5351 = true;
    Serial.println("OK");
  } else {
    status_si5351 = false;
    Serial.println("ERROR");
  }

  if (status_si5351) {
    display_print(0, 55, NULL, 1, "Status si5351:");
    if (status_si5351) {
      display_print(90, 55, NULL, 1, "OK");
    } else {
      display_print(90, 55, NULL, 1, "ERROR");
    }
    display.display();
  }

  onewire_scan();

  //Create default FT8 message
  strcpy(ft8_message, "CQ ");
  strcat(ft8_message, callsign);
  strcat(ft8_message, " ");
  strcat(ft8_message, location);

  Serial.println("---------------------");
  //
  //ft8_encode_message();
  //wspr_encode_message();
  //
  //Serial.println("---------------------");

  delay(3000);
}

//---------------------------------------
//---------------------------------------
uint32_t tmr;
bool onewire_init = true;
//
void loop() {
  web_server.handleClient();
  //
  get_serial();
  //
  get_encoder_button();
  //
  int buttonState = digitalRead(BUTTON_PIN);
  if (buttonState == LOW) {
    count_tx = COUNT_DEFAULT;
    status_tx = true;    
  }

  if (millis() - tmr >= 500) {
    tmr = millis();
    //
    date_time = rtc.now();
    //
    if (status_onewire) {
      if (onewire_init) {
        onewire_init = false;
        onewire_init_temp();
      } else {
        onewire_init = true;
        onewire_get_temp();
      }
    }
    //
    if (status_oled) show_main();
  }

  if (status_tx) {
    int start_time = 0;
    //
    if (time_interval < 60) {
      start_time = date_time.second() % time_interval;
    } else {
      start_time = date_time.minute() % (time_interval / 60);
    }
    //
    if (start_time == time_shift) {
      count_tx -= 1;
      if (count_tx < 1) { status_tx = false; }
      //
      show_status_tx = true;
      if (status_oled) show_main();
      //
      if (strcmp (mode_tx,"FT8") == 0) {
        ft8_encode_message();
        //
        serial_print_time();
        ft8_tx(ft8_tones); 
      }
      if (strcmp (mode_tx,"WSPR") == 0) {
        wspr_encode_message();
        //
        serial_print_time();
        wspr_tx(wspr_tones);
      }
      //
      show_status_tx = false;
    }
  }
}
