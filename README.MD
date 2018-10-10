# OPC UA Publisher on ESP32

It is aimed to compile OPC UA publisher on ESP32 board (xtensa toolchain). Published data is the ambient temperature, measured with a DHT-22 Sensor. You can see the sensor code at /components/DHT22.c lines 233 - 238.

    char* ReadTemperature(int sensor_gpio){
        char buf[10];
      char* error[20];
      float temperatureFromSensor;
        int ret_ReadDHT = readDHT();
        if (ret_ReadDHT != DHT_OK){
        *error = "Sensor Error!";
        return *error;
      }
        else {
        setDHTgpio(4);
        temperatureFromSensor = getTemperature();
          gcvt(temperatureFromSensor,4,buf);
        return buf;
      }
    }

## Getting Started

It is required to have ESP-IDF (https://github.com/espressif/esp-idf) and this repo can be directly cloned under examples. Open62541 stack functionaliyu is provided by open62541.c and open62541.h files. If you want to dive into OPC UA part you should check Open62541 project.

Also, if you want to build open62541.c/.h/.lib from source, beware of the changes on https://github.com/cmbahadir/opcua-pubsub-esp32/blob/master/components/open62541lib/README.md in order to be xtensa-toolchain compatible.

- I haven't analyzed the required modifications on open62541.c/.h  files, these are just a result of trial and error process, so an explanatory contribution would be great. :)

### Prerequisites

- ESP-IDF : https://docs.espressif.com/projects/esp-idf/en/latest/get-started/
- Open62541 (Optional) : https://github.com/open62541/open62541

## License

This project is licensed under the Mozilla Public License Version 2.0 which is inherited from Open62541 project - see the [LICENSE.md](LICENSE.md) file for details
