#include "ImprovWiFiLibrary.h"


void ImprovWiFi::handleSerial() {
  
  if (serial->available() > 0) {
    uint8_t b = serial->read();

    if (parseImprovSerial(_position, b, _buffer)) {
      _buffer[_position++] = b;
    } else {
      _position = 0;
    }
  }
}

void ImprovWiFi::onErrorCallback(improv::Error err) {
  if (onImprovWiFiErrorCallback) {
    onImprovWiFiErrorCallback(err);
  }
}

bool ImprovWiFi::onCommandCallback(improv::ImprovCommand cmd) {

  switch (cmd.command) {
    case improv::Command::GET_CURRENT_STATE:
    {
      if (isConnected()) {
        setState(improv::State::STATE_PROVISIONED);
        sendDeviceUrl(cmd.command);
      } else {
        setState(improv::State::STATE_AUTHORIZED);
      }
      
      break;
    }

    case improv::Command::WIFI_SETTINGS:
    {
    
      if (cmd.ssid.empty()) {
        setError(improv::Error::ERROR_INVALID_RPC);
        break;
      }

      setState(improv::STATE_PROVISIONING);

      bool success = false;

      if (customConnectWiFiCallback) {
        success = customConnectWiFiCallback(cmd.ssid, cmd.password);
      } else {
        success = tryConnectToWifi(cmd.ssid, cmd.password);
      }

      
      if (success) {
        setError(improv::Error::ERROR_NONE);
        setState(improv::STATE_PROVISIONED);
        sendDeviceUrl(cmd.command);
        if (onImprovWiFiConnectedCallback) {
          onImprovWiFiConnectedCallback(cmd.ssid, cmd.password);
        }
        
      } else {
        setState(improv::STATE_STOPPED);
        setError(improv::ERROR_UNABLE_TO_CONNECT);
        onErrorCallback(improv::ERROR_UNABLE_TO_CONNECT);
      }
      
      break;
    }

    case improv::Command::GET_DEVICE_INFO:
    {
      std::vector<std::string> infos = {
        // Firmware name
        improvWiFiParams.firmwareName,
        // Firmware version
        improvWiFiParams.firmwareVersion,
        // Hardware chip/variant
        CHIP_FAMILY_DESC[improvWiFiParams.chipFamily],
        // Device name
        improvWiFiParams.deviceName
      };
      std::vector<uint8_t> data = improv::build_rpc_response(improv::GET_DEVICE_INFO, infos, false);
      sendResponse(data);
      break;
    }

    case improv::Command::GET_WIFI_NETWORKS:
    {
      getAvailableWifiNetworks();
      break;
    }

    default: {
      setError(improv::ERROR_UNKNOWN_RPC);
      return false;
    }
  }

  return true;
}
void ImprovWiFi::setDeviceInfo(improv::ChipFamily chipFamily, std::string firmwareName, std::string firmwareVersion, std::string deviceName) {
  improvWiFiParams.chipFamily = chipFamily;
  improvWiFiParams.firmwareName = firmwareName;
  improvWiFiParams.firmwareVersion = firmwareVersion;
  improvWiFiParams.deviceName = deviceName;  
}
void ImprovWiFi::setDeviceInfo(improv::ChipFamily chipFamily, std::string firmwareName, std::string firmwareVersion, std::string deviceName, std::string deviceUrl) {
  setDeviceInfo(chipFamily, firmwareName, firmwareVersion, deviceName);
  improvWiFiParams.deviceUrl = deviceUrl;
}


bool ImprovWiFi::isConnected() {
  return (WiFi.status() == WL_CONNECTED);
}

void ImprovWiFi::sendDeviceUrl(improv::Command cmd) {
  // URL where user can finish onboarding or use device
  // Recommended to use website hosted by device
  if (improvWiFiParams.deviceUrl.empty()) {
    improvWiFiParams.deviceUrl = String("http://" + WiFi.localIP().toString()).c_str();
  } else {
    replaceAll(improvWiFiParams.deviceUrl, "{LOCAL_IPV4}", WiFi.localIP().toString().c_str());
  }

  std::vector<uint8_t> data = improv::build_rpc_response(cmd, {improvWiFiParams.deviceUrl}, false);
  sendResponse(data);
}

void ImprovWiFi::onImprovWiFiError(OnImprovWiFiError *errorCallback) {
  onImprovWiFiErrorCallback = errorCallback;
}

void ImprovWiFi::onImprovWiFiConnected(OnImprovWiFiConnected *connectedCallback) {
  onImprovWiFiConnectedCallback = connectedCallback;
}

void ImprovWiFi::setCustomConnectWiFi(CustomConnectWiFi *connectWiFiCallBack) {
  customConnectWiFiCallback = connectWiFiCallBack;
}


bool ImprovWiFi::tryConnectToWifi(std::string ssid, std::string password) {
  uint8_t count = 0;

  if (isConnected()) {
    WiFi.disconnect();
    delay(100);
  }

  WiFi.begin(ssid.c_str(), password.c_str());

  while (!isConnected()) {
    delay(DELAY_MS_WAIT_WIFI_CONNECTION);
    if (count > MAX_ATTEMPTS_WIFI_CONNECTION) {
      WiFi.disconnect();
      return false;
    }
    count++;
  }

  return true;
}


void ImprovWiFi::getAvailableWifiNetworks() {

  int networkNum = WiFi.scanNetworks();

  for (int id = 0; id < networkNum; ++id) { 
    std::vector<uint8_t> data = improv::build_rpc_response(
            improv::GET_WIFI_NETWORKS, {WiFi.SSID(id), String(WiFi.RSSI(id)), (WiFi.encryptionType(id) == WIFI_AUTH_OPEN ? "NO" : "YES")}, false);
    sendResponse(data);
    delay(1);
  }
  //final response
  std::vector<uint8_t> data =
          improv::build_rpc_response(improv::GET_WIFI_NETWORKS, std::vector<std::string>{}, false);
  sendResponse(data);
}


inline void ImprovWiFi::replaceAll(std::string &str, const std::string& from, const std::string& to)
{
  size_t start_pos = 0;
  while((start_pos = str.find(from, start_pos)) != std::string::npos) {
      str.replace(start_pos, from.length(), to);
      start_pos += to.length();
  }
}


bool ImprovWiFi::parseImprovSerial(size_t position, uint8_t byte, const uint8_t *buffer) {
  if (position == 0)
    return byte == 'I';
  if (position == 1)
    return byte == 'M';
  if (position == 2)
    return byte == 'P';
  if (position == 3)
    return byte == 'R';
  if (position == 4)
    return byte == 'O';
  if (position == 5)
    return byte == 'V';

  if (position == 6)
    return byte == improv::IMPROV_SERIAL_VERSION;

  if (position <= 8)
    return true;

  uint8_t type = buffer[7];
  uint8_t data_len = buffer[8];

  if (position <= 8 + data_len)
    return true;

  if (position == 8 + data_len + 1) {
    uint8_t checksum = 0x00;
    for (size_t i = 0; i < position; i++)
      checksum += buffer[i];

    if (checksum != byte) {
      _position = 0;
      onErrorCallback(improv::Error::ERROR_INVALID_RPC);
      return false;
    }

    if (type == improv::ImprovSerialType::TYPE_RPC) {   
      _position = 0;  
      auto command = parseImprovData(&buffer[9], data_len, false);
      return onCommandCallback(command);
    }
  }

  return false;
}


improv::ImprovCommand ImprovWiFi::parseImprovData(const std::vector<uint8_t> &data, bool check_checksum) {
  return parseImprovData(data.data(), data.size(), check_checksum);
}

improv::ImprovCommand ImprovWiFi::parseImprovData(const uint8_t *data, size_t length, bool check_checksum) {
  improv::ImprovCommand improv_command;
  improv::Command command = (improv::Command) data[0];
  uint8_t data_length = data[1];

  if (data_length != length - 2 - check_checksum) {
    improv_command.command = improv::Command::UNKNOWN;
    return improv_command;
  }

  if (check_checksum) {
    uint8_t checksum = data[length - 1];

    uint32_t calculated_checksum = 0;
    for (uint8_t i = 0; i < length - 1; i++) {
      calculated_checksum += data[i];
    }

    if ((uint8_t) calculated_checksum != checksum) {
      improv_command.command = improv::Command::BAD_CHECKSUM;
      return improv_command;
    }
  }

  if (command == improv::Command::WIFI_SETTINGS) {
    uint8_t ssid_length = data[2];
    uint8_t ssid_start = 3;
    size_t ssid_end = ssid_start + ssid_length;

    uint8_t pass_length = data[ssid_end];
    size_t pass_start = ssid_end + 1;
    size_t pass_end = pass_start + pass_length;

    std::string ssid(data + ssid_start, data + ssid_end);
    std::string password(data + pass_start, data + pass_end);
    return {.command = command, .ssid = ssid, .password = password};
  }

  improv_command.command = command;
  return improv_command;
}


void ImprovWiFi::setState(improv::State state) {  
  
  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
  data.resize(11);
  data[6] = improv::IMPROV_SERIAL_VERSION;
  data[7] = improv::TYPE_CURRENT_STATE;
  data[8] = 1;
  data[9] = state;

  uint8_t checksum = 0x00;
  for (uint8_t d : data)
    checksum += d;
  data[10] = checksum;

  serial->write(data.data(), data.size());
}

void ImprovWiFi::setError(improv::Error error) {
  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
  data.resize(11);
  data[6] = improv::IMPROV_SERIAL_VERSION;
  data[7] = improv::TYPE_ERROR_STATE;
  data[8] = 1;
  data[9] = error;

  uint8_t checksum = 0x00;
  for (uint8_t d : data)
    checksum += d;
  data[10] = checksum;

  serial->write(data.data(), data.size());
}

void ImprovWiFi::sendResponse(std::vector<uint8_t> &response) {
  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
  data.resize(9);
  data[6] = improv::IMPROV_SERIAL_VERSION;
  data[7] = improv::TYPE_RPC_RESPONSE;
  data[8] = response.size();
  data.insert(data.end(), response.begin(), response.end());

  uint8_t checksum = 0x00;
  for (uint8_t d : data)
    checksum += d;
  data.push_back(checksum);

  serial->write(data.data(), data.size());
}

