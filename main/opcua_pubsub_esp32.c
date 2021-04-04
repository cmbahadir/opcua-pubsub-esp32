
#include <stdio.h>
#include <sys/param.h>
#include <unistd.h>
#include <lwip/sockets.h>
#include <esp_flash_encrypt.h>
#include "esp_netif.h"
#include <esp_task_wdt.h>
#include <esp_sntp.h>
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "open62541.h"
#include "DHT22.h"
#include "ethernet_connect.h"

/* LOGGING TAGS */
#define MAIN_TAG "APP_MAIN"
#define SNTP_TAG "SNTP"
#define MEMORY_TAG "MEMORY"

/* WIFI CONFIG */
#define DEFAULT_SSID CONFIG_WIFI_SSID
#define DEFAULT_PWD CONFIG_WIFI_PASSWORD

/* DHT SENSOR PIN*/
#define DHT_GPIO 4

/* MDNS ENABLE */
#define ENABLE_MDNS

static UA_Boolean running = true;
static UA_Boolean sntp_initialized = false;
static UA_Boolean isServerCreated = false;
static struct tm timeinfo;
static time_t now = 0;
static void parseTemperature(UA_Server *server, UA_NodeId nodeid);

UA_NodeId connectionIdent, publishedDataSetIdent, writerGroupIdent;

static void
addPubSubConnection(UA_Server *server)
{
    /* Details about the connection configuration and handling are located
     * in the pubsub connection tutorial */
    UA_PubSubConnectionConfig connectionConfig;
    memset(&connectionConfig, 0, sizeof(connectionConfig));
    connectionConfig.name = UA_STRING("UDP-UADP Connection 1");
    connectionConfig.transportProfileUri = UA_STRING("http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp");
    connectionConfig.enabled = UA_TRUE;
    UA_NetworkAddressUrlDataType networkAddressUrl = {UA_STRING_NULL, UA_STRING("opc.udp://224.0.0.22:4840/")};
    UA_Variant_setScalar(&connectionConfig.address, &networkAddressUrl, &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    connectionConfig.publisherId.numeric = UA_UInt32_random();
    UA_StatusCode addPBConnStat = UA_Server_addPubSubConnection(server, &connectionConfig, &connectionIdent);
    printf("Add PubSub Connection Status Code: %d\n", addPBConnStat);
}

/**
 * **PublishedDataSet handling**
 * The PublishedDataSet (PDS) and PubSubConnection are the toplevel entities and can exist alone. The PDS contains
 * the collection of the published fields.
 * All other PubSub elements are directly or indirectly linked with the PDS or connection.
 */
static void
addPublishedDataSet(UA_Server *server)
{
    /* The PublishedDataSetConfig contains all necessary public
    * informations for the creation of a new PublishedDataSet */
    UA_PublishedDataSetConfig publishedDataSetConfig;
    memset(&publishedDataSetConfig, 0, sizeof(UA_PublishedDataSetConfig));
    publishedDataSetConfig.publishedDataSetType = UA_PUBSUB_DATASET_PUBLISHEDITEMS;
    publishedDataSetConfig.name = UA_STRING("Demo PDS");
    /* Create new PublishedDataSet based on the PublishedDataSetConfig. */
    UA_AddPublishedDataSetResult s = UA_Server_addPublishedDataSet(server, &publishedDataSetConfig, &publishedDataSetIdent);
    printf("Add PubSub Connection Status Code: %d\n", s.addResult);
}

/**
 * **DataSetField handling**
 * The DataSetField (DSF) is part of the PDS and describes exactly one published field.
 */
static void
addDataSetField(UA_Server *server)
{
    /* Add a field to the previous created PublishedDataSet */
    UA_NodeId createdNodeId;
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.minimumSamplingInterval = 0.000000;
    attr.userAccessLevel = 3;
    attr.accessLevel = 3;
    attr.valueRank = -1;
    attr.dataType = UA_NODEID_NUMERIC(0, 12); //6 for INT32
    UA_String classVar = UA_STRING("Temperature: 21 C");
    UA_Variant_setScalar(&attr.value, &classVar, &UA_TYPES[UA_TYPES_STRING]);

    UA_Server_addNode_begin(server,
                            UA_NODECLASS_VARIABLE,
                            UA_NODEID_NUMERIC(1, 6001),
                            UA_NODEID_NUMERIC(0, UA_NS0ID_PUBLISHSUBSCRIBE),
                            UA_NODEID_NUMERIC(0, 47),
                            UA_QUALIFIEDNAME(1, "Test"),
                            UA_NODEID_NUMERIC(0, 63),
                            (const UA_NodeAttributes *)&attr,
                            &UA_TYPES[UA_TYPES_VARIABLEATTRIBUTES],
                            NULL,
                            &createdNodeId);

    parseTemperature(server, createdNodeId);
    UA_NodeId dataSetFieldIdent;
    UA_DataSetFieldConfig dataSetFieldConfig;
    memset(&dataSetFieldConfig, 0, sizeof(UA_DataSetFieldConfig));
    dataSetFieldConfig.dataSetFieldType = UA_PUBSUB_DATASETFIELD_VARIABLE;
    dataSetFieldConfig.field.variable.fieldNameAlias = UA_STRING("Server localtime");
    dataSetFieldConfig.field.variable.promotedField = UA_FALSE;
    dataSetFieldConfig.field.variable.publishParameters.publishedVariable = createdNodeId;
    dataSetFieldConfig.field.variable.publishParameters.attributeId = UA_ATTRIBUTEID_VALUE;

    UA_Server_addDataSetField(server, publishedDataSetIdent, &dataSetFieldConfig, &dataSetFieldIdent);
}

/**
 * **WriterGroup handling**
 * The WriterGroup (WG) is part of the connection and contains the primary configuration
 * parameters for the message creation.
 */
static void
addWriterGroup(UA_Server *server)
{
    /* Now we create a new WriterGroupConfig and add the group to the existing PubSubConnection. */
    UA_WriterGroupConfig writerGroupConfig;
    memset(&writerGroupConfig, 0, sizeof(UA_WriterGroupConfig));
    writerGroupConfig.name = UA_STRING("Demo WriterGroup");
    writerGroupConfig.publishingInterval = 100;
    writerGroupConfig.enabled = UA_FALSE;
    writerGroupConfig.writerGroupId = 100;
    writerGroupConfig.encodingMimeType = UA_PUBSUB_ENCODING_UADP;
    /* The configuration flags for the messages are encapsulated inside the
     * message- and transport settings extension objects. These extension objects
     * are defined by the standard. e.g. UadpWriterGroupMessageDataType */
    UA_Server_addWriterGroup(server, connectionIdent, &writerGroupConfig, &writerGroupIdent);
}

/**
 * **DataSetWriter handling**
 * A DataSetWriter (DSW) is the glue between the WG and the PDS. The DSW is linked to exactly one
 * PDS and contains additional informations for the message generation.
 */
static void
addDataSetWriter(UA_Server *server)
{
    /* We need now a DataSetWriter within the WriterGroup. This means we must
     * create a new DataSetWriterConfig and add call the addWriterGroup function. */
    UA_NodeId dataSetWriterIdent;
    UA_DataSetWriterConfig dataSetWriterConfig;
    memset(&dataSetWriterConfig, 0, sizeof(UA_DataSetWriterConfig));
    dataSetWriterConfig.name = UA_STRING("Demo DataSetWriter");
    dataSetWriterConfig.dataSetWriterId = 62541;
    dataSetWriterConfig.keyFrameCount = 10;
    UA_Server_addDataSetWriter(server, writerGroupIdent, publishedDataSetIdent,
                               &dataSetWriterConfig, &dataSetWriterIdent);
}

static void parseTemperature(UA_Server *server, const UA_NodeId nodeid)
{
    float temp;
    char *buf = UA_malloc(sizeof(char) * 512);
    temp = ReadTemperature(DHT_GPIO);
    printf("Returned Temperature: %.6f\n", temp);
    snprintf(buf, 512, "%f", temp);
    printf("Read Temperature : %s\n", buf);
    UA_String temperature = UA_STRING(buf);

    //UA_String temperature = UA_STRING("Temperature as string!"); //Change here as read numeric temperature value
    UA_Variant value;
    UA_Variant_setScalar(&value, &temperature, &UA_TYPES[UA_TYPES_STRING]);
    UA_Server_writeValue(server, nodeid, value);
}

static UA_StatusCode
UA_ServerConfig_setUriName(UA_ServerConfig *uaServerConfig, const char *uri, const char *name)
{
    // delete pre-initialized values
    UA_String_clear(&uaServerConfig->applicationDescription.applicationUri);
    UA_LocalizedText_clear(&uaServerConfig->applicationDescription.applicationName);

    uaServerConfig->applicationDescription.applicationUri = UA_String_fromChars(uri);
    uaServerConfig->applicationDescription.applicationName.locale = UA_STRING_NULL;
    uaServerConfig->applicationDescription.applicationName.text = UA_String_fromChars(name);

    for (size_t i = 0; i < uaServerConfig->endpointsSize; i++)
    {
        UA_String_clear(&uaServerConfig->endpoints[i].server.applicationUri);
        UA_LocalizedText_clear(
            &uaServerConfig->endpoints[i].server.applicationName);

        UA_String_copy(&uaServerConfig->applicationDescription.applicationUri,
                       &uaServerConfig->endpoints[i].server.applicationUri);

        UA_LocalizedText_copy(&uaServerConfig->applicationDescription.applicationName,
                              &uaServerConfig->endpoints[i].server.applicationName);
    }

    return UA_STATUSCODE_GOOD;
}

void opcua_task(void *pvParameter)
{
    UA_Int32 sendBufferSize = 16384;
    UA_Int32 recvBufferSize = 16384;

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    UA_Server *server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setDefault(config);
    ESP_LOGI(MAIN_TAG, "Fire up OPC UA Server.");

    UA_ServerConfig_setMinimalCustomBuffer(config, 4840, 0, sendBufferSize, recvBufferSize);
    
    UA_ServerConfig_addPubSubTransportLayer(config, UA_PubSubTransportLayerUDPMP());

    const char *appUri = "open62541.esp32.server";
    UA_String hostName = UA_STRING("opcua-esp32");
    
#ifdef ENABLE_MDNS
    config->mdnsEnabled = true;
    config->mdnsConfig.mdnsServerName = UA_String_fromChars(appUri);
    config->mdnsConfig.serverCapabilitiesSize = 2;
    UA_String *caps = (UA_String *)UA_Array_new(2, &UA_TYPES[UA_TYPES_STRING]);
    caps[0] = UA_String_fromChars("LDS");
    caps[1] = UA_String_fromChars("NA");
    config->mdnsConfig.serverCapabilities = caps;
    // We need to set the default IP address for mDNS since internally it's not able to detect it.
    tcpip_adapter_ip_info_t default_ip;
    
    #ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    tcpip_adapter_if_t tcpip_if = TCPIP_ADAPTER_IF_ETH;
    #else
    tcpip_adapter_if_t tcpip_if = TCPIP_ADAPTER_IF_STA;
    #endif

    esp_err_t ret = tcpip_adapter_get_ip_info(tcpip_if, &default_ip);
    if ((ESP_OK == ret) && (default_ip.ip.addr != INADDR_ANY))
    {
        config->mdnsIpAddressListSize = 1;
        config->mdnsIpAddressList = (uint32_t *)UA_malloc(sizeof(uint32_t) * config->mdnsIpAddressListSize);
        memcpy(config->mdnsIpAddressList, &default_ip.ip.addr, sizeof(uint32_t));
    }
    else
    {
        ESP_LOGI(MAIN_TAG, "Could not get default IP Address!");
    }
#endif
    UA_ServerConfig_setUriName(config, appUri, "OPC_UA_Server_ESP32");
    UA_ServerConfig_setCustomHostname(config, hostName);

    /* Add Pub/Sub Datasets */
    addPubSubConnection(server);
    addPublishedDataSet(server);
    addDataSetField(server);
    addWriterGroup(server);
    addDataSetWriter(server);

    ESP_LOGI(MAIN_TAG, "Heap Left : %d", xPortGetFreeHeapSize());
    UA_StatusCode retval = UA_Server_run_startup(server);
    if (retval == UA_STATUSCODE_GOOD)
    {
        while (running)
        {
            UA_Server_run_iterate(server, false);
            ESP_ERROR_CHECK(esp_task_wdt_reset());
            taskYIELD();
        }
        UA_Server_run_shutdown(server);
    }
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(SNTP_TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
    ESP_LOGI(SNTP_TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
    sntp_initialized = true;
}

static bool get_time(void)
{
    initialize_sntp();
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    memset(&timeinfo, 0, sizeof(struct tm));
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry <= retry_count)
    {
        ESP_LOGI(SNTP_TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
    return timeinfo.tm_year > (2016 - 1900);
}

static void opc_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (sntp_initialized != true)
    {
        if (timeinfo.tm_year < (2016 - 1900))
        {
            ESP_LOGI(SNTP_TAG, "Time is not set yet. Settting up network connection and getting time over NTP.");
            if (!get_time())
            {
                ESP_LOGE(SNTP_TAG, "Could not get time from NTP. Using default timestamp.");
            }
            time(&now);
        }
        localtime_r(&now, &timeinfo);
        ESP_LOGI(SNTP_TAG, "Current time: %d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }

    if (!isServerCreated)
    {
        xTaskCreatePinnedToCore(opcua_task, "opcua_task", 24336, NULL, 10, NULL, 0);
        ESP_LOGI(MEMORY_TAG, "Heap size after OPC UA Task : %d", esp_get_free_heap_size());
        isServerCreated = true;
    }
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
}

static void connect_scan(void)
{
    // tcpip_adapter_init();
    // ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );

    // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // ESP_ERROR_CHECK( esp_wifi_init(&cfg));
    // wifi_config_t wifi_config = {
    //     .sta = {
    //         .ssid = DEFAULT_SSID,
    //         .password = DEFAULT_PWD
    //     },
    // };

    // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    // ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_task_wdt_init(10, true));
    ESP_ERROR_CHECK(esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0)));
    ESP_ERROR_CHECK(esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1)));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &opc_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, NULL));

    ESP_ERROR_CHECK(example_connect());
}

void app_main(void)
{

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    connect_scan();

    //xTaskCreate(&opcua_task, "opcua_task", configMINIMAL_STACK_SIZE, NULL, 5, NULL);
}
