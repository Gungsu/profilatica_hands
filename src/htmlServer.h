#ifndef HTMLSERVER
#define HTMLSERVER

#define IO_externalLED 4

#include "FS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

typedef struct
{
    String ssid;
    String password;
    String dps_id_scope;
    String iot_conf_dID;
    String iot_conf_dkey;
} ST_confWifi;

typedef struct
{
    uint16_t refil_vol;
    uint16_t refil_vol_rest;
    float calib_val;
    uint16_t confml;
    uint16_t result_calib_vol;
    uint16_t m_act_dist;
} ST_Refil;

enum
{
    en_CMD_SSID,
    en_CMD_PASSWORD,
    en_CMD_RESET,
    en_CMD_READCONFS,
    en_CMD_CALIB,
    en_CMD_SETREFIL,
    en_CMD_dps_id_scope,
    en_CMD_iot_conf_did,
    en_CMD_iot_conf_dkey,
    en_CMD_help,
    en_CMD_m_act_dist,
    en_CMD_confml,
    en_CMD_reset_refil
} COMMANDS;

class HTML_SERVER {
    public:
        ST_confWifi confWifi;
        ST_Refil confEq;
        void startServer();
        void readConfs();
        void saveConfs();
        String readFile(fs::FS &fs, const char *path);
        void writeFile(fs::FS &fs, const char *path, const char *message);
        HTML_SERVER() {
            SPIFFS.begin();
        }
    private:
        void deserializeJson(fs::FS &fs, const char *path);
        void serializeJson(fs::FS &fs, const char *path);
};

#endif
