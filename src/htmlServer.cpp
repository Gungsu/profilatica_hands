#include "htmlServer.h"

AsyncWebServer server(80);
const char *PARAM_MESSAGE = "action";

void HTML_SERVER::serializeJson(fs::FS &fs, const char *path)
{
    char text[350] = "";
    String allthetext = "";
    memset(text, '\0', 350);
    allthetext = "{\r\n";
    allthetext += "\"ssid\": \"" + confWifi.ssid + "\",\n\"pass\": \"" + confWifi.password + "\",\n";
    allthetext += "\"refil\": \"" + String(confEq.refil_vol) + "\",\n\"refil_vRest\": \"" + String(confEq.refil_vol_rest) + "\",\n";
    allthetext += "\"calib\": \""+String(confEq.calib_val,3U)+"\",\n";
    allthetext += "\"confml\": \""+String(confEq.confml)+"\"\n";
    allthetext += "\"dps_id_scope\": \"" + String(confWifi.dps_id_scope) + "\",\n";
    allthetext += "\"iot_conf_dID\": \"" + String(confWifi.iot_conf_dID) + "\",\n";
    allthetext += "\"iot_conf_dKey\": \"" + String(confWifi.iot_conf_dkey) + "\",\n";
    allthetext += "\"m_act_dist\": \"" + String(confEq.m_act_dist) + "\"\n";
    allthetext += "}\r\n";
    strcpy(text, allthetext.c_str());
    allthetext = "";
    writeFile(SPIFFS, path, text);
}

void HTML_SERVER::deserializeJson(fs::FS &fs, const char *path)
{
    File file = fs.open(path);
    if (!file || file.isDirectory())
    {
        Serial.println("- failed to open file for reading");
        return;
    }
    uint8_t lineCont = 0;
    while (file.available())
    {
        String line = file.readStringUntil('\n');
        lineCont++;
        int8_t pos = line.indexOf(':');

        if (pos != -1)
        {
            String parte1 = line.substring(1, pos - 1);
            int8_t x = line.indexOf(',');
            String parte2 = line.substring(pos + 3, line.length() - 1);
            int8_t p = parte2.indexOf("\"");
            parte2 = parte2.substring(0,p);
            if (parte1 == "ssid")
            {
                confWifi.ssid = parte2;
            }
            else if (parte1 == "pass")
            {
                confWifi.password = parte2;
            }
            else if (parte1 == "refil")
            {
                confEq.refil_vol = atoi(parte2.c_str());
            }
            else if (parte1 == "refil_vRest")
            {
                confEq.refil_vol_rest = atoi(parte2.c_str());
            }
            else if (parte1 == "calib") {
                confEq.calib_val = atof(parte2.c_str());
            }
            else if(parte1 == "confml") {
                confEq.confml = atoi(parte2.c_str());
            }
            else if (parte1 == "dps_id_scope")
            {
                confWifi.dps_id_scope = parte2;
            }
            else if (parte1 == "iot_conf_dID")
            {
                confWifi.iot_conf_dID = parte2;
            }
            else if (parte1 == "iot_conf_dKey")
            {
                confWifi.iot_conf_dkey = parte2;
            }
            else if (parte1 == "m_act_dist") {
                confEq.m_act_dist = atoi(parte2.c_str());
            }
            confEq.result_calib_vol = confEq.confml * confEq.calib_val;
        }
    }
    file.close(); // Fecha o arquivo
}

String HTML_SERVER::readFile(fs::FS &fs, const char *path)
{
    String fileContent;
    // Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory())
    {
        // Serial.println("- failed to open file for reading");
        return "";
    }

    // Serial.println("- read from file:");
    while (file.available())
    {
        fileContent += file.readString();
    }
    file.close();
    return fileContent;
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\r\n", dirname);
    File root = fs.open(dirname);
    if (!root)
    {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory())
    {
        Serial.println(" - not a directory");
        return;
    }
    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels)
            {
                listDir(fs, file.name(), levels - 1);
            }
        }
        else
        {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void HTML_SERVER::readConfs() {
    deserializeJson(SPIFFS,"/ssid_conf.json");
    // Serial.println(confWifi.ssid);
    // Serial.println(confWifi.password);
    // Serial.println(confEq.refil_vol);
    // Serial.println(confEq.refil_vol_rest);
}
void HTML_SERVER::saveConfs() {
    serializeJson(SPIFFS,"/ssid_conf.json");
    // Serial.println(confWifi.ssid);
    // Serial.println(confWifi.password);
    // Serial.println(confEq.refil_vol);
    // Serial.println(confEq.refil_vol_rest);
}

void HTML_SERVER::writeFile(fs::FS &fs, const char *path, const char *message)
{
    // Serial.printf("Writing file: %s\r\n", path);
    File file = fs.open(path, "w");
    if (!file)
    {
        Serial.println("- failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
        Serial.println("- file written");
    }
    else
    {
        Serial.println("- write failed");
    }
    file.close();
}

void notFound(AsyncWebServerRequest *request)
{
    request->send(404, "text/plain", "Not found");
}

void HTML_SERVER::startServer()
{
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "text/plain", "Hello, world"); });

    // Send a GET request to <IP>/get?message=<message>
    server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        String message;
        if (request->hasParam(PARAM_MESSAGE)) {
            message = request->getParam(PARAM_MESSAGE)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, GET: " + message); });

    // Send a POST request to <IP>/post with a form field message set to <message>
    server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        String message;
        if (request->hasParam(PARAM_MESSAGE, true)) {
            message = request->getParam(PARAM_MESSAGE, true)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, POST: " + message); });

    server.onNotFound(notFound);

    server.begin();
}