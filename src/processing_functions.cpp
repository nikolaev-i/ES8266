#include <processing_functions.h>

#define MAX_TOTAL_LENGTH 20
// |sensor_1_type|sensor_1_temp|sensor_1_humidity|sensor_1_light|sensor_1_co|

#define MAX_TOKEN_LENGTH 20  // Adjust the maximum token length as needed


void read_serial_port(payload_structure *ptr_payload_data) {
  // Check if there is data available in the serial buffer
  if (Serial.available() > 0) {
    String receivedData = Serial.readStringUntil('\n');  // Read the string until a newline character is encountered

    // Process the received data
    processData(receivedData, ptr_payload_data);
  }

  // Your non-blocking code can continue here
}

void processData(String data, payload_structure *ptr_payload_data) {

int values[20];

  // Tokenize the string using commas as separators
char *token = strtok(const_cast<char*>(data.c_str()), ",");   // NOLINT
  // Loop through each token and convert it to an integer
  for (int i = 0; i < MAX_TOKEN_LENGTH && token != NULL; i++) {
     values[i] = atoi(token);
      token = strtok(NULL, ",");
}



ptr_payload_data->sensor_1_type = values[0];
ptr_payload_data->sensor_1_temperature = values[1];
ptr_payload_data->sensors_1_humidity = values[2];
ptr_payload_data->sensor_1_light = values[3];
ptr_payload_data->sensor_1_CO2 = values[4];

      //
ptr_payload_data->sensor_2_type = values[5];
ptr_payload_data->sensor_2_temperature = values[6];
ptr_payload_data->sensors_2_humidity = values[7];
ptr_payload_data->sensor_2_light = values[8];
ptr_payload_data->sensor_2_CO2 = values[9];

ptr_payload_data->fan_1_type = values[10];
ptr_payload_data->fan_1_set_percent = values[11];
ptr_payload_data->fan_1_speed = values[12];

ptr_payload_data->fan_2_type = values[13];
ptr_payload_data->fan_2_set_percent = values[14];
ptr_payload_data->fan_2_speed = values[15];

ptr_payload_data->relay_CO2 = values[16];
ptr_payload_data->relay_programmable_1 = values[17];
ptr_payload_data->relay_programmable_2 = values[18];
ptr_payload_data->pwm_light = values[19];


}