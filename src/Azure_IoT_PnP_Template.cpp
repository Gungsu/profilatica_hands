// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include <Arduino.h>

#include <stdlib.h>
#include <stdarg.h>

#include <az_core.h>
#include <az_iot.h>

#include "AzureIoT.h"
#include "Azure_IoT_PnP_Template.h"

#include <az_precondition_internal.h>
#include "htmlServer.h"

// For DHT sensor
#include "iot_configs.h"

// For sunlight sensor
#include <Wire.h>

/* --- Defines --- */
#define AZURE_PNP_MODEL_ID "dtmi:profisysmonitor:ProfilaticaHANDS_422;1"

#define SAMPLE_DEVICE_INFORMATION_NAME                 "deviceInformation"
#define SAMPLE_MANUFACTURER_PROPERTY_NAME              "manufacturer"
#define SAMPLE_MODEL_PROPERTY_NAME                     "model"
#define SAMPLE_SOFTWARE_VERSION_PROPERTY_NAME          "swVersion"
#define SAMPLE_OS_NAME_PROPERTY_NAME                   "osName"
#define SAMPLE_PROCESSOR_ARCHITECTURE_PROPERTY_NAME    "processorArchitecture"
#define SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_NAME    "processorManufacturer"
#define SAMPLE_TOTAL_STORAGE_PROPERTY_NAME             "totalStorage"
#define SAMPLE_TOTAL_MEMORY_PROPERTY_NAME              "totalMemory"

#define SAMPLE_MANUFACTURER_PROPERTY_VALUE             "Profilatica"
#define SAMPLE_MODEL_PROPERTY_VALUE                    "Profilatica HANDS"
#define SAMPLE_VERSION_PROPERTY_VALUE                  "1.0.0"
#define SAMPLE_OS_NAME_PROPERTY_VALUE                  "CPP-ESP32"
#define SAMPLE_ARCHITECTURE_PROPERTY_VALUE             "ESP32 C3-M"
#define SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_VALUE   "ESPRESSIF"
// The next couple properties are in KiloBytes.
#define SAMPLE_TOTAL_STORAGE_PROPERTY_VALUE            4096
#define SAMPLE_TOTAL_MEMORY_PROPERTY_VALUE             8192

#define SAMPLE_TOTAL_PROPERTY_REFIL_VALUE              "VlmRefil"
#define TELEMETRY_PROP_NAME_Qnt_Dispensada             "Qnt_Dispensada"
#define TELEMETRY_PROP_NAME_Tag_User                   "Tag_User"
#define TELEMETRY_PROP_NAME_VlmRestante                "VlmRestante"

static az_span COMMAND_NAME_Conf_Vol_Disp = AZ_SPAN_FROM_STR("Conf_Vol_Disp");
static az_span COMMAND_NAME_ResetRefil = AZ_SPAN_FROM_STR("ResetRefil");
static az_span COMMAND_NAME_Set_Refil = AZ_SPAN_FROM_STR("CMD_VlmRefil");
#define COMMAND_RESPONSE_CODE_ACCEPTED                 202
#define COMMAND_RESPONSE_CODE_REJECTED                 404

#define WRITABLE_PROPERTY_TELEMETRY_FREQ_SECS          "telemetryFrequencySecs"
#define WRITABLE_PROPERTY_RESPONSE_SUCCESS             "success"

#define DOUBLE_DECIMAL_PLACE_DIGITS 2

/* --- Function Checks and Returns --- */
#define RESULT_OK       0
#define RESULT_ERROR    __LINE__

#define EXIT_IF_TRUE(condition, retcode, message, ...)                              \
  do                                                                                \
  {                                                                                 \
    if (condition)                                                                  \
    {                                                                               \
      LogError(message, ##__VA_ARGS__ );                                            \
      return retcode;                                                               \
    }                                                                               \
  } while (0)

#define EXIT_IF_AZ_FAILED(azresult, retcode, message, ...)                                   \
  EXIT_IF_TRUE(az_result_failed(azresult), retcode, message, ##__VA_ARGS__ )

/* --- Data --- */
#define DATA_BUFFER_SIZE 1024
static uint8_t data_buffer[DATA_BUFFER_SIZE];
static uint32_t telemetry_send_count = 0;

static size_t telemetry_frequency_in_seconds = 10; // With default frequency of once in 10 seconds.
static time_t last_telemetry_send_time = INDEFINITE_TIME;

static bool led1_on = false;
static bool led2_on = false;

/* --- Function Prototypes --- */
/* Please find the function implementations at the bottom of this file */
static int generate_telemetry_payload(
  uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length);
static int generate_device_info_payload(
  az_iot_hub_client const* hub_client, uint8_t* payload_buffer,
  size_t payload_buffer_size, size_t* payload_buffer_length);
static int consume_properties_and_generate_response(
  azure_iot_t* azure_iot, az_span properties,
  uint8_t* buffer, size_t buffer_size, size_t* response_length);

/* --- Public Functions --- */
void azure_pnp_init()
{
}

const az_span azure_pnp_get_model_id()
{
  return AZ_SPAN_FROM_STR(AZURE_PNP_MODEL_ID);
}

void azure_pnp_set_telemetry_frequency(size_t frequency_in_seconds)
{
  telemetry_frequency_in_seconds = frequency_in_seconds;
  LogInfo("Telemetry frequency set to once every %d seconds.", telemetry_frequency_in_seconds);
}

/* Application-specific data section */
extern bool enviarAzure;
int azure_pnp_send_telemetry(azure_iot_t* azure_iot)
{
  // if(enviarAzure) {
  //   LogError("Chamou pra enviar");
  // };
  _az_PRECONDITION_NOT_NULL(azure_iot);

  //time_t now = time(NULL);

  if (enviarAzure)
  {
    size_t payload_size;
    //last_telemetry_send_time = now;

    if (generate_telemetry_payload(data_buffer, DATA_BUFFER_SIZE, &payload_size) != RESULT_OK)
    {
      LogError("Failed generating telemetry payload.");
      return RESULT_ERROR;
    }

    if (azure_iot_send_telemetry(azure_iot, az_span_create(data_buffer, payload_size)) != 0)
    {
      LogError("Failed sending telemetry.");
      return RESULT_ERROR;
    }
    enviarAzure = false;
  }

  return RESULT_OK;
}

int azure_pnp_send_device_info(azure_iot_t* azure_iot, uint32_t request_id)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  int result;
  size_t length;  
    
  result = generate_device_info_payload(&azure_iot->iot_hub_client, data_buffer, DATA_BUFFER_SIZE, &length);
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed generating telemetry payload.");

  result = azure_iot_send_properties_update(azure_iot, request_id, az_span_create(data_buffer, length));
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed sending reported properties update.");

  return RESULT_OK;
}

extern HTML_SERVER serverhtml;
int azure_pnp_handle_command_request(azure_iot_t* azure_iot, command_request_t command)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  uint16_t response_code;

  if (az_span_is_content_equal(command.command_name, COMMAND_NAME_Conf_Vol_Disp))
  {
    // The payload comes surrounded by quotes, so to remove them we offset the payload by 1 and its size by 2.
    LogInfo("Configurado para dispensar: %.*s ml", az_span_size(command.payload) - 2, az_span_ptr(command.payload) + 1);
    response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
    az_span receiveValue = command.payload;
    uint8_t *strValue = receiveValue._internal.ptr;
    int32_t strValueSize = receiveValue._internal.size;
    String result = "";
    for (int32_t s=1;s<strValueSize-1;s++) {
      result += (char)strValue[s];
    }
    float ejectValue = result.toFloat();
    serverhtml.confEq.confml = ejectValue * 100;
  }
  else if (az_span_is_content_equal(command.command_name, COMMAND_NAME_ResetRefil))
  {
    LogInfo("Reset refil");
    uint8_t piscaLed = 10;
    serverhtml.confEq.refil_vol_rest = serverhtml.confEq.refil_vol;
    serverhtml.saveConfs();
    bool pLed;
    while (piscaLed--)
    {
      digitalWrite(IO_externalLED, pLed);
      pLed = !pLed;
      delay(100);
    }
    digitalWrite(IO_externalLED, 0);
  }
  else if (az_span_is_content_equal(command.command_name, COMMAND_NAME_Set_Refil))
  {
    // COMMAND_NAME_Set_Refil
    LogInfo("Configurado para set vlm refil: %.*s ml", az_span_size(command.payload), az_span_ptr(command.payload));
    az_span receiveValue = command.payload;
    uint8_t *strValue = receiveValue._internal.ptr;
    int32_t strValueSize = receiveValue._internal.size;
    String result = "";
    for (int32_t s = 0; s < strValueSize; s++)
    {
      result += (char)strValue[s];
    }
    serverhtml.confEq.refil_vol = atoi(result.c_str());
    serverhtml.saveConfs();
    uint8_t piscaLed = 10;
    bool pLed;
    while (piscaLed--)
    {
      digitalWrite(IO_externalLED, pLed);
      pLed = !pLed;
      delay(100);
    }
    digitalWrite(IO_externalLED, 0);
  }
  else
  {
    LogError("Command not recognized (%.*s).", az_span_size(command.command_name), az_span_ptr(command.command_name));
    response_code = COMMAND_RESPONSE_CODE_REJECTED;
  }

  return azure_iot_send_command_response(azure_iot, command.request_id, response_code, AZ_SPAN_EMPTY);
}

int azure_pnp_handle_properties_update(azure_iot_t* azure_iot, az_span properties, uint32_t request_id)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);
  _az_PRECONDITION_VALID_SPAN(properties, 1, false);

  int result;
  size_t length;

  result = consume_properties_and_generate_response(azure_iot, properties, data_buffer, DATA_BUFFER_SIZE, &length);
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed generating properties ack payload.");

  result = azure_iot_send_properties_update(azure_iot, request_id, az_span_create(data_buffer, length));
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed sending reported properties update.");

  return RESULT_OK;
}

/* --- Create instance of the DHT11 sensor --- */
//static DHT dht(DHT_PIN, DHT_TYPE);

/* --- Create instance of the sunlight (SI1145) sensor --- */
//static Adafruit_SI1145 uv = Adafruit_SI1145();

void initSensors()
{
    Wire.setPins(SDA, SCL);
    Wire.setClock(400000);
    Wire.begin();
}

extern float volumDisp;
extern String tagRead;
/* --- Internal Functions --- */
static int generate_telemetry_payload(uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length)
{
  az_json_writer jw;
  az_result rc;
  az_span payload_buffer_span = az_span_create(payload_buffer, payload_buffer_size);
  az_span json_span;
  const char *ptr = tagRead.c_str();
  uint8_t sizeStr = 35;
  uint8_t tag_user[35];
  uint8_t cont = 0;
  while (cont < 35)
  {
    tag_user[cont] = tagRead[cont];
    cont++;
  }
  
  az_span text = az_span_create(tag_user, sizeStr);

  // Acquiring data from sensors.

  // The index is multiplied by 100 so to get the
  // integer index, divide by 100!

  rc = az_json_writer_init(&jw, payload_buffer_span, NULL);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed initializing json writer for telemetry.");

  rc = az_json_writer_append_begin_object(&jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed setting telemetry json root.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_Qnt_Dispensada));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding temperature property name to telemetry payload.");
  rc = az_json_writer_append_double(&jw, volumDisp, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding temperature property value to telemetry payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_Tag_User));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pressure property name to telemetry payload.");
  rc = az_json_writer_append_string(&jw, text);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pressure property value to telemetry payload.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_VlmRestante));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pressure property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, serverhtml.confEq.refil_vol_rest);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pressure property value to telemetry payload.");

  rc = az_json_writer_append_end_object(&jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed closing telemetry json payload.");

  payload_buffer_span = az_json_writer_get_bytes_used_in_destination(&jw);

  if ((payload_buffer_size - az_span_size(payload_buffer_span)) < 1)
  {
    LogError("Insufficient space for telemetry payload null terminator.");
    return RESULT_ERROR;
  }

  payload_buffer[az_span_size(payload_buffer_span)] = null_terminator;
  *payload_buffer_length = az_span_size(payload_buffer_span);
 
  return RESULT_OK;
}

static int generate_device_info_payload(az_iot_hub_client const* hub_client, uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length)
{
  az_json_writer jw;
  az_result rc;
  az_span payload_buffer_span = az_span_create(payload_buffer, payload_buffer_size);
  az_span json_span;

  rc = az_json_writer_init(&jw, payload_buffer_span, NULL);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed initializing json writer for telemetry.");

  rc = az_json_writer_append_begin_object(&jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed setting telemetry json root.");
  
  rc = az_iot_hub_client_properties_writer_begin_component(
    hub_client, &jw, AZ_SPAN_FROM_STR(SAMPLE_DEVICE_INFORMATION_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed writting component name.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_MANUFACTURER_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_MANUFACTURER_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_MANUFACTURER_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_MANUFACTURER_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_MODEL_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_MODEL_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_MODEL_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_MODEL_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_TOTAL_PROPERTY_REFIL_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_MODEL_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_int32(&jw, serverhtml.confEq.refil_vol);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_MODEL_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_SOFTWARE_VERSION_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_SOFTWARE_VERSION_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_VERSION_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_VERSION_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_OS_NAME_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_OS_NAME_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_OS_NAME_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_OS_NAME_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_ARCHITECTURE_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_PROCESSOR_ARCHITECTURE_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_ARCHITECTURE_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_ARCHITECTURE_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_TOTAL_STORAGE_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_TOTAL_STORAGE_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_double(&jw, SAMPLE_TOTAL_STORAGE_PROPERTY_VALUE, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_TOTAL_STORAGE_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_TOTAL_MEMORY_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_TOTAL_MEMORY_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_double(&jw, SAMPLE_TOTAL_MEMORY_PROPERTY_VALUE, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_TOTAL_MEMORY_PROPERTY_VALUE to payload. ");

  rc = az_iot_hub_client_properties_writer_end_component(hub_client, &jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed closing component object.");

  rc = az_json_writer_append_end_object(&jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed closing telemetry json payload.");

  payload_buffer_span = az_json_writer_get_bytes_used_in_destination(&jw);

  if ((payload_buffer_size - az_span_size(payload_buffer_span)) < 1)
  {
    LogError("Insufficient space for telemetry payload null terminator.");
    return RESULT_ERROR;
  }

  payload_buffer[az_span_size(payload_buffer_span)] = null_terminator;
  *payload_buffer_length = az_span_size(payload_buffer_span);
 
  return RESULT_OK;
}

static int generate_properties_update_response(
  azure_iot_t* azure_iot,
  az_span component_name, int32_t frequency, int32_t version,
  uint8_t* buffer, size_t buffer_size, size_t* response_length)
{
  az_result azrc;
  az_json_writer jw;
  az_span response = az_span_create(buffer, buffer_size);

  azrc = az_json_writer_init(&jw, response, NULL);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed initializing json writer for properties update response.");

  azrc = az_json_writer_append_begin_object(&jw);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed opening json in properties update response.");

  // This Azure PnP Template does not have a named component,
  // so az_iot_hub_client_properties_writer_begin_component is not needed.

  azrc = az_iot_hub_client_properties_writer_begin_response_status(
    &azure_iot->iot_hub_client,
    &jw,
    AZ_SPAN_FROM_STR(WRITABLE_PROPERTY_TELEMETRY_FREQ_SECS),
    (int32_t)AZ_IOT_STATUS_OK,
    version,
    AZ_SPAN_FROM_STR(WRITABLE_PROPERTY_RESPONSE_SUCCESS));
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed appending status to properties update response.");

  azrc = az_json_writer_append_int32(&jw, frequency);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed appending frequency value to properties update response.");

  azrc = az_iot_hub_client_properties_writer_end_response_status(&azure_iot->iot_hub_client, &jw);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed closing status section in properties update response.");

  // This Azure PnP Template does not have a named component,
  // so az_iot_hub_client_properties_writer_end_component is not needed.

  azrc = az_json_writer_append_end_object(&jw);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed closing json in properties update response.");

  *response_length = az_span_size(az_json_writer_get_bytes_used_in_destination(&jw));

  return RESULT_OK;
}

static int consume_properties_and_generate_response(
  azure_iot_t* azure_iot, az_span properties,
  uint8_t* buffer, size_t buffer_size, size_t* response_length)
{
  int result;
  az_json_reader jr;
  az_span component_name;
  int32_t version = 0;

  az_result azrc = az_json_reader_init(&jr, properties, NULL);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed initializing json reader for properties update.");

  const az_iot_hub_client_properties_message_type message_type =
    AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_TYPE_WRITABLE_UPDATED;

  azrc = az_iot_hub_client_properties_get_properties_version(
    &azure_iot->iot_hub_client, &jr, message_type, &version);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed writable properties version.");

  azrc = az_json_reader_init(&jr, properties, NULL);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed re-initializing json reader for properties update.");

  while (az_result_succeeded(
    azrc = az_iot_hub_client_properties_get_next_component_property(
      &azure_iot->iot_hub_client, &jr, message_type,
      AZ_IOT_HUB_CLIENT_PROPERTY_WRITABLE, &component_name)))
  {
    if (az_json_token_is_text_equal(&jr.token, AZ_SPAN_FROM_STR(WRITABLE_PROPERTY_TELEMETRY_FREQ_SECS)))
    {
      int32_t value;
      azrc = az_json_reader_next_token(&jr);
      EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed getting writable properties next token.");

      azrc = az_json_token_get_int32(&jr.token, &value);
      EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed getting writable properties int32_t value.");

      azure_pnp_set_telemetry_frequency((size_t)value);

      result = generate_properties_update_response(
        azure_iot, component_name, value, version, buffer, buffer_size, response_length);
      EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "generate_properties_update_response failed.");
    }
    else
    {
      LogError("Unexpected property received (%.*s).",
        az_span_size(jr.token.slice), az_span_ptr(jr.token.slice));
    }

    azrc = az_json_reader_next_token(&jr);
    EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed moving to next json token of writable properties.");

    azrc = az_json_reader_skip_children(&jr);
    EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed skipping children of writable properties.");

    azrc = az_json_reader_next_token(&jr);
    EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed moving to next json token of writable properties (again).");
  }

  return RESULT_OK;
}
