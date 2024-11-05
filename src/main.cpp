// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Central sample specific for Espressif ESP32.
 * It uses our Azure Embedded SDK for C to help interact with Azure IoT.
 * For reference, please visit https://github.com/azure/azure-sdk-for-c and https://azureiotcentral.com/.
 * 
 * To connect and work with Azure IoT Hub you need an MQTT client, connecting, subscribing
 * and publishing to specific topics to use the messaging features of the hub.
 * Our azure-sdk-for-c is an MQTT client support library, helping composing and parsing the
 * MQTT topic names and messages exchanged with the Azure IoT Hub.
 * 
 * The additional layers in this sketch provide a structured use of azure-sdk-for-c and
 * the MQTT client of your choice to perform all the steps needed to connect and interact with 
 * Azure IoT Central.
 * 
 * AzureIoT.cpp contains a state machine that implements those steps, plus abstractions to simplify
 * its overall use. Besides the basic configuration needed to access the Azure IoT services,
 * all that is needed is to provide the functions required by that layer to:
 * - Interact with your MQTT client,
 * - Perform data manipulations (HMAC SHA256 encryption, Base64 decoding and encoding),
 * - Receive the callbacks for Plug and Play properties and commands.
 * 
 * Azure_IoT_PnP_Template.cpp contains the actual implementation of the IoT Plug and Play template
 * specific for the Espressif ESP32 board.
 * 
 * To properly connect to your Azure IoT services, please fill the information in the `iot_configs.h` file. 
 */

/* --- Dependencies --- */
#include <Arduino.h>
#include "FS.h"
// C99 libraries
#include <cstdlib>
#include <cstdarg>
#include <string.h>
#include <time.h>

// For hmac SHA256 encryption
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <mqtt_client.h>

// MRD Confs
#include "htmlServer.h"
#include "SparkFun_VL53L1X.h"
#include <Wire.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers 
#include "AzureIoT.h"
#include "Azure_IoT_PnP_Template.h"
#include "iot_configs.h"

// MRD DEFINES
// led_defines in htmlServer
#define IO_PUMP 10
#define IO_HOLD_UP 3
#define IO_RX 0
#define IO_TX 1
#define SDA 8
#define SCL 9
#define SPEED 400000
#define TIMECUTOFF 30000
#define TIMECUTOFF2 60000 /* 1 min apos ligado*/

// MRD PERIFERICOS
void updateSensorValue();
void activeEject();
void timeCount();
void commandFromSerial0(String p1, String p2);
String uint8ToHex(uint8_t value);

SFEVL53L1X distanceSensor;
HTML_SERVER serverhtml;
//ConnectWifiByDataHtml serverHtml;

/*MRD VARIABLES GLOBAL*/
unsigned char ReadSimgle[7] = {0XAA, 0X00, 0X22, 0X00, 0X00, 0X22, 0XDD};
uint16_t valueSensor_mm;
bool eject = false;
uint32_t time_t_ejected = 0;
uint32_t time_t_ejected_start = 0;
uint32_t time_t_off = 0;
uint32_t time_t_off_start = 0;
uint32_t time_t_off2 = 0;
uint32_t time_t_off2_start = 0;
bool time_t_off_flag = false;
bool timeStart_init;
bool noWifiMore = false;
float volumDisp;
String tagRead;
char serialReadBuff[256];

bool enviarAzure = false;

bool readSensor = false;

/* --- Sample-specific Settings --- */
#define SERIAL_LOGGER_BAUD_RATE 115200
#define MQTT_DO_NOT_RETAIN_MSG  0

/* --- Time and NTP Settings --- */
//#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define NTP_SERVERS "a.st1.ntp.br", "a.ntp.br"

#define PST_TIME_ZONE -3
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF   1

#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)

#define UNIX_TIME_NOV_13_2017 1510592825
#define UNIX_EPOCH_START_YEAR 1900

/* --- Function Returns --- */
#define RESULT_OK       0
#define RESULT_ERROR    __LINE__

/* --- Function Declarations --- */
static void sync_device_clock_with_ntp_server();
static void connect_to_wifi();
static esp_err_t esp_mqtt_event_handler(esp_mqtt_event_handle_t event);

// This is a logging function used by Azure IoT client.
static void logging_function(log_level_t log_level, char const* const format, ...);

/* --- Sample variables --- */
static azure_iot_config_t azure_iot_config;
static azure_iot_t azure_iot;
static esp_mqtt_client_handle_t mqtt_client;

static char mqtt_broker_uri[128];

#define AZ_IOT_DATA_BUFFER_SIZE 1500 
static uint8_t az_iot_data_buffer[AZ_IOT_DATA_BUFFER_SIZE];

#define MQTT_PROTOCOL_PREFIX "mqtts://"

static uint32_t properties_request_id = 0;
static bool send_device_info = true;


/* --- MQTT Interface Functions --- */
/*
 * These functions are used by Azure IoT to interact with whatever MQTT client used by the sample
 * (in this case, Espressif's ESP MQTT). Please see the documentation in AzureIoT.h for more details.
 */

/*
 * See the documentation of `mqtt_client_init_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_init_function(mqtt_client_config_t* mqtt_client_config, mqtt_client_handle_t *mqtt_client_handle)
{
  int result;
  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));  

  az_span mqtt_broker_uri_span = AZ_SPAN_FROM_BUFFER(mqtt_broker_uri);
  mqtt_broker_uri_span = az_span_copy(mqtt_broker_uri_span, AZ_SPAN_FROM_STR(MQTT_PROTOCOL_PREFIX));
  mqtt_broker_uri_span = az_span_copy(mqtt_broker_uri_span, mqtt_client_config->address);
  az_span_copy_u8(mqtt_broker_uri_span, null_terminator);

  mqtt_config.uri = mqtt_broker_uri;
  mqtt_config.port = mqtt_client_config->port;
  mqtt_config.client_id = (const char*)az_span_ptr(mqtt_client_config->client_id);
  mqtt_config.username = (const char*)az_span_ptr(mqtt_client_config->username);

  mqtt_config.password = (const char*)az_span_ptr(mqtt_client_config->password);
  
  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = esp_mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char*)ca_pem;

  LogInfo("MQTT client target uri set to '%s'", mqtt_broker_uri);

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    LogError("esp_mqtt_client_init failed.");
    result = 1;
  }
  else
  {
    esp_err_t start_result = esp_mqtt_client_start(mqtt_client);
  
    if (start_result != ESP_OK)
    {
      LogError("esp_mqtt_client_start failed (error code: 0x%08x).", start_result);
      result = 1;
    }
    else
    {
      *mqtt_client_handle = mqtt_client;
      result = 0;
    }
  }

  return result;
}

/*
 * See the documentation of `mqtt_client_deinit_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_deinit_function(mqtt_client_handle_t mqtt_client_handle)
{
  int result = 0;
  esp_mqtt_client_handle_t esp_mqtt_client_handle = (esp_mqtt_client_handle_t)mqtt_client_handle;

  LogInfo("MQTT client being disconnected.");
  
  if (esp_mqtt_client_stop(esp_mqtt_client_handle) != ESP_OK)
  {
    LogError("Failed stopping MQTT client.");
  }

  if (esp_mqtt_client_destroy(esp_mqtt_client_handle) != ESP_OK)
  {
    LogError("Failed destroying MQTT client.");
  }

  if (azure_iot_mqtt_client_disconnected(&azure_iot) != 0)
  {
    LogError("Failed updating azure iot client of MQTT disconnection.");      
  }

  return 0;
}

/*
 * See the documentation of `mqtt_client_subscribe_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_subscribe_function(mqtt_client_handle_t mqtt_client_handle, az_span topic, mqtt_qos_t qos)
{
  LogInfo("MQTT client subscribing to '%.*s'", az_span_size(topic), az_span_ptr(topic));
       
  // As per documentation, `topic` always ends with a null-terminator.
  // esp_mqtt_client_subscribe returns the packet id or negative on error already, so no conversion is needed.
  int packet_id = esp_mqtt_client_subscribe((esp_mqtt_client_handle_t)mqtt_client_handle, (const char*)az_span_ptr(topic), (int)qos);

  return packet_id;
}

/*
 * See the documentation of `mqtt_client_publish_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_publish_function(mqtt_client_handle_t mqtt_client_handle, mqtt_message_t* mqtt_message)
{
  LogInfo("MQTT client publishing to '%s'", az_span_ptr(mqtt_message->topic));

  int mqtt_result = esp_mqtt_client_publish(
    (esp_mqtt_client_handle_t)mqtt_client_handle, 
    (const char*)az_span_ptr(mqtt_message->topic), // topic is always null-terminated.
    (const char*)az_span_ptr(mqtt_message->payload), 
    az_span_size(mqtt_message->payload),
    (int)mqtt_message->qos, 
    MQTT_DO_NOT_RETAIN_MSG);
  
  if (mqtt_result == -1)
  {
    return RESULT_ERROR;
  }
  else
  {
    return RESULT_OK;
  }
}

/* --- Other Interface functions required by Azure IoT --- */

/*
 * See the documentation of `hmac_sha256_encryption_function_t` in AzureIoT.h for details.
 */
static int mbedtls_hmac_sha256(const uint8_t* key, size_t key_length, const uint8_t* payload, size_t payload_length, uint8_t* signed_payload, size_t signed_payload_size)
{
  (void)signed_payload_size;
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key, key_length);
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)payload, payload_length);
  mbedtls_md_hmac_finish(&ctx, (byte*)signed_payload);
  mbedtls_md_free(&ctx);

  return 0;
}

/*
 * See the documentation of `base64_decode_function_t` in AzureIoT.h for details.
 */
static int base64_decode(uint8_t* data, size_t data_length, uint8_t* decoded, size_t decoded_size, size_t* decoded_length)
{
  return mbedtls_base64_decode(decoded, decoded_size, decoded_length, data, data_length);
}

/*
 * See the documentation of `base64_encode_function_t` in AzureIoT.h for details.
 */
static int base64_encode(uint8_t* data, size_t data_length, uint8_t* encoded, size_t encoded_size, size_t* encoded_length)
{
  return mbedtls_base64_encode(encoded, encoded_size, encoded_length, data, data_length);
}

/*
 * See the documentation of `properties_update_completed_t` in AzureIoT.h for details.
 */
static void on_properties_update_completed(uint32_t request_id, az_iot_status status_code)
{
  LogInfo("Properties update request completed (id=%d, status=%d)", request_id, status_code);
}

/*
 * See the documentation of `properties_received_t` in AzureIoT.h for details.
 */
void on_properties_received(az_span properties)
{
  LogInfo("Properties update received: %.*s", az_span_size(properties), az_span_ptr(properties));

  // It is recommended not to perform work within callbacks.
  // The properties are being handled here to simplify the sample.
  if (azure_pnp_handle_properties_update(&azure_iot, properties, properties_request_id++) != 0)
  {
    LogError("Failed handling properties update.");
  }
}

/*
 * See the documentation of `command_request_received_t` in AzureIoT.h for details.
 */
static void on_command_request_received(command_request_t command)
{  
  az_span component_name = az_span_size(command.component_name) == 0 ? AZ_SPAN_FROM_STR("") : command.component_name;
  
  LogInfo("Command request received (id=%.*s, component=%.*s, name=%.*s)", 
    az_span_size(command.request_id), az_span_ptr(command.request_id),
    az_span_size(component_name), az_span_ptr(component_name),
    az_span_size(command.command_name), az_span_ptr(command.command_name));

  // Here the request is being processed within the callback that delivers the command request.
  // However, for production application the recommendation is to save `command` and process it outside
  // this callback, usually inside the main thread/task/loop.
  (void)azure_pnp_handle_command_request(&azure_iot, command); //RECEBE E TRATA O COMANDO
}

az_span convert_str(String value)
{
  int length = value.length();
  char *nKey = new char[length];
  strcpy(nKey, value.c_str());
  az_span key = az_span_create_from_str(nKey);
  return key;
}

/* --- Arduino setup and loop Functions --- */
void setup()
{
  Serial.begin(SERIAL_LOGGER_BAUD_RATE);
  Serial1.begin(115200, SERIAL_8N1, IO_RX, IO_TX);
  
  set_logging_function(logging_function);

  pinMode(IO_PUMP, OUTPUT);
  pinMode(IO_HOLD_UP, OUTPUT);
  pinMode(IO_externalLED, OUTPUT);

  digitalWrite(IO_HOLD_UP,1);
  initSensors();

  while (distanceSensor.begin(Wire) != 0) // Begin returns 0 on a good init
  {
      Serial.print("E1");
  }
  serverhtml.readConfs();

  connect_to_wifi();

  azure_pnp_init();

  /*
  * The configuration structure used by Azure IoT must remain unchanged (including data buffer) 
  * throughout the lifetime of the sample. This variable must also not lose context so other
  * components do not overwrite any information within this structure.
  */
  azure_iot_config.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);
  azure_iot_config.model_id = azure_pnp_get_model_id();
  azure_iot_config.use_device_provisioning = true; // Required for Azure IoT Central.
  azure_iot_config.iot_hub_fqdn = AZ_SPAN_EMPTY;
  azure_iot_config.device_id = AZ_SPAN_EMPTY;

  azure_iot_config.device_certificate = AZ_SPAN_EMPTY;
  azure_iot_config.device_certificate_private_key = AZ_SPAN_EMPTY;
  azure_iot_config.device_key = convert_str(serverhtml.confWifi.iot_conf_dkey);

  azure_iot_config.dps_id_scope = convert_str(serverhtml.confWifi.dps_id_scope);
  azure_iot_config.dps_registration_id = convert_str(serverhtml.confWifi.iot_conf_dID); // Use Device ID for Azure IoT Central.
  azure_iot_config.data_buffer = AZ_SPAN_FROM_BUFFER(az_iot_data_buffer);
  azure_iot_config.sas_token_lifetime_in_minutes = MQTT_PASSWORD_LIFETIME_IN_MINUTES;
  azure_iot_config.mqtt_client_interface.mqtt_client_init = mqtt_client_init_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_deinit = mqtt_client_deinit_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_subscribe = mqtt_client_subscribe_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_publish = mqtt_client_publish_function;
  azure_iot_config.data_manipulation_functions.hmac_sha256_encrypt = mbedtls_hmac_sha256;
  azure_iot_config.data_manipulation_functions.base64_decode = base64_decode;
  azure_iot_config.data_manipulation_functions.base64_encode = base64_encode;
  azure_iot_config.on_properties_update_completed = on_properties_update_completed;
  azure_iot_config.on_properties_received = on_properties_received;
  azure_iot_config.on_command_request_received = on_command_request_received;

  azure_iot_init(&azure_iot, &azure_iot_config);
  azure_iot_start(&azure_iot);

  LogInfo("Azure IoT client initialized (state=%d)", azure_iot.state);
  time_t_off2_start = millis();
}

// MRD Functions
String uint8ToHex(uint8_t value)
{
  char buffer[4]; // 2 caracteres para o hex + nulo
  sprintf(buffer, "%02X-", value);
  return String(buffer);
}

uint8_t x = 0;
void serialRead() {
  uint8_t endPos = 0;
  bool startRead = false;
  if (Serial.available()>0) {
    char r = Serial.read();
    if(r=='{'){
      x = 0;
    }
    if(r=='}') {
      endPos = x;
      startRead = true;
    }
    serialReadBuff[x] = r;
    x++;
  }
  if(!startRead) return;
  String result = "";
  uint8_t co = 0;
  while(co<=endPos) {
    result += serialReadBuff[co];
    co++;
  }
  int8_t pos = result.indexOf(':');
  String parte1 = result.substring(2, pos - 1);
  String parte2 = result.substring(pos + 2, endPos-1);
  commandFromSerial0(parte1,parte2);
}

void commandFromSerial0(String cmd,String value) {
  String cmdList[] = {"ssid", "pass", "reset", "readConfs", "calib", "refil", "dps_id_scope", "iot_conf_did", "iot_conf_dkey", "help", "m_act_dist", "confml", "reset_refil"};
  int numeroDeElementos = sizeof(cmdList) / sizeof(cmdList[0]);
  uint8_t co = 0;
  uint8_t cmdN = 245;
  while(co<=numeroDeElementos){
    if(cmd == cmdList[co]) {
      cmdN = co;
      break;
    }
    co++;
  }
  
  switch (cmdN)
  {
  case en_CMD_SSID: // ssid
    Serial.print("SSID: ");
    Serial.println(value);
    serverhtml.confWifi.ssid = value;
    break;
  case en_CMD_PASSWORD:
    Serial.print("PASS: ");
    Serial.println(value);
    serverhtml.confWifi.password = value;
    break;
  case en_CMD_RESET:
    serverhtml.saveConfs();
    Serial.println("Reset");
    delay(3000);
    ESP.restart();
    break;
  case en_CMD_READCONFS:
    Serial.println("######## ssid_conf.json ############");
    Serial.println(serverhtml.readFile(SPIFFS, "/ssid_conf.json"));
    Serial.println("####################################");
    break;
  case en_CMD_CALIB:
    serverhtml.confEq.calib_val = atof(value.c_str());
    Serial.print("\n--- Calib: ");
    Serial.println(value.c_str());
    serverhtml.confEq.result_calib_vol = serverhtml.confEq.confml * serverhtml.confEq.calib_val;
    break;
  case en_CMD_SETREFIL:
    serverhtml.confEq.refil_vol = atoi(value.c_str());
    Serial.print("\n--- Refil: ");
    Serial.println(value.c_str());
    serverhtml.confEq.refil_vol_rest = serverhtml.confEq.refil_vol;
    break;
  case en_CMD_dps_id_scope:
    serverhtml.confWifi.dps_id_scope = value;
    Serial.print("\n--- DPS ID SCOPE: ");
    Serial.println(serverhtml.confWifi.dps_id_scope);
    break;
  case en_CMD_iot_conf_did:
    serverhtml.confWifi.iot_conf_dID = value;
    Serial.print("\n--- IOT CONF DEVICE ID: ");
    Serial.println(serverhtml.confWifi.iot_conf_dID);
    break;
  case en_CMD_iot_conf_dkey:
    serverhtml.confWifi.iot_conf_dkey = value;
    Serial.print("\n--- IOT CONF DEVICE KEY: ");
    Serial.println(serverhtml.confWifi.iot_conf_dkey);
    break;
  case en_CMD_help:
    Serial.println("\n### CMD LIST ####");
    co = 0;
    while (co < numeroDeElementos)
    {
      Serial.println(cmdList[co]);
      co++;
    }
    Serial.println("#################");
    break;
  case en_CMD_m_act_dist:
    serverhtml.confEq.m_act_dist = atoi(value.c_str());
    Serial.print("\n--- ACTIVE HANDS DISTANCE: ");
    Serial.println(serverhtml.confEq.m_act_dist);
    break;
  case en_CMD_confml:
    serverhtml.confEq.confml = atoi(value.c_str());
    Serial.print("\n--- confml: ");
    Serial.println(serverhtml.confEq.confml);
    serverhtml.confEq.result_calib_vol = serverhtml.confEq.confml * serverhtml.confEq.calib_val;
    break;
  case en_CMD_reset_refil:
    serverhtml.confEq.refil_vol_rest = serverhtml.confEq.refil_vol;
    Serial.print("\n--- Refil trocado: ");
    Serial.println(serverhtml.confEq.refil_vol_rest);
    serverhtml.saveConfs();
    break;
  default:
    Serial.println("\n--- COMANDO N RECONHECIDO ");
    break;
  }
}

bool startReadRF = false;
void serial1Read()
{
  bool newRead = false;
  bool startSave = false;
  bool waitDD = false;
  uint16_t contHere = 0;
  uint8_t buff[25];
  //tagRead = "";
  if (Serial1.available() > 0 && startReadRF)
  {
    while (Serial1.available() && contHere < 25)
    {
      char x = Serial1.read();
      if (startSave)
      {
        buff[contHere] = x;
      }
      if (x == 0xAA && contHere == 0)
      {
        buff[contHere] = x;
        startSave = true;
      }
      if (startSave && buff[1] == 0x02)
      {
        waitDD = true;
      }
      if (buff[contHere] == 0xDD && waitDD && startSave)
      {
        uint8_t nCont = 8;
        tagRead = "";
        while (nCont <= 19)
        {
          tagRead += uint8ToHex(buff[nCont]);
          nCont++;
        }
        startReadRF = false;
        enviarAzure = true;
        //Serial.print(tagRead);
        //Serial.println();
      }
      contHere++;
      // Serial.print("c");
    }
    // Serial.println("EW");
    if (!waitDD)
    {
      Serial1.write(ReadSimgle, 7);
    }
  }
}

void updateSensorValue()
{
  if (!readSensor)
  {
    distanceSensor.startRanging();
    readSensor = true;
  }
  while (!distanceSensor.checkForDataReady())
  {
    // return;
  }
  valueSensor_mm = distanceSensor.getDistance();
  distanceSensor.clearInterrupt();
  distanceSensor.stopRanging();
  readSensor = false;
  if (valueSensor_mm<serverhtml.confEq.m_act_dist) {
    digitalWrite(IO_externalLED, 1);
  } else {
    digitalWrite(IO_externalLED, 0);
  }
  //Serial.print(".");
  //Serial.println(valueSensor_mm);
};

void activeEject()
{
  if (valueSensor_mm < serverhtml.confEq.m_act_dist && time_t_ejected < serverhtml.confEq.result_calib_vol)
  {
    if (!timeStart_init)
    {
      timeStart_init = true;
      time_t_off_flag = false;
      time_t_ejected_start = millis();
      //Serial.println("EJETANDO");
      eject = true;
    }
  }
  else
  {
    if (eject)
    {
      //Serial.println("EJETANDO_FIM");
      Serial1.write(ReadSimgle, 7);
      startReadRF = true;
      time_t_off_flag = true;
      time_t_off_start = millis();
      time_t_off2_start = millis();
      uint8_t cont = 5;
      bool ledOnOff;
      digitalWrite(IO_PUMP, 0);
      while (cont--)
      {
        digitalWrite(IO_externalLED, ledOnOff);
        ledOnOff = !ledOnOff;
        delay(100);
      }
      if (time_t_ejected < serverhtml.confEq.result_calib_vol)
      {
        //uint16_t totalTime = serverhtml.confEq.result_calib_vol - time_t_ejected;
        volumDisp = time_t_ejected*serverhtml.confEq.confml/serverhtml.confEq.result_calib_vol;
      }
      else
      {
        volumDisp = serverhtml.confEq.confml;
      }
      serverhtml.confEq.refil_vol_rest = serverhtml.confEq.refil_vol_rest - volumDisp;
      if (serverhtml.confEq.refil_vol_rest<=0) {
        serverhtml.confEq.refil_vol_rest = 0;
      }
      serverhtml.saveConfs();
    }
    eject = false;
    if (valueSensor_mm > serverhtml.confEq.m_act_dist + 10)
    {
      timeStart_init = false;
    }
    time_t_ejected = 0;
  }
  digitalWrite(IO_PUMP, eject);
};

void timeCount()
{
  if (eject)
  {
    time_t_ejected = millis() - time_t_ejected_start;
  }
  if (time_t_off_flag)
  {
    time_t_off = millis() - time_t_off_start;
  } else {
    time_t_off = 0;
  }
  time_t_off2 = millis() - time_t_off2_start;
};

void manterLigado()
{
  if (time_t_off > TIMECUTOFF || time_t_off2 > TIMECUTOFF2)
  {
    digitalWrite(IO_HOLD_UP,0);
  }
  else
  {
    digitalWrite(IO_HOLD_UP,1);
  }
}

void loop()
{
  updateSensorValue();
  activeEject();
  timeCount();
  serial1Read();
  manterLigado();
  serialRead();
  
  if (WiFi.status() != WL_CONNECTED)
  {
    azure_iot_stop(&azure_iot);
    connect_to_wifi();
  }
  else if (WiFi.status() == WL_CONNECTED)
  {
    sync_device_clock_with_ntp_server();

    switch(azure_iot_get_status(&azure_iot))
    {
      case azure_iot_connected:
        if (send_device_info)
        {
          (void)azure_pnp_send_device_info(&azure_iot, properties_request_id++);
          send_device_info = false; // Only need to send once.
        }
        else if (azure_pnp_send_telemetry(&azure_iot) != 0) // DENTRO DO ENVIO E VERIFICADO O BOOL <enviarAzure>
        {
          LogError("Failed sending telemetry.");
        }
        break;
      case azure_iot_error:
        LogError("Azure IoT client is in error state." );
        azure_iot_stop(&azure_iot);
        break;
      case azure_iot_disconnected:
        azure_iot_start(&azure_iot);
        break;
      default:
        break;
    }
    azure_iot_do_work(&azure_iot);
  }
}

/* === Function Implementations === */

/*
 * These are support functions used by the sample itself to perform its basic tasks
 * of connecting to the internet, syncing the board clock, ESP MQTT client event handler 
 * and logging.
 */
bool timeNTPInit = false;
/* --- System and Platform Functions --- */
static void sync_device_clock_with_ntp_server()
{
  if(timeNTPInit) return;
  LogInfo("Setting time using SNTP");

  configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
  time_t now = time(NULL);
  int8_t tentat_l = 10;
  while (now < UNIX_TIME_NOV_13_2017 && tentat_l--)
  {
    delay(500);
    Serial.print(".");
    now = time(NULL);
  }
  if(tentat_l<=0) {
    LogInfo("FAIL TIME");
  } else {
    LogInfo("Time initialized!");
  }
  timeNTPInit = true;
}

int8_t tentativas = 10;
static void connect_to_wifi()
{
  if(noWifiMore) return;

  //LogInfo("Connecting to WIFI wifi_ssid %s", serverhtml.confWifi.ssid);
  //LogInfo("Connecting to WIFI pass %s", serverhtml.confWifi.password);
  //Serial.println(serverhtml.confWifi.password);

  WiFi.mode(WIFI_STA);
  WiFi.begin(serverhtml.confWifi.ssid, serverhtml.confWifi.password); // serverhtml.confWifi.password
  if (WiFi.status() != WL_CONNECTED && tentativas--)
  {
    delay(1000);
  }
  if (tentativas <= 0) {
    Serial.println("FAIL CONNECT WIFI ");
    noWifiMore = true;
  } else {
    LogInfo("WiFi connected, IP address: %s", WiFi.localIP().toString().c_str());
  }
}

static esp_err_t esp_mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
    int i, r;

    case MQTT_EVENT_ERROR:
      LogError("MQTT client in ERROR state.");
      LogError( 
        "esp_tls_stack_err=%d; esp_tls_cert_verify_flags=%d;esp_transport_sock_errno=%d;error_type=%d;connect_return_code=%d",  
        event->error_handle->esp_tls_stack_err,
        event->error_handle->esp_tls_cert_verify_flags,
        event->error_handle->esp_transport_sock_errno,
        event->error_handle->error_type,
        event->error_handle->connect_return_code);

      switch (event->error_handle->connect_return_code) 
      {
        case MQTT_CONNECTION_ACCEPTED: 
          LogError("connect_return_code=MQTT_CONNECTION_ACCEPTED"); 
          break; 
        case MQTT_CONNECTION_REFUSE_PROTOCOL: 
          LogError("connect_return_code=MQTT_CONNECTION_REFUSE_PROTOCOL"); 
          break; 
        case MQTT_CONNECTION_REFUSE_ID_REJECTED: 
          LogError("connect_return_code=MQTT_CONNECTION_REFUSE_ID_REJECTED"); 
          break; 
        case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE: 
          LogError("connect_return_code=MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE"); 
          break; 
        case MQTT_CONNECTION_REFUSE_BAD_USERNAME: 
          LogError("connect_return_code=MQTT_CONNECTION_REFUSE_BAD_USERNAME"); 
          break; 
        case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED: 
          LogError("connect_return_code=MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED"); 
          break; 
        default: 
          LogError("connect_return_code=unknown (%d)", event->error_handle->connect_return_code); 
          break; 
      };

      break;
    case MQTT_EVENT_CONNECTED:
      LogInfo("MQTT client connected (session_present=%d).", event->session_present);

      if (azure_iot_mqtt_client_connected(&azure_iot) != 0)
      {
        LogError("azure_iot_mqtt_client_connected failed.");
      }

      break;
    case MQTT_EVENT_DISCONNECTED:
      LogInfo("MQTT client disconnected.");

      if (azure_iot_mqtt_client_disconnected(&azure_iot) != 0)
      {
        LogError("azure_iot_mqtt_client_disconnected failed.");
      }

      break;
    case MQTT_EVENT_SUBSCRIBED:
      LogInfo("MQTT topic subscribed (message id=%d).", event->msg_id);

      if (azure_iot_mqtt_client_subscribe_completed(&azure_iot, event->msg_id) != 0)
      {
        LogError("azure_iot_mqtt_client_subscribe_completed failed.");
      }

      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      LogInfo("MQTT topic unsubscribed.");
      break;
    case MQTT_EVENT_PUBLISHED:
      LogInfo("MQTT event MQTT_EVENT_PUBLISHED");

      if (azure_iot_mqtt_client_publish_completed(&azure_iot, event->msg_id) != 0)
      {
        LogError("azure_iot_mqtt_client_publish_completed failed (message id=%d).", event->msg_id);
      }

      break;
    case MQTT_EVENT_DATA:
      LogInfo("MQTT message received.");

      mqtt_message_t mqtt_message;
      mqtt_message.topic = az_span_create((uint8_t*)event->topic, event->topic_len);
      mqtt_message.payload = az_span_create((uint8_t*)event->data, event->data_len);
      mqtt_message.qos = mqtt_qos_at_most_once; // QoS is unused by azure_iot_mqtt_client_message_received. 

      if (azure_iot_mqtt_client_message_received(&azure_iot, &mqtt_message) != 0)
      {
        LogError("azure_iot_mqtt_client_message_received failed (topic=%.*s).", event->topic_len, event->topic);
      }

      break;
    case MQTT_EVENT_BEFORE_CONNECT:
      LogInfo("MQTT client connecting.");
      break;
    default:
      LogError("MQTT event UNKNOWN.");
      break;
  }

  return ESP_OK;
}

static void logging_function(log_level_t log_level, char const* const format, ...)
{
  struct tm* ptm;
  time_t now = time(NULL);

  ptm = gmtime(&now);

  Serial.print(ptm->tm_year + UNIX_EPOCH_START_YEAR);
  Serial.print("/");
  Serial.print(ptm->tm_mon + 1);
  Serial.print("/");
  Serial.print(ptm->tm_mday);
  Serial.print(" ");

  if (ptm->tm_hour < 10)
  {
    Serial.print(0);
  }

  Serial.print(ptm->tm_hour);
  Serial.print(":");

  if (ptm->tm_min < 10)
  {
    Serial.print(0);
  }

  Serial.print(ptm->tm_min);
  Serial.print(":");

  if (ptm->tm_sec < 10)
  {
    Serial.print(0);
  }

  Serial.print(ptm->tm_sec);

  Serial.print(log_level == log_level_info ? " [INFO] " : " [ERROR] ");

  char message[256];
  va_list ap;
  va_start(ap, format);
  int message_length = vsnprintf(message, 256, format, ap);
  va_end(ap);

  if (message_length < 0)
  {
    Serial.println("Failed encoding log message (!)");
  }
  else
  {
    Serial.println(message);
  }
}
