#include "fnConfig.h"
#include "fnSystem.h"

#include "fsFlash.h"

#include <cstring>
#include <sstream>

#include "debug.h"

/* Save configuration data to FLASH. If SD is mounted, save a backup copy there.
*/
void fnConfig::save()
{
    int i;

    Debug_printf("fnConfig::save \"%s\"\r\n", _general.config_file_path.c_str());

    if (!_dirty)
    {
        Debug_println("fnConfig::save not dirty, not saving");
        return;
    }

    // We're going to write a stringstream so that we have only one write to file at the end
    std::stringstream ss;

#define LINETERM "\r\n"

    // GENERAL
    ss << "[General]" LINETERM;
    ss << "devicename=" << _general.devicename << LINETERM;
    ss << "hsioindex=" << _general.hsio_index << LINETERM;
    ss << "rotationsounds=" << _general.rotation_sounds << LINETERM;
    ss << "configenabled=" << _general.config_enabled << LINETERM;
    ss << "boot_mode=" << _general.boot_mode << LINETERM;
    if (_general.timezone.empty() == false)
        ss << "timezone=" << _general.timezone << LINETERM;
    ss << "fnconfig_on_spifs=" << _general.fnconfig_spifs << LINETERM;
    ss << "status_wait_enabled=" << _general.status_wait_enabled << LINETERM;
    ss << "printer_enabled=" << _general.printer_enabled << LINETERM;
    ss << "encrypt_passphrase=" << _general.encrypt_passphrase << LINETERM;

    // ss << LINETERM;

    // SERIAL
    ss << LINETERM << "[Serial]" << LINETERM;
    ss << "port=" << _serial.port << LINETERM;
    ss << "command=" << std::string(_serial_command_pin_names[_serial.command]) << LINETERM;
    ss << "proceed=" << std::string(_serial_proceed_pin_names[_serial.proceed]) << LINETERM;

    // WIFI
    ss << LINETERM << "[WiFi]" LINETERM;
    ss << "enabled=" << _wifi.enabled << LINETERM;
    ss << "SSID=" << _wifi.ssid << LINETERM;
    ss << "passphrase=" << _wifi.passphrase << LINETERM;

    // WIFI STORED
    for (i = 0; i < MAX_WIFI_STORED; i++)
    {
        if (_wifi_stored[i].enabled)
        {
            ss << LINETERM << "[WiFiStored" << (i + 1) << "]" LINETERM;
            ss << "SSID=" << _wifi_stored[i].ssid << LINETERM;
            ss << "passphrase=" << _wifi_stored[i].passphrase << LINETERM;
        }
        else
            break;
    }

    // BLUETOOTH
    ss << LINETERM << "[Bluetooth]" LINETERM;
    ss << "devicename=" << _bt.bt_devname << LINETERM;
    ss << "enabled=" << _bt.bt_status << LINETERM;
    ss << "baud=" << _bt.bt_baud << LINETERM;

    // NETWORK
    ss << LINETERM << "[Network]" LINETERM;
    ss << "sntpserver=" << _network.sntpserver << LINETERM;

    // HOSTS
    for (i = 0; i < MAX_HOST_SLOTS; i++)
    {
        if (_host_slots[i].type != HOSTTYPE_INVALID)
        {
            ss << LINETERM << "[Host" << (i + 1) << "]" LINETERM;
            ss << "type=" << _host_type_names[_host_slots[i].type] << LINETERM;
            ss << "name=" << _host_slots[i].name << LINETERM;
        }
    }

    // MOUNTS
    for (i = 0; i < MAX_MOUNT_SLOTS; i++)
    {
        if (_mount_slots[i].host_slot >= 0)
        {
            ss << LINETERM << "[Mount" << (i + 1) << "]" LINETERM;
            ss << "hostslot=" << (_mount_slots[i].host_slot + 1) << LINETERM; // Write host slot as 1-based
            ss << "path=" << _mount_slots[i].path << LINETERM;
            ss << "mode=" << _mount_mode_names[_mount_slots[i].mode] << LINETERM;
        }
    }

    // PRINTERS
    for (i = 0; i < MAX_PRINTER_SLOTS; i++)
    {
        if (_printer_slots[i].type != PRINTER_CLASS::printer_type::PRINTER_INVALID)
        {
            ss << LINETERM << "[Printer" << (i + 1) << "]" LINETERM;
            ss << "type=" << _printer_slots[i].type << LINETERM;
            ss << "port=" << (_printer_slots[i].port + 1) << LINETERM; // Write port # as 1-based
        }
    }

    // TAPES
    for (i = 0; i < MAX_TAPE_SLOTS; i++)
    {
        if (_tape_slots[i].host_slot >= 0)
        {
            ss << LINETERM << "[Tape" << (i + 1) << "]" LINETERM;
            ss << "hostslot=" << (_tape_slots[i].host_slot + 1) << LINETERM; // Write host slot as 1-based
            ss << "path=" << _tape_slots[i].path << LINETERM;
            ss << "mode=" << _mount_mode_names[_tape_slots[i].mode] << LINETERM;
        }
    }

    // MODEM
    ss << LINETERM << "[Modem]" << LINETERM;
    ss << "modem_enabled=" << _modem.modem_enabled << LINETERM;
    ss << "sniffer_enabled=" << _modem.sniffer_enabled << LINETERM;

    //PHONEBOOK
    for (i = 0; i < MAX_PB_SLOTS; i++)
    {
        if (!_phonebook_slots[i].phnumber.empty())
        {
            ss << LINETERM << "[Phonebook" << (i+1) << "]" LINETERM;
            ss << "number=" << _phonebook_slots[i].phnumber << LINETERM;
            ss << "host=" << _phonebook_slots[i].hostname << LINETERM;
            ss << "port=" << _phonebook_slots[i].port << LINETERM;
        }
    }

    // CASSETTE
    ss << LINETERM << "[Cassette]" << LINETERM;
    ss << "play_record=" << ((_cassette.button) ? "1 Record" : "0 Play") << LINETERM;
    ss << "pulldown=" << ((_cassette.pulldown) ? "1 Pulldown Resistor" : "0 B Button Press") << LINETERM;
    ss << "cassette_enabled=" << _cassette.cassette_enabled << LINETERM;

    // CPM
    ss << LINETERM << "[CPM]" << LINETERM;
    ss << "cpm_enabled=" << _cpm.cpm_enabled << LINETERM;
    ss << "ccp=" << _cpm.ccp << LINETERM;

    // ENABLE DEVICE SLOTS
    ss << LINETERM << "[ENABLE]" << LINETERM;
    ss << "enable_device_slot_1=" << _denable.device_1_enabled << LINETERM;
    ss << "enable_device_slot_2=" << _denable.device_2_enabled << LINETERM;
    ss << "enable_device_slot_3=" << _denable.device_3_enabled << LINETERM;
    ss << "enable_device_slot_4=" << _denable.device_4_enabled << LINETERM;
    ss << "enable_device_slot_5=" << _denable.device_5_enabled << LINETERM;
    ss << "enable_device_slot_6=" << _denable.device_6_enabled << LINETERM;
    ss << "enable_device_slot_7=" << _denable.device_7_enabled << LINETERM;
    ss << "enable_device_slot_8=" << _denable.device_8_enabled << LINETERM;
    ss << "enable_apetime=" << _denable.apetime << LINETERM;

    // NETSIO
    ss << LINETERM << "[NetSIO]" << LINETERM;
    ss << "enabled=" << _netsio.netsio_enabled << LINETERM;
    ss << "host=" << _netsio.host << LINETERM;
    ss << "port=" << _netsio.port << LINETERM;

    // Write the results out
    FILE *fout = fopen(_general.config_file_path.c_str(), FILE_WRITE);
    if (fout == nullptr)
    {
        Debug_printf("Failed to open config file\r\n");
        return;
    }
    std::string result = ss.str();
    size_t z = fwrite(result.c_str(), 1, result.length(), fout);
    (void)z; // Get around unused var
    Debug_printf("fnConfig::save wrote %u bytes\r\n", (unsigned)z);
    fclose(fout);
    
    _dirty = false;

}
