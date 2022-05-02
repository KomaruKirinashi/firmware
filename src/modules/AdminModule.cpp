#include "AdminModule.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

#ifdef PORTDUINO
#include "unistd.h"
#endif

AdminModule *adminModule;

/// A special reserved string to indicate strings we can not share with external nodes.  We will use this 'reserved' word instead.
/// Also, to make setting work correctly, if someone tries to set a string to this reserved value we assume they don't really want
/// a change.
static const char *secretReserved = "sekrit";

/// If buf is !empty, change it to secret
static void hideSecret(char *buf)
{
    if (*buf) {
        strcpy(buf, secretReserved);
    }
}

/// If buf is the reserved secret word, replace the buffer with currentVal
static void writeSecret(char *buf, const char *currentVal)
{
    if (strcmp(buf, secretReserved) == 0) {
        strcpy(buf, currentVal);
    }
}

/**
 * @brief Handle recieved protobuf message
 *
 * @param mp Recieved MeshPacket
 * @param r Decoded AdminMessage
 * @return bool
 */
bool AdminModule::handleReceivedProtobuf(const MeshPacket &mp, AdminMessage *r)
{
    // if handled == false, then let others look at this message also if they want
    bool handled = false;
    assert(r);

    switch (r->which_variant) {

    /**
     * Getters
     */
    case AdminMessage_get_owner_request_tag:
        DEBUG_MSG("Client is getting owner\n");
        handleGetOwner(mp);
        break;

    case AdminMessage_get_radio_request_tag:
        DEBUG_MSG("Client is getting radio\n");
        handleGetRadio(mp);
        break;

    case AdminMessage_get_config_request_tag:
        DEBUG_MSG("Client is getting config\n");
        handleGetConfig(mp);
        break;

    case AdminMessage_get_module_config_request_tag:
        DEBUG_MSG("Client is getting module config\n");
        handleGetModuleConfig(mp);
        break;

    case AdminMessage_get_channel_request_tag: {
        uint32_t i = r->get_channel_request - 1;
        DEBUG_MSG("Client is getting channel %u\n", i);
        if (i >= MAX_NUM_CHANNELS)
            myReply = allocErrorResponse(Routing_Error_BAD_REQUEST, &mp);
        else
            handleGetChannel(mp, i);
        break;
    }

    /**
     * Setters
     */
    case AdminMessage_set_owner_tag:
        DEBUG_MSG("Client is setting owner\n");
        handleSetOwner(r->set_owner);
        break;

    case AdminMessage_set_radio_tag:
        DEBUG_MSG("Client is setting radio\n");
        handleSetRadio(r->set_radio);
        break;

    case AdminMessage_set_config_tag:
        DEBUG_MSG("Client is setting the config\n");
        handleSetConfig(r->set_config);
        break;

    case AdminMessage_set_module_config_tag:
        DEBUG_MSG("Client is setting the module config\n");
        handleSetModuleConfig(r->set_module_config);
        break;

    case AdminMessage_set_channel_tag:
        DEBUG_MSG("Client is setting channel %d\n", r->set_channel.index);
        if (r->set_channel.index < 0 || r->set_channel.index >= (int)MAX_NUM_CHANNELS)
            myReply = allocErrorResponse(Routing_Error_BAD_REQUEST, &mp);
        else
            handleSetChannel(r->set_channel);
        break;

    /**
     * Other
     */
    case AdminMessage_reboot_seconds_tag: {
        int32_t s = r->reboot_seconds;
        DEBUG_MSG("Rebooting in %d seconds\n", s);
        rebootAtMsec = (s < 0) ? 0 : (millis() + s * 1000);
        break;
    }
    case AdminMessage_shutdown_seconds_tag: {
        int32_t s = r->shutdown_seconds;
        DEBUG_MSG("Shutdown in %d seconds\n", s);
        shutdownAtMsec = (s < 0) ? 0 : (millis() + s * 1000);
        break;
    }

#ifdef PORTDUINO
    case AdminMessage_exit_simulator_tag:
        DEBUG_MSG("Exiting simulator\n");
        _exit(0);
        break;
#endif

    default:
        AdminMessage response = AdminMessage_init_default;
        AdminMessageHandleResult handleResult = MeshModule::handleAdminMessageForAllPlugins(mp, r, &response);

        if (handleResult == AdminMessageHandleResult::HANDLED_WITH_RESPONSE) {
            myReply = allocDataProtobuf(response);
        } else if (mp.decoded.want_response) {
            DEBUG_MSG("We did not responded to a request that wanted a respond. req.variant=%d\n", r->which_variant);
        } else if (handleResult != AdminMessageHandleResult::HANDLED) {
            // Probably a message sent by us or sent to our local node.  FIXME, we should avoid scanning these messages
            DEBUG_MSG("Ignoring nonrelevant admin %d\n", r->which_variant);
        }
        break;
    }
    return handled;
}

/**
 * Setter methods
 */

void AdminModule::handleSetOwner(const User &o)
{
    int changed = 0;

    if (*o.long_name) {
        changed |= strcmp(owner.long_name, o.long_name);
        strcpy(owner.long_name, o.long_name);
    }
    if (*o.short_name) {
        changed |= strcmp(owner.short_name, o.short_name);
        strcpy(owner.short_name, o.short_name);
    }
    if (*o.id) {
        changed |= strcmp(owner.id, o.id);
        strcpy(owner.id, o.id);
    }
    if (owner.is_licensed != o.is_licensed) {
        changed = 1;
        owner.is_licensed = o.is_licensed;
    }

    if (changed) // If nothing really changed, don't broadcast on the network or write to flash
        service.reloadOwner();
}

void AdminModule::handleSetRadio(RadioConfig &r)
{
    writeSecret(r.preferences.wifi_password, radioConfig.preferences.wifi_password);
    radioConfig = r;

    service.reloadConfig();
}

void AdminModule::handleSetConfig(const Config &c)
{
    switch (c.which_payloadVariant) {
    case AdminMessage_ConfigType_DEVICE_CONFIG:
        DEBUG_MSG("Setting config: Device\n");
        break;
    case AdminMessage_ConfigType_GPS_CONFIG:
        DEBUG_MSG("Setting config: GPS\n");
        break;
    case AdminMessage_ConfigType_POWER_CONFIG:
        DEBUG_MSG("Setting config: Power\n");
        break;
    case AdminMessage_ConfigType_WIFI_CONFIG:
        DEBUG_MSG("Setting config: WiFi\n");
        break;
    case AdminMessage_ConfigType_DISPLAY_CONFIG:
        DEBUG_MSG("Setting config: Display\n");
        break;
    case AdminMessage_ConfigType_LORA_CONFIG:
        DEBUG_MSG("Setting config: LoRa\n");
        break;
    }

    service.reloadConfig();
}

void AdminModule::handleSetModuleConfig(const ModuleConfig &c)
{
    switch (c.which_payloadVariant) {
    case AdminMessage_ModuleConfigType_MQTT_CONFIG:
        DEBUG_MSG("Setting module config: MQTT\n");
        break;
    case AdminMessage_ModuleConfigType_SERIAL_CONFIG:
        DEBUG_MSG("Setting module config: Serial\n");
        break;
    case AdminMessage_ModuleConfigType_EXTNOTIF_CONFIG:
        DEBUG_MSG("Setting module config: External Notification\n");
        break;
    case AdminMessage_ModuleConfigType_STOREFORWARD_CONFIG:
        DEBUG_MSG("Setting module config: Store & Forward\n");
        break;
    case AdminMessage_ModuleConfigType_RANGETEST_CONFIG:
        DEBUG_MSG("Setting module config: Range Test\n");
        break;
    case AdminMessage_ModuleConfigType_TELEMETRY_CONFIG:
        DEBUG_MSG("Setting module config: Telemetry\n");
        break;
    case AdminMessage_ModuleConfigType_CANNEDMSG_CONFIG:
        DEBUG_MSG("Setting module config: Canned Message\n");
        break;
    }

    service.reloadConfig();
}

void AdminModule::handleSetChannel(const Channel &cc)
{
    channels.setChannel(cc);

    // Just update and save the channels - no need to update the radio for ! primary channel changes
    if (cc.index == 0) {
        // FIXME, this updates the user preferences also, which isn't needed - we really just want to notify on configChanged
        service.reloadConfig();
    } else {
        channels.onConfigChanged(); // tell the radios about this change
        nodeDB.saveChannelsToDisk();
    }
}

/**
 * Getter methods
 */

void AdminModule::handleGetOwner(const MeshPacket &req)
{
    if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_owner_response = owner;

        r.which_variant = AdminMessage_get_owner_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

void AdminModule::handleGetRadio(const MeshPacket &req)
{
    if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_radio_response = radioConfig;

        // NOTE: The phone app needs to know the ls_secs & phone_timeout value so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).
        r.get_radio_response.preferences.ls_secs = getPref_ls_secs();
        r.get_radio_response.preferences.phone_timeout_secs = getPref_phone_timeout_secs();
        // hideSecret(r.get_radio_response.preferences.wifi_ssid); // hmm - leave public for now, because only minimally private
        // and useful for users to know current provisioning)
        hideSecret(r.get_radio_response.preferences.wifi_password);

        r.which_variant = AdminMessage_get_radio_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

void AdminModule::handleGetConfig(const MeshPacket &req)
{
    // We create the reply here
    AdminMessage r = AdminMessage_init_default;

    if (req.decoded.want_response) {
        switch (r.get_config_request) {
        case AdminMessage_ConfigType_DEVICE_CONFIG:
            DEBUG_MSG("Getting config: Device\n");
            r.get_config_response.which_payloadVariant = Config_device_config_tag;
            break;
        case AdminMessage_ConfigType_GPS_CONFIG:
            DEBUG_MSG("Getting config: GPS\n");
            r.get_config_response.which_payloadVariant = Config_gps_config_tag;
            break;
        case AdminMessage_ConfigType_POWER_CONFIG:
            DEBUG_MSG("Getting config: Power\n");
            r.get_config_response.which_payloadVariant = Config_power_config_tag;
            break;
        case AdminMessage_ConfigType_WIFI_CONFIG:
            DEBUG_MSG("Getting config: WiFi\n");
            r.get_config_response.which_payloadVariant = Config_wifi_config_tag;
            break;
        case AdminMessage_ConfigType_DISPLAY_CONFIG:
            DEBUG_MSG("Getting config: Display\n");
            r.get_config_response.which_payloadVariant = Config_display_config_tag;
            break;
        case AdminMessage_ConfigType_LORA_CONFIG:
            DEBUG_MSG("Getting config: LoRa\n");
            r.get_config_response.which_payloadVariant = Config_lora_config_tag;
            break;
        }

        // NOTE: The phone app needs to know the ls_secs & phone_timeout value so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).
        // r.get_radio_response.preferences.ls_secs = getPref_ls_secs();
        // r.get_radio_response.preferences.phone_timeout_secs = getPref_phone_timeout_secs();
        // hideSecret(r.get_radio_response.preferences.wifi_ssid); // hmm - leave public for now, because only minimally private
        // and useful for users to know current provisioning) hideSecret(r.get_radio_response.preferences.wifi_password);
        // r.get_config_response.which_payloadVariant = Config_ModuleConfig_telemetry_config_tag;
        r.which_variant = AdminMessage_get_config_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

void AdminModule::handleGetModuleConfig(const MeshPacket &req)
{
    // We create the reply here
    AdminMessage r = AdminMessage_init_default;

    Serial.println(r.get_module_config_request);

    if (req.decoded.want_response) {
        switch (r.get_module_config_request) {
        case AdminMessage_ModuleConfigType_MQTT_CONFIG:
            DEBUG_MSG("Getting module config: MQTT\n");
            r.get_module_config_response.which_payloadVariant = ModuleConfig_mqtt_config_tag;
            break;
        case AdminMessage_ModuleConfigType_SERIAL_CONFIG:
            DEBUG_MSG("Getting module config: Serial\n");
            r.get_module_config_response.which_payloadVariant = ModuleConfig_serial_config_tag;
            break;
        case AdminMessage_ModuleConfigType_EXTNOTIF_CONFIG:
            DEBUG_MSG("Getting module config: External Notification\n");
            r.get_module_config_response.which_payloadVariant = ModuleConfig_external_notification_config_tag;
            break;
        case AdminMessage_ModuleConfigType_STOREFORWARD_CONFIG:
            DEBUG_MSG("Getting module config: Store & Forward\n");
            r.get_module_config_response.which_payloadVariant = ModuleConfig_store_forward_config_tag;
            break;
        case AdminMessage_ModuleConfigType_RANGETEST_CONFIG:
            DEBUG_MSG("Getting module config: Range Test\n");
            r.get_module_config_response.which_payloadVariant = ModuleConfig_range_test_config_tag;
            break;
        case AdminMessage_ModuleConfigType_TELEMETRY_CONFIG:
            DEBUG_MSG("Getting module config: Telemetry\n");
            r.get_module_config_response.which_payloadVariant = ModuleConfig_telemetry_config_tag;
            r.get_module_config_response.payloadVariant.telemetry_config = moduleConfig.payloadVariant.telemetry_config;
            break;
        case AdminMessage_ModuleConfigType_CANNEDMSG_CONFIG:
            DEBUG_MSG("Getting module config: Canned Message\n");
            r.get_module_config_response.which_payloadVariant = ModuleConfig_canned_message_config_tag;
            break;
        }

        // NOTE: The phone app needs to know the ls_secs & phone_timeout value so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).
        // r.get_radio_response.preferences.ls_secs = getPref_ls_secs();
        // r.get_radio_response.preferences.phone_timeout_secs = getPref_phone_timeout_secs();
        // hideSecret(r.get_radio_response.preferences.wifi_ssid); // hmm - leave public for now, because only minimally private
        // and useful for users to know current provisioning) hideSecret(r.get_radio_response.preferences.wifi_password);
        // r.get_config_response.which_payloadVariant = Config_ModuleConfig_telemetry_config_tag;
        r.which_variant = AdminMessage_get_module_config_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

void AdminModule::handleGetChannel(const MeshPacket &req, uint32_t channelIndex)
{
    if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_channel_response = channels.getByIndex(channelIndex);
        r.which_variant = AdminMessage_get_channel_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

AdminModule::AdminModule() : ProtobufModule("Admin", PortNum_ADMIN_APP, AdminMessage_fields)
{
    // restrict to the admin channel for rx
    boundChannel = Channels::adminChannel;
}