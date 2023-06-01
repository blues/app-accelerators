
/*
 *                          |   LM75A   |
 *                          |           |
 *                          | V G S S   |
 *                          | C N D C O |
 *                          |_C_D_A_L_S_|
 * __________________,        | | | | |        ,__________________
 *       ESP32       |        | | | | |        |   NOTECARRIER-B
 *                   |        | | | | |        |
 *                   |        | | | | '--------|AUX1
 *                   |        | | | |          |
 *                   |        '-^-^-^----------|<VIO
 *                   |          | | |          |
 *               VBAT|----------^-^-^----------|VBAT
 *                   |          | | |          |
 *                GND|----------'-^-^----------|GND
 *                   |            | |          |
 *                SDA|------------'-^----------|SDA
 *                   |              |          |
 *                SCL|--------------'----------|SCL
 *                   |                         |
 *                 EN|-------------------------|ATTN
 *                   |                         |
 */

#include <M2M_LM75A.h>
#include <Notecard.h>
#include <NotecardEnvVarManager.h>
#ifdef ARDUINO_ARCH_ESP32
#include <NotecardAuxiliaryWiFi.h>
#endif // ARDUINO_ARCH_ESP32

// Uncomment this line and replace com.your-company:your-product-name with your
// ProductUID.
// #define PRODUCT_UID "com.my-company.my-name:my-project"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif
#define usbSerial Serial
#define txRxPinsSerial Serial1

// Configure temperature threshold for Controlled Room Temperature (CRT)
// U.S. Pharmacopeia (USP) USP <659> "Packaging and Storage Requirements"
//
// Controlled room temperature: The temperature maintained thermostatically
// that encompasses at the usual and customary working environment of 20°-25°
// (68°-77° F). Excursions between 15° and 30° (59° and 86° F) that are
// experienced in pharmacies, hospitals, and warehouses, and during shipping
// are allowed. Provided the mean kinetic temperature does not exceed 25°,
// transient spikes up to 40° are permitted as long as they do not exceed 24h.
// Spikes above 40° may be permitted only if the manufacturer so instructs.
// https://www.gmp-compliance.org/gmp-news/what-are-the-regulatory-definitions-for-ambient-room-temperature-and-cold-chain
#define CRT_MAX_TEMP_C 30
#define CRT_SAFE_TEMP_C 25

#define DEFAULT_SAMPLE_INTERVAL_S 1800
#define RETRY_S 5

M2M_LM75A lm75a;
Notecard notecard;
NotecardEnvVarManager *env_var_manager = NULL;
#ifdef ARDUINO_ARCH_ESP32
using namespace blues;
NotecardAuxiliaryWiFi aux_wifi(notecard);
#endif

volatile int alarm_hysteresis = CRT_SAFE_TEMP_C;
volatile int alarm_temperature = CRT_MAX_TEMP_C;
volatile size_t sample_interval_s = DEFAULT_SAMPLE_INTERVAL_S;

// TODO: Remove with `note-arduino` v1.4.2
J *JGetArray(J *rsp, const char *field)
{
    if (rsp == NULL)
    {
        return NULL;
    }
    J *item = JGetObjectItem(rsp, field);
    if (item == NULL)
    {
        return NULL;
    }
    if (!JIsArray(item))
    {
        return NULL;
    }
    return item;
}

void logNoteF(const char *format_, ...)
{
    char log[256];
    va_list args;
    va_start(args, format_);
    vsnprintf(log, sizeof(log), format_, args);

    if (J *cmd = notecard.newCommand("hub.log"))
    {
        JAddStringToObject(cmd, "text", log);
        if (!notecard.sendRequest(cmd))
        {
            notecard.logDebug("Failed to submit log to Notehub.\r\n");
        }
    }

    va_end(args);
}

int acquireGPSLocation(size_t timeout_s_ = 95)
{
    int result;
    char gps_mode[16];
    size_t gps_time_s;

    // Get Current Configuration
    if (J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location.mode")))
    {
        const char * gps_mode_ptr = JGetString(rsp, "mode");
        if (gps_mode_ptr) {
            ::strlcpy(gps_mode, gps_mode_ptr, sizeof(gps_mode));
        }
        notecard.deleteResponse(rsp);
    }
    else
    {
        result = -1; // Unable to communicate with Notecard
        return result;
    }

    // Halt periodic tracking, before active sync is performed
    if (J *cmd = notecard.newCommand("card.location.mode"))
    {
        JAddStringToObject(cmd, "mode", "off");
        notecard.sendRequest(cmd);
    }

    // Gather timestamp of previous location information
    if (J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location")))
    {
        gps_time_s = JGetInt(rsp, "time");
        notecard.logDebugf("INFO: Previous GPS time (seconds): %u", gps_time_s);
        notecard.deleteResponse(rsp);
    }
    else
    {
        result = -1; // Unable to communicate with Notecard
        return result;
    }

    // Activate GPS
    if (J *cmd = notecard.newCommand("card.location.mode"))
    {
        JAddStringToObject(cmd, "mode", "continuous");
        notecard.sendRequest(cmd);
    }

    // Block while resolving GPS location
    for (const size_t start_ms = ::millis();;)
    {
        if (timeout_s_)
        {
            if (::millis() >= (start_ms + (timeout_s_ * 1000)))
            {
                logNoteF("WARNING: GPS user timeout expired!");
                result = -3; // User timeout has expired
                break;
            }
        }

        // Check if GPS has acquired location information
        if (J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location")))
        {
            const size_t current_gps_time_s = JGetInt(rsp, "time");
            if (current_gps_time_s != gps_time_s)
            {
                notecard.logDebugf("INFO: Current GPS time (seconds): %u", current_gps_time_s);
                result = 0; // GPS has fixed on position
                break;
            }
            if (JGetObjectItem(rsp, "stop"))
            {
                logNoteF("WARNING: GPS internal timeout expired!");
                result = -2; // Notecard has signaled polling should stop,
                             // most likely caused by internal GPS timeout
                break;
            }
            notecard.deleteResponse(rsp);
        }
        else
        {
            result = -1; // Unable to communicate with Notecard
            break;
        }
        ::delay(2500);
    }

    // Restore Previous Configuration
    if (J *cmd = notecard.newCommand("card.location.mode"))
    {
        JAddStringToObject(cmd, "mode", gps_mode);
        notecard.sendRequest(cmd);
    }

    // Clean up and exit
    return result;
}

void configureNotecard(void)
{
    // Configure Notecard to synchronize with Notehub periodically, as well as
    // adjust the frequency based on the battery level
    if (J *req = notecard.newCommand("hub.set"))
    {
        JAddStringToObject(req, "sn", "EpiCAT");
        if (PRODUCT_UID[0])
        {
            JAddStringToObject(req, "product", PRODUCT_UID);
        }
        JAddStringToObject(req, "mode", "periodic");
        JAddStringToObject(req, "vinbound", "usb:5;high:120;normal:240;low:480;dead:0");
        JAddStringToObject(req, "voutbound", "usb:5;high:60;normal:120;low:240;dead:0");
        notecard.sendRequestWithRetry(req, RETRY_S);
    }

    // Optimize voltage variable behaviors for LiPo battery
    if (J *cmd = notecard.newCommand("card.voltage"))
    {
        JAddStringToObject(cmd, "mode", "lipo");
        notecard.sendRequest(cmd);
    }

    // Ensure the accelerometer has been activated
    if (J *cmd = notecard.newCommand("card.motion.mode"))
    {
        JAddBoolToObject(cmd, "start", true);
        JAddNumberToObject(cmd, "sensitivity", 1);
        notecard.sendRequest(cmd);
    }

    // Configure GPS heartbeat to verify last location reported
    if (J *cmd = notecard.newCommand("card.location.track"))
    {
        JAddBoolToObject(cmd, "start", true);
        JAddBoolToObject(cmd, "sync", true);
        JAddBoolToObject(cmd, "heartbeat", true);
        JAddNumberToObject(cmd, "hours", 12);
        notecard.sendRequest(cmd);
    }

    // Establish a template to optimize queue size and data usage
    if (J *cmd = notecard.newCommand("note.template"))
    {
        JAddStringToObject(cmd, "file", "status.qo");
        if (J *body = JAddObjectToObject(cmd, "body"))
        {
            JAddBoolToObject(body, "alert", TBOOL);
            JAddBoolToObject(body, "low_batt", TBOOL);
            JAddNumberToObject(body, "temp", TFLOAT16);
            JAddNumberToObject(body, "voltage", TFLOAT16);
            notecard.sendRequest(cmd);
        }
    }
}

bool currentlyMoving(void)
{
    bool moving = false; // prioritize battery life on error
    uint32_t movementTimeSecs = 0;

    // Increase motion sensitivity to help detect movement
    if (J *cmd = notecard.newCommand("card.motion.mode"))
    {
        JAddBoolToObject(cmd, "start", true);
        JAddNumberToObject(cmd, "sensitivity", 5);
        notecard.sendRequest(cmd);
    }

    // Determine whether or not the module is moving.
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.triangulate"));
    if (rsp)
    {
        movementTimeSecs = JGetInt(rsp, "motion");
        notecard.deleteResponse(rsp);

        // Wait long enough for the accelerometer to record a new timestamp.
        ::delay(1500);

        rsp = notecard.requestAndResponse(notecard.newRequest("card.triangulate"));
        if (rsp)
        {
            if (movementTimeSecs != JGetInt(rsp, "motion"))
            {
                moving = true;
            }
            notecard.deleteResponse(rsp);
        }
    }

    // Restore motion sensitivity to preserve battery life
    if (J *cmd = notecard.newCommand("card.motion.mode"))
    {
        JAddBoolToObject(cmd, "start", true);
        JAddNumberToObject(cmd, "sensitivity", 1);
        notecard.sendRequest(cmd);
    }

    return moving;
}

void envVarManagerCb(const char *var, const char *val, void *user_ctx)
{
    (void)user_ctx;

    // Cache the values for each variable.
    if (strcmp(var, "alarm_temperature") == 0 && atoi(val))
    {
        alarm_temperature = atoi(val);
    }
    else if (strcmp(var, "alarm_hysteresis") == 0 && atoi(val))
    {
        alarm_hysteresis = atoi(val);
    }
    else if (strcmp(var, "sample_interval_s") == 0 && atoi(val))
    {
        sample_interval_s = atoi(val);
    }
}

void fetchEnvironmentVariables(void)
{
    static const char *env_vars[] = {
        "alarm_temperature",
        "alarm_hysteresis",
        "sample_interval_s"};
    static const size_t num_env_vars = sizeof(env_vars) / sizeof(env_vars[0]);

    // Allocate the environment variable manager.
    if (!(env_var_manager = NotecardEnvVarManager_alloc()))
    {
        notecard.logDebug("Failed to allocate env var manager.");
    }

    // Set the callback for the manager, and give it a pointer to our cache
    // so that we can store the values of the environment variables we're
    // fetching.
    if (NEVM_SUCCESS != NotecardEnvVarManager_setEnvVarCb(env_var_manager, envVarManagerCb, nullptr))
    {
        notecard.logDebug("Failed to set env var manager callback.");
    }

    if (NEVM_SUCCESS != NotecardEnvVarManager_fetch(env_var_manager, env_vars, num_env_vars))
    {
        notecard.logDebug("Failed to fetch environment variables.");
    }

    NotecardEnvVarManager_free(env_var_manager);
}

bool inHysteresis(void)
{
    bool result = true; // To mitigate I2C communication failure, we must assume
                        // we are in hysteresis to sample Notecard again soon.

    // Check status of OS pin
    J *rsp = nullptr;
    if (J *req = notecard.newRequest("card.aux"))
    {
        JAddStringToObject(req, "mode", "gpio");
        if (J *usage = JAddArrayToObject(req, "usage"))
        {
            JAddItemToArray(usage, JCreateString("input-pullup")); // Aux 1
            JAddItemToArray(usage, JCreateString(""));             // Aux 2
            JAddItemToArray(usage, JCreateString(""));             // Aux 3
            JAddItemToArray(usage, JCreateString(""));             // Aux 4
            rsp = notecard.requestAndResponseWithRetry(req, RETRY_S);
        }
        else
        {
            notecard.deleteResponse(req);
            notecard.logDebug("Failed to allocate `card.aux` request.");
        }
    }

    // Parse response
    if (rsp)
    {
        // See if there's an error
        char *err = JGetString(rsp, "err");
        if (err[0] != '\0')
        {
            notecard.logDebug(err);
        }
        else
        {
            J *state = JGetArray(rsp, "state");
            if (state)
            {
                if (J *aux1 = JGetArrayItem(state, 0))
                {
                    result = JGetBool(aux1, "low");
                }
            }
            else
            {
                notecard.logDebug("`card.aux` returned an unexpected response.\r\n");
            }
        }
        notecard.deleteResponse(rsp);
    }
    else
    {
        notecard.logDebug("`card.aux` failed to return a response.\r\n");
    }

    return result;
}

void setup()
{
    // Provide visual signal when the Host MCU is powered
    ::pinMode(LED_BUILTIN, OUTPUT);
    ::digitalWrite(LED_BUILTIN, HIGH);

#ifndef NDEBUG
#warning "Debug mode is enabled. Define NDEBUG to disable debug."
    // Initialize Debug Output
    usbSerial.begin(115200);
    static const size_t MAX_SERIAL_WAIT_MS = 5000;
    size_t begin_serial_wait_ms = ::millis();
    while (!usbSerial && (MAX_SERIAL_WAIT_MS > (::millis() - begin_serial_wait_ms)))
    {
        ; // wait for debug serial port to connect. Needed for native USB
    }
    notecard.setDebugOutputStream(usbSerial);
#endif

    // Initialize LM75A
    lm75a.begin();

    // Initialize Notecard
    notecard.begin();
#ifdef ARDUINO_ARCH_ESP32
    // Initialize Auxiliary Wi-Fi
    aux_wifi.begin();
#endif

    // Configure Notecard
    configureNotecard();

    // Fetch Environment Variables
    fetchEnvironmentVariables();

    // Configure alarm and hystersis thresholds
    lm75a.setOsTripTemperature(alarm_temperature);    // Max safe temperature for travel
    lm75a.setHysterisisTemperature(alarm_hysteresis); // Max safe temperature for storage

    // Sample temperature sensor
    const float temperature = lm75a.getTemperature();
    static const bool ACTIVE_ALARM = (temperature > alarm_temperature);
    static const bool TEMP_WARNING = inHysteresis(); // Read AUX1 pin to determine
                                                     // if we are in hysteresis

    // Sample battery voltage
    float voltage = 0.0f;
    bool low_battery = false;
    if (J *rsp = notecard.requestAndResponse(notecard.newRequest("card.voltage")))
    {
        voltage = JGetNumber(rsp, "value");
        const char *power_mode = JGetString(rsp, "mode");
        low_battery = (power_mode && !strcmp(power_mode, "low"));
        notecard.deleteResponse(rsp);
    }

    // Update polling interval based upon temperature
    if (TEMP_WARNING)
    {
        sample_interval_s = 180; // Sample temperature every three (3) minutes
                                 // when storage threshold has been exceeded
    }

    // Select location service (Wi-Fi or GPS) based upon alarm state
    if (ACTIVE_ALARM)
    {
        // Halt periodic tracking when alarm is active
        if (J *cmd = notecard.newCommand("card.location.mode"))
        {
            JAddStringToObject(cmd, "mode", "off");
            notecard.sendRequest(cmd);
        }

        // Update Current Location (via GPS)
        if (int gps_error = acquireGPSLocation())
        {
            logNoteF("ERROR: Failed to acquire GPS signal! <%d>", gps_error);
            // Failed to acquire GPS try Wi-Fi instead
#ifdef ARDUINO_ARCH_ESP32
            aux_wifi.updateTriangulationData(true, false);
            aux_wifi.logCachedSsids(); // Log SSIDs used in calculation
#endif
        }
    }
    else
    {
#ifdef ARDUINO_ARCH_ESP32
        // Check if actively moving before updating Wi-Fi location
        if (!currentlyMoving())
        {
            aux_wifi.updateTriangulationData(true); // Update Current Location (via Wi-Fi)
            aux_wifi.logCachedSsids();              // Log SSIDs used in calculation
        }
#endif

        // Configure GPS to update only when device is in motion
        if (J *cmd = notecard.newCommand("card.location.mode"))
        {
            JAddStringToObject(cmd, "mode", "periodic");
            JAddNumberToObject(cmd, "threshold", 5);
            JAddNumberToObject(cmd, "seconds", 900); // Fifteen (15) minutes
            notecard.sendRequest(cmd);
        }
    }

    // Send results to Notehub
    if (J *cmd = notecard.newCommand("note.add"))
    {
        JAddStringToObject(cmd, "file", "status.qo");
        if (J *body = JAddObjectToObject(cmd, "body"))
        {
            if (ACTIVE_ALARM)
            {
                JAddBoolToObject(cmd, "sync", true);
                JAddBoolToObject(body, "alert", true);
            }
            if (low_battery)
            {
                JAddBoolToObject(body, "low_batt", true);
            }
            JAddNumberToObject(body, "temp", temperature);
            JAddNumberToObject(body, "voltage", voltage);
            notecard.sendRequest(cmd);
        }
    }
}

void loop()
{
    // Request sleep from loop to safeguard against transmission failure,
    // and ensure sleep request is honored so power usage is minimized.

    // Configure auxiliary GPIO for input (interrupt trigger). The interrupt
    // is critical to ensure the host MCU is awakened in the event of an alarm.
    if (J *req = notecard.newRequest("card.aux"))
    {
        JAddStringToObject(req, "mode", "gpio");
        J *usage = JAddArrayToObject(req, "usage");
        JAddItemToArray(usage, JCreateString("count-pullup")); // Aux 1
        JAddItemToArray(usage, JCreateString(""));             // Aux 2
        JAddItemToArray(usage, JCreateString(""));             // Aux 3
        JAddItemToArray(usage, JCreateString(""));             // Aux 4
        if (notecard.sendRequest(req))
        {
            // Create a "command" instead of a "request", because the host
            // MCU is going to power down and cannot receive a response.
            if (J *cmd = notecard.newCommand("card.attn"))
            {
                JAddStringToObject(cmd, "mode", "rearm,auxgpio,sleep");
                JAddNumberToObject(cmd, "seconds", sample_interval_s);
                notecard.sendRequest(cmd);
            }
        }
    }

    // Wait 3s before retrying
    ::delay(3000);
}
