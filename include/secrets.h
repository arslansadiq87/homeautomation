#ifndef HOME_AUTOMATION_SECRETS_H
#define HOME_AUTOMATION_SECRETS_H

#if __has_include("secrets_local.h")
#include "secrets_local.h"
#endif

#ifndef HA_DEFAULT_WIFI_SSID
#define HA_DEFAULT_WIFI_SSID ""
#endif

#ifndef HA_DEFAULT_WIFI_PASSWORD
#define HA_DEFAULT_WIFI_PASSWORD ""
#endif

#ifndef HA_DEFAULT_MDNS_HOSTNAME
#define HA_DEFAULT_MDNS_HOSTNAME "home-automation"
#endif

#ifndef HA_FALLBACK_AP_SSID
#define HA_FALLBACK_AP_SSID "HomeAutomation-Setup"
#endif

#ifndef HA_DEFAULT_SOLAX_ENABLED
#define HA_DEFAULT_SOLAX_ENABLED false
#endif

#ifndef HA_DEFAULT_SOLAX_ADDRESS
#define HA_DEFAULT_SOLAX_ADDRESS "http://192.168.100.23/"
#endif

#ifndef HA_DEFAULT_SOLAX_PASSWORD
#define HA_DEFAULT_SOLAX_PASSWORD ""
#endif

#ifndef HA_DEFAULT_NITROX_ENABLED
#define HA_DEFAULT_NITROX_ENABLED false
#endif

#ifndef HA_DEFAULT_NITROX_HOST
#define HA_DEFAULT_NITROX_HOST "192.168.100.121"
#endif

#ifndef HA_DEFAULT_GROWATT_ENABLED
#define HA_DEFAULT_GROWATT_ENABLED false
#endif

#ifndef HA_DEFAULT_GROWATT_TOKEN
#define HA_DEFAULT_GROWATT_TOKEN ""
#endif

#endif
