#ifndef IMU_SERVICE_H
#define IMU_SERVICE_H

#include <Arduino.h>
#include "SparkFunLSM6DS3.h"

// Declare the global service instance
extern LSM6DS3 imuInstance;

struct IMUSnapshot {
    bool available;
    float accelX;
    float accelY;
    float accelZ;
    float gyroX;
    float gyroY;
    float gyroZ;
    float temperatureC;
};

// Function declarations
bool initIMUService();
void updateIMUReadings();
IMUSnapshot getIMUSnapshot();

#endif // IMU_SERVICE_H
