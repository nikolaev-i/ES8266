#pragma once

#include<Arduino.h>
#include<payload.h>



#define MAX_VALUES 5
#define MAX_TOKEN_LENGTH 20  // Adjust the maximum token length as needed

void read_serial_port(payload_structure *ptr_payload_data);
void processData(String data, payload_structure *ptr_payload_data);
bool isNumeric(const char *str);




