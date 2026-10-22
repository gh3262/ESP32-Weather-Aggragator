#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#include <stddef.h>

struct WiFiCredential {
    const char* ssid;
    const char* password;
};

// Copy to credentials.h and replace the empty entries with your networks.
const WiFiCredential wifiNetworks[] = {
    {"", ""},
    {"", ""}
};

const size_t WIFI_NETWORK_COUNT =
    sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);

#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
// POSIX timezone rule, not an IANA location name. Set your local rule here.
#define TZ_STRING "UTC0"

#define AIO_USERNAME ""
#define AIO_KEY ""

#endif
