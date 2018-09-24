# OPC UA Publisher on ESP32

It is aimed to compile OPC UA publisher on ESP32 board (xtensa toolchain). Published data will be temperature after finding a proper I2C temperature sensor (open for suggestions :)), currently a dummy test string is published.

    //app_main.c - Lines 146-152 
    static void readTemperature(UA_Server *server, const UA_NodeId nodeid) 
    {
      UA_String temperature = UA_STRING("Temperature as string!"); //Change here as read numeric temperature value
      UA_Variant value;
      UA_Variant_setScalar(&value, &temperature, &UA_TYPES[UA_TYPES_STRING]);
      UA_Server_writeValue(server, nodeid, value);
    }

## Getting Started

It is required to have ESP-IDF (https://github.com/espressif/esp-idf) and this repo can be directly cloned under examples. Open62541 is not required, open62541.c and open62541.h files as component handle the open62541 functionality but it is suggested for those who wants to know more about the OPC UA side.

Also, if you want to use your own open62541.c/.h, it is required to make the changes stated on https://github.com/cmbahadir/opcua-pubsub-esp32/blob/master/main/README after building with the required cmake options, otherwise xtensa-toolchain raises compile errors.

- I haven't analyzed the required modifications on open62541.c/.h  files, these are just a result of trial and error process, so an explanatory contribution would be great. :)

### Prerequisites

- ESP-IDF : https://docs.espressif.com/projects/esp-idf/en/latest/get-started/
- Open62541 (Optional) : https://github.com/open62541/open62541

## License

This project is licensed under the Mozilla Public License Version 2.0 which is inherited from Open62541 project - see the [LICENSE.md](LICENSE.md) file for details
