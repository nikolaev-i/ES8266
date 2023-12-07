#ifndef PAYLOAD_DATA_H
#define PAYLOAD_DATA_H

#include <az_core.h>
#include <az_iot.h>


typedef struct  {
// data-time TODO

// ----Sensor-1---- //
uint8_t sensor_1_type ;
uint16_t sensor_1_temperature;  // negative ?
uint8_t sensors_1_humidity;
uint8_t sensor_1_light;  // opt
uint16_t sensor_1_CO2;  // opt
// ----Sensor-2---- //
uint8_t sensor_2_type ;
uint16_t sensor_2_temperature;  // negative ?
uint8_t sensors_2_humidity;
uint8_t sensor_2_light;  // opt
uint16_t sensor_2_CO2;  // opt
// ----Fan-1---- //
uint8_t fan_1_type ;
uint8_t fan_1_set_percent;
uint16_t fan_1_speed;

// ----Fan-2---- //
uint8_t fan_2_type ;
uint8_t fan_2_set_percent;
uint16_t fan_2_speed;

// ---- Outputs --- //

uint8_t relay_CO2;
uint8_t relay_programmable_1;
uint8_t relay_programmable_2;
uint8_t pwm_light;
} payload_structure;


extern payload_structure payload_data;


#endif

