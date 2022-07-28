#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP_Mail_Client.h>
#include <SPIFFS.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>
#include <string.h>

#define SMTP_HOST "mail.gmx.net"
#define SMTP_PORT 587
#define AUTHOR_EMAIL "absender@gmx.de"
#define AUTHOR_PASSWORD "geheimes_passwort"
#define RECIPIENT_EMAIL "empfaenger@gmx.de"

SMTPSession smtp;
void smtpCallback(SMTP_Status status);


const char* ssid = "WLAN_SSID";
const char* password = "geheimes_passwort";

IPAddress local_IP(192, 168, 0, 99);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

const char* input_parameter = "state";

const int output = 16;
const int Push_button_GPIO = 39 ;

// Variables will change:
int LED_state = HIGH;         
int button_state;             
int lastbutton_state = LOW;   

unsigned long lastDebounceTime = 0;  
unsigned long debounceDelay = 50;    

AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="de">
<head>
  <title>Türöffner</title>
  <meta charset="utf-8" name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html {font-family: Times New Roman; display: inline-block; text-align: center;}
    h2 {font-size: 3.0rem;}
    h4 {font-size: 2.0rem;}
    p {font-size: 3.0rem;}
    body {max-width: 900px; margin:0px auto; padding-bottom: 25px; background-color: #000000; color: #ffffff;}
    .switch {position: relative; display: inline-block; width: 120px; height: 68px} 
    .switch input {display: none}
    .slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #FF0000; border-radius: 34px}
    .slider:before {position: absolute; content: ""; height: 52px; width: 52px; left: 8px; bottom: 8px; background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 68px}
    input:checked+.slider {background-color: #27c437}
    input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px); transform: translateX(52px)}
  </style>
</head>
<body>
  <h2>Türöffner</h2>
  %BUTTONPLACEHOLDER%
<script>function toggleCheckbox(element) {
  var xhr = new XMLHttpRequest();
  if(element.checked){ xhr.open("GET", "/update?state=1", true); }
  else { xhr.open("GET", "/update?state=0", true); }
  xhr.send();
}

setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var inputChecked;
      var outputStateM;
      if( this.responseText == 1){ 
        inputChecked = true;
        outputStateM = "ZU";
      }
      else { 
        inputChecked = false;
        outputStateM = "AUF";
      }
      document.getElementById("output").checked = inputChecked;
      document.getElementById("outputState").innerHTML = outputStateM;
    }
  };
  xhttp.open("GET", "/state", true);
  xhttp.send();
}, 1000 ) ;
</script>
</body>
</html>
)rawliteral";

String processor(const String& var){
  if(var == "BUTTONPLACEHOLDER"){
    String buttons ="";
    String outputStateValue = outputState();
    buttons+= "<h4>TÜRE IST <span id=\"outputState\"></span></h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"output\" " + outputStateValue + "><span class=\"slider\"></span></label>";
    return buttons;
  }
  return String();
}

String outputState(){
  if(digitalRead(output)){
    return "checked";
  }
  else {
    return "";
  }
  return "";
}


/* Callback function to get the Email sending status */

void smtpCallback(SMTP_Status status){

/* Print the current status */

Serial.println(status.info());

/* Print the sending result */ if (status.success()){

Serial.println("----------------");

ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());

ESP_MAIL_PRINTF("Message sent failled: %d\n", status.failedCount());

Serial.println("----------------\n"); struct tm dt;

for (size_t i = 0; i < smtp.sendingResult.size(); i++){

/* Get the result item */

SMTP_Result result = smtp.sendingResult.getItem(i);

time_t ts = (time_t)result.timestamp; localtime_r(&ts, &dt);

ESP_MAIL_PRINTF("Message No: %d\n", i + 1);

ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed"); ESP_MAIL_PRINTF("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900,

dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec); ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients); ESP_MAIL_PRINTF("Subject: %s\n", result.subject);

}}}

MFRC522DriverPinSimple ss_pin(5);
MFRC522DriverSPI driver{ss_pin};

MFRC522 readers[]{driver};

void setup(void) {
  Serial.begin(115200);
  while (!Serial);
  
  for (MFRC522 reader : readers) {
    reader.PCD_Init();
    MFRC522Debug::PCD_DumpVersionToSerial(reader, Serial);

  pinMode(output, OUTPUT);
  digitalWrite(output, HIGH);
  pinMode(Push_button_GPIO, INPUT);
  
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  if (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Verbinde WLAN..");
  }


  Serial.println("IP Adresse: ");
  Serial.println(WiFi.localIP());


  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  
  server.on("/update", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String input_message;
    String inputParameter;
    // GET input1 value on <ESP_IP>/update?state=<input_message>
    if (request->hasParam(input_parameter)) {
      input_message = request->getParam(input_parameter)->value();
      inputParameter = input_parameter;
      digitalWrite(output, input_message.toInt());
      LED_state = !LED_state;
    }
    else {
      input_message = "No message sent";
      inputParameter = "none";
    }
    Serial.println(input_message);
    request->send(200, "text/plain", "OK");
  });

  
  server.on("/state", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(digitalRead(output)).c_str());
  });
  // Start server
  server.begin();
}}

void loop(void) {
  for (MFRC522 reader : readers) {
    if (reader.PICC_IsNewCardPresent() && reader.PICC_ReadCardSerial()){
      Serial.print(F("Karten-UID:"));
      MFRC522Debug::PrintUID(Serial, reader.uid);
      Serial.println();
      Serial.print(F("Karten-Typ: "));
      MFRC522::PICC_Type piccType = reader.PICC_GetType(reader.uid.sak);
      Serial.println(MFRC522Debug::PICC_GetTypeName(piccType));
int uid ;
uid = (reader.uid.uidByte[0] << 24) | (reader.uid.uidByte[1] << 16) | (reader.uid.uidByte[2] << 8) | reader.uid.uidByte[3];

unsigned int code1 = 0x01234567;
unsigned int code2 = 0x89ABCDEF;

if((uid) == (code1))
{
   Serial.println("Chip 1") ;
   pinMode(26, OUTPUT);   
   digitalWrite(26, LOW);
   pinMode(25, OUTPUT);   
   digitalWrite(25, LOW);
   pinMode(33, OUTPUT);   
   digitalWrite(33, LOW);
   pinMode(32, OUTPUT);   
   digitalWrite(32, HIGH);
   delay(3000);
}
else if((uid) == (code2))
{
   Serial.println( "Chip 2") ;
   pinMode(26, OUTPUT);   
   digitalWrite(26, LOW);
   pinMode(25, OUTPUT);   
   digitalWrite(25, LOW);
   pinMode(33, OUTPUT);   
   digitalWrite(33, LOW);
   pinMode(32, OUTPUT);   
   digitalWrite(32, HIGH);
   delay(3000);
}
else {Serial.println("Falsche UID");
{
   pinMode(26, OUTPUT);   
   digitalWrite(26, HIGH);
   pinMode(25, OUTPUT);   
   digitalWrite(25, HIGH);
   pinMode(33, OUTPUT);   
   digitalWrite(33, HIGH);
   pinMode(32, OUTPUT);   
   digitalWrite(32, LOW);
}
smtp.debug(1);
   smtp.callback(smtpCallback);
   ESP_Mail_Session session;
   session.server.host_name = SMTP_HOST;
   session.server.port = SMTP_PORT;
   session.login.email = AUTHOR_EMAIL;
   session.login.password = AUTHOR_PASSWORD;
   session.login.user_domain = "";
   SMTP_Message message;
   message.sender.name = "absender@gmx.de";
   message.sender.email = AUTHOR_EMAIL;
   message.subject = "Falsche UID probiert";
   message.addRecipient("", RECIPIENT_EMAIL);
   String htmlMsg = "<div style=\"color:#2f4468;\"><h1>Falscher RFID-Chip erkannt</h1><p>- Sollte das ein Test sein, kann die Email ignoriert werden.</p></div>";
   message.html.content = htmlMsg.c_str();
   message.html.content = htmlMsg.c_str();
   message.text.charSet = "us-ascii";
   message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

    if (!smtp.connect(&session))
    return;
  /* Start sending Email and close the session */
  if (!MailClient.sendMail(&smtp, &message))
    Serial.println("Fehler beim versenden der Email, " + smtp.errorReason());

   delay(60000);
   Serial.println("Nächster Versuch");}

  int data = digitalRead(Push_button_GPIO);

  if (data != lastbutton_state) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (data != button_state) {
      button_state = data;


      if (button_state == HIGH) {
        LED_state = !LED_state;
      }
    }
  }

  
  digitalWrite(output, LED_state);


  lastbutton_state = data;
   
}}}
