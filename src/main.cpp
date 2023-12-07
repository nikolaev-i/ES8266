// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Hub sample for ESPRESSIF ESP8266 board.
 * It uses our Azure Embedded SDK for C to help interact with Azure IoT.
 * For reference, please visit https://github.com/azure/azure-sdk-for-c.
 *
 * To connect and work with Azure IoT Hub you need an MQTT client, connecting, subscribing
 * and publishing to specific topics to use the messaging features of the hub.
 * Our azure-sdk-for-c is an MQTT client support library, helping to compose and parse the
 * MQTT topic names and messages exchanged with the Azure IoT Hub.
 *
 * This sample performs the following tasks:
 * - Synchronize the device clock with a NTP server;
 * - Initialize our "az_iot_hub_client" (struct for data, part of our azure-sdk-for-c);
 * - Initialize the MQTT client (here we use Nick Oleary's PubSubClient, which also handle the tcp
 * connection and TLS);
 * - Connect the MQTT client (using server-certificate validation, SAS-tokens for client
 * authentication);
 * - Periodically send telemetry data to the Azure IoT Hub.
 *
 * To properly connect to your Azure IoT Hub, please fill the information in the `iot_configs.h`
 * file.
 */

// C99 libraries
#include <cstdlib>
#include <stdbool.h>
#include <string.h>
#include <time.h>

// Libraries for MQTT client, WiFi connection and SAS-token generation.
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <bearssl/bearssl.h>
#include <bearssl/bearssl_hmac.h>
#include <libb64/cdecode.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include <config.h>
#include <payload.h>
#include <processing_functions.h>





// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp8266)"

// Utility macros and defines
#define LED_PIN 2
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define ONE_HOUR_IN_SECS 3600
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_PACKET_SIZE 1024



// Translate iot_configs.h defines into variables used by the sample
static const char *ssid = IOT_CONFIG_WIFI_SSID;
static const char *password = IOT_CONFIG_WIFI_PASSWORD;
static const char *host = IOT_CONFIG_IOTHUB_FQDN;
static const char *device_id = IOT_CONFIG_DEVICE_ID;
static const char *device_key = IOT_CONFIG_DEVICE_KEY;
static const int port = 8883;
static const int timezone = -3;
// Memory allocated for the sample's variables and structures.
static WiFiClientSecure wifi_client;
static X509List cert((const char *)ca_pem);
static PubSubClient mqtt_client(wifi_client);
static az_iot_hub_client client;
static char sas_token[200];
static uint8_t signature[512];
static unsigned char encrypted_signature[32];
static char base64_decoded_device_key[32];
static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint8_t telemetry_payload[1024];
static uint32_t telemetry_send_count = 0;
az_iot_message_properties properties;
az_result result;
payload_structure payload_data;
// Auxiliary functions






static void connectToWiFi()
{
  Serial.begin(115200);
  Serial.println();
  Serial.print("Connecting to WIFI SSID ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.print("WiFi connected, IP address: ");
  Serial.println(WiFi.localIP());
}

static void initializeTime()
{
  Serial.print("Setting time using SNTP");
  
  configTime(timezone * 3600, 0, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < 1510592825)
  {
    delay(500);
    Serial.print(".");
    now = time(NULL);
  }
  Serial.println("done!");
}

static char *getCurrentLocalTimeString()
{
  time_t now = time(NULL);
  return ctime(&now);
}

static void printCurrentTime()
{
  Serial.print("Current time: ");
  Serial.print(getCurrentLocalTimeString());
}

void receivedCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

static void initializeClients()
{
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  wifi_client.setTrustAnchors(&cert);
  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((uint8_t *)host, strlen(host)),
          az_span_create((uint8_t *)device_id, strlen(device_id)),
          &options)))
  {
    Serial.println("Failed initializing Azure IoT Hub client");
    return;
  }

  mqtt_client.setServer(host, port);
  mqtt_client.setCallback(receivedCallback);
}

/*
 * @brief           Gets the number of seconds since UNIX epoch until now.
 * @return uint32_t Number of seconds.
 */
static uint32_t getSecondsSinceEpoch() { return (uint32_t)time(NULL); }





static int generateSasToken(char *sas_token, size_t size)
{
  az_span signature_span = az_span_create((uint8_t *)signature, sizeofarray(signature));
  az_span out_signature_span;
  az_span encrypted_signature_span = az_span_create((uint8_t *)encrypted_signature, sizeofarray(encrypted_signature));

  uint32_t expiration = getSecondsSinceEpoch() + ONE_HOUR_IN_SECS;

  // Get signature
  if (az_result_failed(az_iot_hub_client_sas_get_signature(
          &client, expiration, signature_span, &out_signature_span)))
  {
    Serial.println("Failed getting SAS signature");
    return 1;
  }




  // Base64-decode device key
  int base64_decoded_device_key_length = base64_decode_chars(device_key, strlen(device_key), base64_decoded_device_key);

  if (base64_decoded_device_key_length == 0)
  {
    Serial.println("Failed base64 decoding device key");
    return 1;
  }

  // SHA-256 encrypt
  br_hmac_key_context kc;
  br_hmac_key_init(
      &kc, &br_sha256_vtable, base64_decoded_device_key, base64_decoded_device_key_length);

  br_hmac_context hmac_ctx;
  br_hmac_init(&hmac_ctx, &kc, 32);
  br_hmac_update(&hmac_ctx, az_span_ptr(out_signature_span), az_span_size(out_signature_span));
  br_hmac_out(&hmac_ctx, encrypted_signature);

  // Base64 encode encrypted signature
  String b64enc_hmacsha256_signature = base64::encode(encrypted_signature, br_hmac_size(&hmac_ctx));

  az_span b64enc_hmacsha256_signature_span = az_span_create(
      (uint8_t *)b64enc_hmacsha256_signature.c_str(), b64enc_hmacsha256_signature.length());

  // URl-encode base64 encoded encrypted signature
  if (az_result_failed(az_iot_hub_client_sas_get_password(
          &client,
          expiration,
          b64enc_hmacsha256_signature_span,
          AZ_SPAN_EMPTY,
          sas_token,
          size,
          NULL))) {
    Serial.println("Failed getting SAS token");
    return 1;
  }

  return 0;
}

static int connectToAzureIoTHub()
{
  size_t client_id_length;
  char mqtt_client_id[128];
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Serial.println("Failed getting client id");
    return 1;
  }

  mqtt_client_id[client_id_length] = '\0';

  char mqtt_username[128];
  // Get the MQTT user name used to connect to IoT Hub
  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqtt_username, sizeofarray(mqtt_username), NULL)))
  {
    printf("Failed to get MQTT clientId, return code\n");
    return 1;
  }

  Serial.print("Client ID: ");
  Serial.println(mqtt_client_id);

  Serial.print("Username: ");
  Serial.println(mqtt_username);

  mqtt_client.setBufferSize(MQTT_PACKET_SIZE);

  while (!mqtt_client.connected())
  {
    time_t now = time(NULL);

    Serial.print("MQTT connecting ... ");

    if (mqtt_client.connect(mqtt_client_id, mqtt_username, sas_token))
    {
      Serial.println("connected.");
    }
    else
    {
      Serial.print("failed, status code =");
      Serial.print(mqtt_client.state());
      Serial.println(". Trying again in 5 seconds.");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }

  mqtt_client.subscribe(AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC);

  return 0;
}

static void establishConnection()
{
  connectToWiFi();
  initializeTime();
  printCurrentTime();
  initializeClients();

  // The SAS token is valid for 1 hour by default in this sample.
  // After one hour the sample must be restarted, or the client won't be able
  // to connect/stay connected to the Azure IoT Hub.
  if (generateSasToken(sas_token, sizeofarray(sas_token)) != 0)
  {
    Serial.println("Failed generating MQTT password");
  }
  else
  {
    connectToAzureIoTHub();
  }

  digitalWrite(LED_PIN, LOW);
}

az_iot_message_properties add_properties()
{
  // Allocate a span to put the properties
  uint8_t property_buffer[128]; // Increased buffer size to accommodate additional properties
  az_span property_span = az_span_create(property_buffer, sizeof(property_buffer));

  // Initialize the property struct with the span
  az_iot_message_properties props;
  az_iot_message_properties_init(&props, property_span, 0);


  // Append UTF-8 property
  az_iot_message_properties_append(&props, AZ_SPAN_FROM_STR("content-encoding"), AZ_SPAN_FROM_STR("utf-8"));

  // Append content/json property
  az_iot_message_properties_append(&props, AZ_SPAN_FROM_STR("content-type"), AZ_SPAN_FROM_STR("application/json"));
  
  // Return the properties struct
  return properties;
}



static char *getTelemetryPayload(payload_structure *payload_data)
{
az_span temp_span = az_span_create(telemetry_payload, sizeof(telemetry_payload));
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("{ \"msgCount\": "));
(void)az_span_u32toa(temp_span, telemetry_send_count++, &temp_span);



// ------------------- Sensor - 1 ---------------------//
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"sensor_1_type\": \""));
(void)az_span_i32toa(temp_span, payload_data->sensor_1_type, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("\", \"sensor_1_temperature\": "));
(void)az_span_i32toa(temp_span, payload_data->sensor_1_temperature, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"sensors_1_humidity\": "));
(void)az_span_i32toa(temp_span, payload_data->sensors_1_humidity, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"sensor_1_light\": "));
(void)az_span_i32toa(temp_span, payload_data->sensor_1_light, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"sensor_1_CO2\": "));
(void)az_span_i32toa(temp_span, payload_data->sensor_1_CO2, &temp_span);

// ------------------- Sensor - 2 ---------------------//
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"sensor_2_type\": \""));
(void)az_span_i32toa(temp_span, payload_data->sensor_2_type, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("\", \"sensor_2_temperature\": "));
(void)az_span_i32toa(temp_span, payload_data->sensor_2_temperature, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"sensors_2_humidity\": "));
(void)az_span_i32toa(temp_span, payload_data->sensors_2_humidity, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"sensor_2_light\": "));
(void)az_span_i32toa(temp_span, payload_data->sensor_2_light, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"sensor_2_CO2\": "));
(void)az_span_i32toa(temp_span, payload_data->sensor_2_CO2, &temp_span);

// ------------------- Fan - 1 ---------------------//
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"fan_1_type\": \""));
(void)az_span_i32toa(temp_span, payload_data->fan_1_type, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("\", \"fan_1_set_percent\": "));
(void)az_span_i32toa(temp_span, payload_data->fan_1_set_percent, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"fan_1_speed\": "));
(void)az_span_i32toa(temp_span, payload_data->fan_1_speed, &temp_span);


// ------------------- Fan - 2 ---------------------//
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"fan_2_type\": \""));
(void)az_span_i32toa(temp_span, payload_data->fan_2_type, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("\", \"fan_2_set_percent\": "));
(void)az_span_i32toa(temp_span, payload_data->fan_2_set_percent, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"fan_2_speed\": "));
(void)az_span_i32toa(temp_span, payload_data->fan_2_speed, &temp_span);



// ------------------- Relay - CO2 ---------------------//
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"relay_CO2\": "));
(void)az_span_i32toa(temp_span, payload_data->relay_CO2, &temp_span);

// ------------------- Relay - Programmable 1 ---------------------//
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"relay_programmable_1\": \""));
(void)az_span_i32toa(temp_span, payload_data->relay_programmable_1, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("\""));

// ------------------- Relay - Programmable 2 ---------------------//
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"relay_programmable_2\": \""));
(void)az_span_i32toa(temp_span, payload_data->relay_programmable_2, &temp_span);
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("\""));

// ------------------- PWM - Light---------------------//
temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(", \"pwm_light\": "));
(void)az_span_i32toa(temp_span, payload_data->pwm_light, &temp_span);

temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" }"));
temp_span = az_span_copy_u8(temp_span, '\0');



  return (char *)telemetry_payload;
}


static void sendTelemetry()
{

///

uint8_t property_buffer[64];  // Increased buffer size to accommodate additional properties
az_span property_span = az_span_create(property_buffer, sizeof(property_buffer));
// Initialize the property struct with the span
az_iot_message_properties props;
az_iot_message_properties_init(&props, property_span, 0);
// Append UTF-8 property
az_iot_message_properties_append(&props, AZ_SPAN_FROM_STR(AZ_IOT_MESSAGE_PROPERTIES_CONTENT_TYPE), AZ_SPAN_LITERAL_FROM_STR("application%2Fjson"));
// Append content/json property
az_iot_message_properties_append(&props, AZ_SPAN_FROM_STR(AZ_IOT_MESSAGE_PROPERTIES_CONTENT_ENCODING), AZ_SPAN_LITERAL_FROM_STR("UTF-8"));

az_iot_message_properties *ptr_props;
ptr_props = &props;

///
// az_iot_message_properties temp_properties = add_properties();
// const az_iot_message_properties* properties = &temp_properties;
  digitalWrite(LED_PIN, HIGH);
  Serial.print(millis());
  Serial.print(" ESP8266 Sending telemetry . . . ");
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, ptr_props, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Serial.println("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  mqtt_client.publish(telemetry_topic, getTelemetryPayload(&payload_data), false);

  Serial.println("OK");
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

// Arduino setup and loop main functions.

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  establishConnection();
}

void loop()
{
  read_serial_port(&payload_data); //read data from serial -> processing_functions
  if (millis() > next_telemetry_send_time_ms)
  {
    // Check if connected, reconnect if needed.
    if (!mqtt_client.connected())
    {

      establishConnection();
    }
sendTelemetry();
Serial.println("we got here");
    next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
  }

  // MQTT loop must be called to process Device-to-Cloud and Cloud-to-Device.
  mqtt_client.loop();
  Serial.println("did we loop ?");
  delay(1000);
}



