#ifndef TEST_MQTT_CLIENT_H
#define TEST_MQTT_CLIENT_H

#include <stdint.h>

typedef int esp_err_t;
constexpr esp_err_t ESP_OK = 0;
typedef const char* esp_event_base_t;
typedef void* esp_mqtt_client_handle_t;

enum esp_mqtt_event_id_t {
  MQTT_EVENT_ANY = -1,
  MQTT_EVENT_CONNECTED,
  MQTT_EVENT_DISCONNECTED,
  MQTT_EVENT_PUBLISHED,
  MQTT_EVENT_ERROR,
  MQTT_EVENT_DATA
};

struct esp_mqtt_error_codes_t {
  int error_type;
};

struct esp_mqtt_event_t {
  esp_mqtt_client_handle_t client;
  int msg_id;
  esp_mqtt_error_codes_t* error_handle;
  int current_data_offset;
  int topic_len;
  char* topic;
};
typedef esp_mqtt_event_t* esp_mqtt_event_handle_t;

struct esp_mqtt_client_config_t {
  const char* uri;
  const char* username;
  const char* password;
  const char* cert_pem;
  int keepalive;
  int buffer_size;
  int task_stack;
  int reconnect_timeout_ms;
};

typedef void (*TestMqttCallback)(void*, esp_event_base_t, int32_t, void*);
const char* esp_err_to_name(esp_err_t);
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t*);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t,
                                       esp_mqtt_event_id_t, TestMqttCallback, void*);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t);
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char*, int);
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t, const char*, const char*,
                            int, int, int, bool);

#endif
