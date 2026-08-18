#include "imu.h"

// Initialize the global service instance
LSM6DS3 imuInstance(0x6A);

// Private variables
static float accelX = 0, accelY = 0, accelZ = 0;
static float lastAccelX = 0, lastAccelY = 0, lastAccelZ = 0;
static float gyroX = 0, gyroY = 0, gyroZ = 0, temperatureC = 0;
static bool imuAvailable = false;

bool initIMUService()
{
    if (imuInstance.begin() != IMU_SUCCESS)
    {
        ESP_LOGD("IMU", "Failed to find LSM6DS3TR-C chip");
        imuAvailable = false;
        return false;
    }

    ESP_LOGD("IMU", "LSM6DS3TR-C Found!");
    imuAvailable = true;
    return true;
}

IMUSnapshot getIMUSnapshot()
{
    return {imuAvailable, lastAccelX, lastAccelY, lastAccelZ,
            gyroX, gyroY, gyroZ, temperatureC};
}

void updateIMUReadings()
{
    if (!imuAvailable) return;
    // Read accelerometer data
    accelX = imuInstance.readFloatAccelX();
    accelY = imuInstance.readFloatAccelY();
    accelZ = imuInstance.readFloatAccelZ();
    gyroX = imuInstance.readFloatGyroX();
    gyroY = imuInstance.readFloatGyroY();
    gyroZ = imuInstance.readFloatGyroZ();
    temperatureC = imuInstance.readTempC();

    // Store last readings
    lastAccelX = accelX;
    lastAccelY = accelY;
    lastAccelZ = accelZ;

    if (accelX != 0 || accelY != 0 || accelZ != 0)
    {
        // Serial.println("accelX: " + String(accelX) + " accelY: " + String(accelY) + " accelZ: " + String(accelZ));
    }
}
