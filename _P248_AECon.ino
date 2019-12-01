#ifdef USES_P248

//#######################################################################################################
//######################## Plugin 248: Energy - AEConversion Inverter ########################
//#######################################################################################################
/*
  Plugin written by: PeterSpringmann@online.de

  This plugin communicats with AEConversion Grid Inverter (inv350)
  
*/

#define PLUGIN_248
#define PLUGIN_ID_248         248
#define PLUGIN_NAME_248       "Energy - AEConversion"

#define P248_DEV_ID          PCONFIG(0)
#define P248_DEV_ID_LABEL    PCONFIG_LABEL(0)
#define P248_MODEL           PCONFIG(1)
#define P248_MODEL_LABEL     PCONFIG_LABEL(1)
#define P248_BAUDRATE        PCONFIG(2)
#define P248_BAUDRATE_LABEL  PCONFIG_LABEL(2)
#define P248_QUERY1          PCONFIG(3)
#define P248_QUERY2          PCONFIG(4)
#define P248_QUERY3          PCONFIG(5)
#define P248_QUERY4          PCONFIG(6)
#define P248_DEPIN           CONFIG_PIN3

#define P248_DEV_ID_DFLT     1
#define P248_MODEL_DFLT      0  // SDM120C
#define P248_BAUDRATE_DFLT   1  // 9600 baud
#define P248_QUERY1_DFLT     0  // Voltage (V)
#define P248_QUERY2_DFLT     1  // Current (A)
#define P248_QUERY3_DFLT     2  // Power (W)
#define P248_QUERY4_DFLT     5  // Power Factor (cos-phi)

#define P248_NR_OUTPUT_VALUES          4
#define P248_NR_OUTPUT_OPTIONS        10
#define P248_QUERY1_CONFIG_POS  3


#include <SDM.h>    // Requires SDM library from Reaper7 - https://github.com/reaper7/SDM_Energy_Meter/
#include <ESPeasySerial.h>

// These pointers may be used among multiple instances of the same plugin,
// as long as the same serial settings are used.
ESPeasySerial* Plugin_248_SoftSerial = NULL;

SDM* Plugin_248_SDM = NULL;


boolean Plugin_248_init = false;

boolean Plugin_248(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;

  switch (function)
  {

    case PLUGIN_DEVICE_ADD:
      {
        Device[++deviceCount].Number = PLUGIN_ID_248;
        Device[deviceCount].Type = DEVICE_TYPE_TRIPLE;     // connected through 3 datapins
        Device[deviceCount].VType = SENSOR_TYPE_QUAD;
        Device[deviceCount].Ports = 0;
        Device[deviceCount].PullUpOption = false;
        Device[deviceCount].InverseLogicOption = false;
        Device[deviceCount].FormulaOption = true;
        Device[deviceCount].ValueCount = P248_NR_OUTPUT_VALUES;
        Device[deviceCount].SendDataOption = true;
        Device[deviceCount].TimerOption = true;
        Device[deviceCount].GlobalSyncOption = true;
        break;
      }

    case PLUGIN_GET_DEVICENAME:
      {
        string = F(PLUGIN_NAME_248);
        break;
      }

    case PLUGIN_GET_DEVICEVALUENAMES:
      {
        for (byte i = 0; i < VARS_PER_TASK; ++i) {
          if ( i < P248_NR_OUTPUT_VALUES) {
            byte choice = PCONFIG(i + P248_QUERY1_CONFIG_POS);
            safe_strncpy(
              ExtraTaskSettings.TaskDeviceValueNames[i],
              P248_getQueryValueString(choice),
              sizeof(ExtraTaskSettings.TaskDeviceValueNames[i]));
          } else {
            ZERO_FILL(ExtraTaskSettings.TaskDeviceValueNames[i]);
          }
        }
        break;
      }

    case PLUGIN_GET_DEVICEGPIONAMES:
      {
        serialHelper_getGpioNames(event);
        event->String3 = formatGpioName_output_optional("DE");
        break;
      }

    case PLUGIN_WEBFORM_SHOW_CONFIG:
      {
        string += serialHelper_getSerialTypeLabel(event);
        success = true;
        break;
      }

    case PLUGIN_SET_DEFAULTS:
      {
        P248_DEV_ID = P248_DEV_ID_DFLT;
        P248_MODEL = P248_MODEL_DFLT;
        P248_BAUDRATE = P248_BAUDRATE_DFLT;
        P248_QUERY1 = P248_QUERY1_DFLT;
        P248_QUERY2 = P248_QUERY2_DFLT;
        P248_QUERY3 = P248_QUERY3_DFLT;
        P248_QUERY4 = P248_QUERY4_DFLT;

        success = true;
        break;
      }

    case PLUGIN_WEBFORM_LOAD:
      {
        serialHelper_webformLoad(event);

        if (P248_DEV_ID == 0 || P248_DEV_ID > 247 || P248_BAUDRATE >= 6) {
          // Load some defaults
          P248_DEV_ID = P248_DEV_ID_DFLT;
          P248_MODEL = P248_MODEL_DFLT;
          P248_BAUDRATE = P248_BAUDRATE_DFLT;
          P248_QUERY1 = P248_QUERY1_DFLT;
          P248_QUERY2 = P248_QUERY2_DFLT;
          P248_QUERY3 = P248_QUERY3_DFLT;
          P248_QUERY4 = P248_QUERY4_DFLT;
        }
        addFormNumericBox(F("Modbus Address"), P248_DEV_ID_LABEL, P248_DEV_ID, 1, 247);

        {
          String options_model[4] = { F("SDM120C"), F("SDM220T"), F("SDM230"), F("SDM630") };
          addFormSelector(F("Model Type"), P248_MODEL_LABEL, 4, options_model, NULL, P248_MODEL );
        }
        {
          String options_baudrate[6];
          for (int i = 0; i < 6; ++i) {
            options_baudrate[i] = String(P248_storageValueToBaudrate(i));
          }
          addFormSelector(F("Baud Rate"), P248_BAUDRATE_LABEL, 6, options_baudrate, NULL, P248_BAUDRATE );
        }

        if (P248_MODEL == 0 && P248_BAUDRATE > 3)
          addFormNote(F("<span style=\"color:red\"> SDM120 only allows up to 9600 baud with default 2400!</span>"));

        if (P248_MODEL == 3 && P248_BAUDRATE == 0)
          addFormNote(F("<span style=\"color:red\"> SDM630 only allows 2400 to 38400 baud with default 9600!</span>"));

        if (Plugin_248_SDM != nullptr) {
          addRowLabel(F("Checksum (pass/fail)"));
          String chksumStats;
          chksumStats = Plugin_248_SDM->getSuccCount();
          chksumStats += '/';
          chksumStats += Plugin_248_SDM->getErrCount();
          addHtml(chksumStats);
        }

        {
          // In a separate scope to free memory of String array as soon as possible
          sensorTypeHelper_webformLoad_header();
          String options[P248_NR_OUTPUT_OPTIONS];
          for (int i = 0; i < P248_NR_OUTPUT_OPTIONS; ++i) {
            options[i] = P248_getQueryString(i);
          }
          for (byte i = 0; i < P248_NR_OUTPUT_VALUES; ++i) {
            const byte pconfigIndex = i + P248_QUERY1_CONFIG_POS;
            sensorTypeHelper_loadOutputSelector(event, pconfigIndex, i, P248_NR_OUTPUT_OPTIONS, options);
          }
        }


        success = true;
        break;
      }

    case PLUGIN_WEBFORM_SAVE:
      {
          serialHelper_webformSave(event);
          // Save output selector parameters.
          for (byte i = 0; i < P248_NR_OUTPUT_VALUES; ++i) {
            const byte pconfigIndex = i + P248_QUERY1_CONFIG_POS;
            const byte choice = PCONFIG(pconfigIndex);
            sensorTypeHelper_saveOutputSelector(event, pconfigIndex, i, P248_getQueryValueString(choice));
          }

          P248_DEV_ID = getFormItemInt(P248_DEV_ID_LABEL);
          P248_MODEL = getFormItemInt(P248_MODEL_LABEL);
          P248_BAUDRATE = getFormItemInt(P248_BAUDRATE_LABEL);

          Plugin_248_init = false; // Force device setup next time
          success = true;
          break;
      }

    case PLUGIN_INIT:
      {
        Plugin_248_init = true;
        if (Plugin_248_SoftSerial != NULL) {
          delete Plugin_248_SoftSerial;
          Plugin_248_SoftSerial=NULL;
        }
        Plugin_248_SoftSerial = new ESPeasySerial(CONFIG_PIN1, CONFIG_PIN2);
        unsigned int baudrate = P248_storageValueToBaudrate(P248_BAUDRATE);
        Plugin_248_SoftSerial->begin(baudrate);

        if (Plugin_248_SDM != NULL) {
          delete Plugin_248_SDM;
          Plugin_248_SDM=NULL;
        }
        Plugin_248_SDM = new SDM(*Plugin_248_SoftSerial, baudrate, P248_DEPIN);
        Plugin_248_SDM->begin();
        success = true;
        break;
      }

    case PLUGIN_EXIT:
    {
      Plugin_248_init = false;
      if (Plugin_248_SoftSerial != NULL) {
        delete Plugin_248_SoftSerial;
        Plugin_248_SoftSerial=NULL;
      }
      if (Plugin_248_SDM != NULL) {
        delete Plugin_248_SDM;
        Plugin_248_SDM=NULL;
      }
      break;
    }

    case PLUGIN_READ:
      {
        if (Plugin_248_init)
        {
          int model = P248_MODEL;
          byte dev_id = P248_DEV_ID;
          UserVar[event->BaseVarIndex]     = P248_readVal(P248_QUERY1, dev_id, model);
          UserVar[event->BaseVarIndex + 1] = P248_readVal(P248_QUERY2, dev_id, model);
          UserVar[event->BaseVarIndex + 2] = P248_readVal(P248_QUERY3, dev_id, model);
          UserVar[event->BaseVarIndex + 3] = P248_readVal(P248_QUERY4, dev_id, model);
          success = true;
          break;
        }
        break;
      }
  }
  return success;
}

float P248_readVal(byte query, byte node, unsigned int model) {
  if (Plugin_248_SDM == NULL) return 0.0;

  byte retry_count = 3;
  bool success = false;
  float _tempvar = NAN;
  while (retry_count > 0 && !success) {
    Plugin_248_SDM->clearErrCode();
    _tempvar = Plugin_248_SDM->readVal(P248_getRegister(query, model), node);
    --retry_count;
    if (Plugin_248_SDM->getErrCode() == SDM_ERR_NO_ERROR) {
      success = true;
    }
  }
  if (loglevelActiveFor(LOG_LEVEL_INFO)) {
    String log = F("EASTRON: (");
    log += node;
    log += ',';
    log += model;
    log += ") ";
    log += P248_getQueryString(query);
    log += ": ";
    log += _tempvar;
    addLog(LOG_LEVEL_INFO, log);
  }
  return _tempvar;
}

unsigned int P248_getRegister(byte query, byte model) {
  if (model == 0) { // SDM120C
    switch (query) {
      case 0: return SDM120C_VOLTAGE;
      case 1: return SDM120C_CURRENT;
      case 2: return SDM120C_POWER;
      case 3: return SDM120C_ACTIVE_APPARENT_POWER;
      case 4: return SDM120C_REACTIVE_APPARENT_POWER;
      case 5: return SDM120C_POWER_FACTOR;
      case 6: return SDM120C_FREQUENCY;
      case 7: return SDM120C_IMPORT_ACTIVE_ENERGY;
      case 8: return SDM120C_EXPORT_ACTIVE_ENERGY;
      case 9: return SDM120C_TOTAL_ACTIVE_ENERGY;
    }
  } else if (model == 1) { // SDM220T
    switch (query) {
      case 0: return SDM220T_VOLTAGE;
      case 1: return SDM220T_CURRENT;
      case 2: return SDM220T_POWER;
      case 3: return SDM220T_ACTIVE_APPARENT_POWER;
      case 4: return SDM220T_REACTIVE_APPARENT_POWER;
      case 5: return SDM220T_POWER_FACTOR;
      case 6: return SDM220T_FREQUENCY;
      case 7: return SDM220T_IMPORT_ACTIVE_ENERGY;
      case 8: return SDM220T_EXPORT_ACTIVE_ENERGY;
      case 9: return SDM220T_TOTAL_ACTIVE_ENERGY;
    }
  } else if (model == 2) { // SDM230
    switch (query) {
      case 0: return SDM230_VOLTAGE;
      case 1: return SDM230_CURRENT;
      case 2: return SDM230_POWER;
      case 3: return SDM230_ACTIVE_APPARENT_POWER;
      case 4: return SDM230_REACTIVE_APPARENT_POWER;
      case 5: return SDM230_POWER_FACTOR;
      case 6: return SDM230_FREQUENCY;
      case 7: return SDM230_IMPORT_ACTIVE_ENERGY;
      case 8: return SDM230_EXPORT_ACTIVE_ENERGY;
      case 9: return SDM230_CURRENT_RESETTABLE_TOTAL_ACTIVE_ENERGY;
    }
  } else if (model == 3) { // SDM630
    switch (query) {
      case 0: return SDM630_VOLTAGE_AVERAGE;
      case 1: return SDM630_CURRENTSUM;
      case 2: return SDM630_POWERTOTAL;
      case 3: return SDM630_VOLT_AMPS_TOTAL;
      case 4: return SDM630_VOLT_AMPS_REACTIVE_TOTAL;
      case 5: return SDM630_POWER_FACTOR_TOTAL;
      case 6: return SDM630_FREQUENCY;
      case 7: return SDM630_IMPORT_ACTIVE_ENERGY;
      case 8: return SDM630_EXPORT_ACTIVE_ENERGY;
      case 9: return SDM630_IMPORT_ACTIVE_ENERGY;  // No equivalent for TOTAL_ACTIVE_ENERGY present in the SDM630
    }
  }
  return 0;
}

String P248_getQueryString(byte query) {
  switch(query)
  {
    case 0: return F("Voltage (V)");
    case 1: return F("Current (A)");
    case 2: return F("Power (W)");
    case 3: return F("Active Apparent Power (VA)");
    case 4: return F("Reactive Apparent Power (VAr)");
    case 5: return F("Power Factor (cos-phi)");
    case 6: return F("Frequency (Hz)");
    case 7: return F("Import Active Energy (Wh)");
    case 8: return F("Export Active Energy (Wh)");
    case 9: return F("Total Active Energy (Wh)");
  }
  return "";
}

String P248_getQueryValueString(byte query) {
  switch(query)
  {
    case 0: return F("V");
    case 1: return F("A");
    case 2: return F("W");
    case 3: return F("VA");
    case 4: return F("VAr");
    case 5: return F("cos_phi");
    case 6: return F("Hz");
    case 7: return F("Wh_imp");
    case 8: return F("Wh_exp");
    case 9: return F("Wh_tot");
  }
  return "";
}


int P248_storageValueToBaudrate(byte baudrate_setting) {
  unsigned int baudrate = 9600;
  switch (baudrate_setting) {
    case 0:  baudrate = 1200; break;
    case 1:  baudrate = 2400; break;
    case 2:  baudrate = 4800; break;
    case 3:  baudrate = 9600; break;
    case 4:  baudrate = 19200; break;
    case 5:  baudrate = 38400; break;
  }
  return baudrate;
}



#endif // USES_P248
