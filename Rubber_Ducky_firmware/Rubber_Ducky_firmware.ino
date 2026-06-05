#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <USB.h>
#include <USBHIDKeyboard.h>

WebServer server(80);
Preferences prefs;
USBHIDKeyboard keyboard;

bool scriptRunning = false;
unsigned long scriptStartTime = 0;

struct Config {
  char ssid[32];
  char password[32];
  char deviceName[32];
  bool apMode;
  int apChannel;
  uint16_t defaultDelay;
} config;

const char* AP_SSID_DEFAULT = "RubberDucky";
const char* AP_PASSWORD_DEFAULT = "12345678";
const int AP_CHANNEL_DEFAULT = 6;
const int AP_MAX_CONNECTIONS = 1;

// USB HID KEYBOARD SETUP
void initUSBHID() {
  keyboard.begin();
  USB.manufacturerName("Microsoft");
  USB.productName("USB Keyboard");
  USB.begin();
  Serial.println("[USB] HID Keyboard initialized");
}

struct KeyInfo {
  const char* name;
  uint8_t keycode;
  bool isSpecial; 
};

const KeyInfo specialKeys[] = {
  {"ENTER", KEY_RETURN, true},
  {"RETURN", KEY_RETURN, true},
  {"SPACE", ' ', false},
  {"TAB", KEY_TAB, true},
  {"BACKSPACE", KEY_BACKSPACE, true},
  {"DELETE", KEY_DELETE, true},
  {"INSERT", KEY_INSERT, true},
  {"HOME", KEY_HOME, true},
  {"END", KEY_END, true},
  {"PAGEUP", KEY_PAGE_UP, true},
  {"PAGE_UP", KEY_PAGE_UP, true},
  {"PAGEDOWN", KEY_PAGE_DOWN, true},
  {"PAGE_DOWN", KEY_PAGE_DOWN, true},
  {"UP", KEY_UP_ARROW, true},
  {"DOWN", KEY_DOWN_ARROW, true},
  {"LEFT", KEY_LEFT_ARROW, true},
  {"RIGHT", KEY_RIGHT_ARROW, true},
  {"ESCAPE", KEY_ESC, true},
  {"ESC", KEY_ESC, true},
  {"CAPSLOCK", KEY_CAPS_LOCK, true},
  {"NUMLOCK", KEY_NUM_LOCK, true},
  {"SCROLLLOCK", KEY_SCROLL_LOCK, true},
  {"PRINTSCREEN", KEY_PRINT_SCREEN, true},
  {"PAUSE", KEY_PAUSE, true},
  {"BREAK", KEY_PAUSE, true},
  {"F1", KEY_F1, true},
  {"F2", KEY_F2, true},
  {"F3", KEY_F3, true},
  {"F4", KEY_F4, true},
  {"F5", KEY_F5, true},
  {"F6", KEY_F6, true},
  {"F7", KEY_F7, true},
  {"F8", KEY_F8, true},
  {"F9", KEY_F9, true},
  {"F10", KEY_F10, true},
  {"F11", KEY_F11, true},
  {"F12", KEY_F12, true},
  {nullptr, 0, false}
};

uint8_t getKeyCode(const char* keyName) {
  for (int i = 0; specialKeys[i].name != nullptr; i++) {
    if (strcmp(keyName, specialKeys[i].name) == 0) {
      return specialKeys[i].keycode;
    }
  }
  if (strlen(keyName) == 1) {
    return (uint8_t)keyName[0];
  }
  return 0;
}

uint8_t getModifierByte(const char* modName) {
  if (strcmp(modName, "CTRL") == 0 || strcmp(modName, "CONTROL") == 0) return KEY_LEFT_CTRL;
  if (strcmp(modName, "SHIFT") == 0) return KEY_LEFT_SHIFT;
  if (strcmp(modName, "ALT") == 0) return KEY_LEFT_ALT;
  if (strcmp(modName, "GUI") == 0 || strcmp(modName, "WINDOWS") == 0 || 
      strcmp(modName, "COMMAND") == 0) return KEY_LEFT_GUI;
  return 0;
}

void executeDuckyScript(const String& scriptContent) {
  if (scriptRunning) {
    Serial.println("[Script] Another script running");
    return;
  }
  
  scriptRunning = true;
  scriptStartTime = millis();
  Serial.println("[Script] Starting");
  
  char* scriptBuf = (char*)malloc(scriptContent.length() + 1);
  if (!scriptBuf) {
    scriptRunning = false;
    return;
  }
  strcpy(scriptBuf, scriptContent.c_str());
  
  char* line = strtok(scriptBuf, "\n");
  int lineNum = 0;
  uint16_t defaultDelay = config.defaultDelay;
  char lastCommand[512] = {0};
  
  while (line && scriptRunning) {
    lineNum++;
    
    int len = strlen(line);
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == ' ')) {
      line[--len] = '\0';
    }
    while (len > 0 && (line[0] == ' ' || line[0] == '\t')) {
      line++;
      len--;
    }
    if (len == 0) {
      line = strtok(nullptr, "\n");
      continue;
    }
    char cmdBuf[512];
    strcpy(cmdBuf, line);
    for (int i = 0; cmdBuf[i]; i++) cmdBuf[i] = toupper(cmdBuf[i]);
    if (strncmp(cmdBuf, "REPEAT ", 7) == 0) {
      int repeatCount = atoi(cmdBuf + 7);
      for (int r = 0; r < repeatCount && scriptRunning; r++) {
        if (strlen(lastCommand) > 0) {
          char lastCopy[512];
          strcpy(lastCopy, lastCommand);
          char* lastCmdBuf = strtok(lastCopy, " ");
          if (lastCmdBuf) {
            char cmdUp[64];
            strcpy(cmdUp, lastCmdBuf);
            for (int i = 0; cmdUp[i]; i++) cmdUp[i] = toupper(cmdUp[i]);
            if (strcmp(cmdUp, "STRING") == 0) {
              char* arg = lastCmdBuf + 6;
              while (*arg == ' ') arg++;
              keyboard.print(arg);
            } else if (strcmp(cmdUp, "DELAY") == 0) {
              int ms = atoi(lastCmdBuf + 5);
              delay(ms);
            }
          }
        }
        if (defaultDelay > 0) delay(defaultDelay);
      }
      line = strtok(nullptr, "\n");
      continue;
    }
    strcpy(lastCommand, line);
    if (strncmp(cmdBuf, "REM", 3) == 0 && (cmdBuf[3] == ' ' || cmdBuf[3] == '\0')) {
      line = strtok(nullptr, "\n");
      continue;
    }
    if (strncmp(cmdBuf, "DEFAULTDELAY ", 13) == 0) {
      defaultDelay = atoi(cmdBuf + 13);
      Serial.printf("[Script] Default delay set to %d ms\n", defaultDelay);
      line = strtok(nullptr, "\n");
      continue;
    }
    if (strncmp(cmdBuf, "DEFAULT_DELAY ", 14) == 0) {
      defaultDelay = atoi(cmdBuf + 14);
      Serial.printf("[Script] Default delay set to %d ms\n", defaultDelay);
      line = strtok(nullptr, "\n");
      continue;
    }

    if (strncmp(cmdBuf, "DELAY ", 6) == 0) {
      int ms = atoi(cmdBuf + 6);
      delay(ms);
      line = strtok(nullptr, "\n");
      continue;
    }

    if (strncmp(cmdBuf, "STRING ", 7) == 0) {
      const char* str = line + 7;
      keyboard.print(str);
      Serial.printf("[Script] Typed: %s\n", str);
      if (defaultDelay > 0) delay(defaultDelay);
      line = strtok(nullptr, "\n");
      continue;
    }
    
    if (strncmp(cmdBuf, "STRINGLN ", 9) == 0) {
      const char* str = line + 9;
      keyboard.print(str);
      keyboard.press(KEY_RETURN);
      keyboard.release(KEY_RETURN);
      if (defaultDelay > 0) delay(defaultDelay);
      line = strtok(nullptr, "\n");
      continue;
    }
// KEY COMBINATIONS
    {
      char* token = strtok(cmdBuf, " ");
      uint8_t modifiers = 0;
      uint8_t finalKey = 0;
      int tokenCount = 0;
      char tokens[16][64];
      int tokenIdx = 0;
      strcpy(cmdBuf, line); 
      token = strtok(cmdBuf, " ");
      while (token && tokenIdx < 16) {
        strcpy(tokens[tokenIdx++], token);
        token = strtok(nullptr, " ");
      }
      for (int i = 0; i < tokenIdx; i++) {
        char keyUp[64];
        strcpy(keyUp, tokens[i]);
        for (int j = 0; keyUp[j]; j++) keyUp[j] = toupper(keyUp[j]);
        uint8_t mod = getModifierByte(keyUp);
        if (mod != 0) {
          modifiers |= mod; 
        } else {
          uint8_t kc = getKeyCode(keyUp);
          if (kc != 0) {
            finalKey = kc;
          }
        }
      }

      if (finalKey != 0 || modifiers != 0) {
        if (modifiers & KEY_LEFT_CTRL) keyboard.press(KEY_LEFT_CTRL);
        if (modifiers & KEY_LEFT_SHIFT) keyboard.press(KEY_LEFT_SHIFT);
        if (modifiers & KEY_LEFT_ALT) keyboard.press(KEY_LEFT_ALT);
        if (modifiers & KEY_LEFT_GUI) keyboard.press(KEY_LEFT_GUI);
        if (finalKey != 0) {
          keyboard.press(finalKey);
        }
        delay(50);
        keyboard.releaseAll();
        delay(50);
        if (finalKey != 0) {
          Serial.printf("[Script] Key combination executed\n");
        }
        if (defaultDelay > 0) delay(defaultDelay);
      }
    }
    line = strtok(nullptr, "\n");
    yield(); 
  } 
  free(scriptBuf);
  scriptRunning = false;
  unsigned long execTime = millis() - scriptStartTime;
}
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("Mount Failed");
    return;
  }
  if (!SPIFFS.exists("/scripts")) {
    File dir = SPIFFS.open("/scripts", FILE_WRITE);
    dir.close();
  }
}
void initPreferences() {
  prefs.begin("ducky", false);
}
void loadConfig() {
  strcpy(config.ssid, prefs.getString("ssid", "").c_str());
  strcpy(config.password, prefs.getString("password", "").c_str());
  strcpy(config.deviceName, prefs.getString("deviceName", "RubberDucky").c_str());
  config.apMode = prefs.getBool("apMode", true);
  config.apChannel = prefs.getInt("apChannel", AP_CHANNEL_DEFAULT);
  config.defaultDelay = prefs.getUShort("defaultDelay", 100);
  
  if (strlen(config.ssid) == 0) {
    config.apMode = true;
  }
}

void saveConfig() {
  prefs.putString("ssid", config.ssid);
  prefs.putString("password", config.password);
  prefs.putString("deviceName", config.deviceName);
  prefs.putBool("apMode", config.apMode);
  prefs.putInt("apChannel", config.apChannel);
  prefs.putUShort("defaultDelay", config.defaultDelay);
}

String readFile(const char* path) {
  if (!SPIFFS.exists(path)) return "";
  File file = SPIFFS.open(path, FILE_READ);
  String content = "";
  while (file.available()) {
    content += (char)file.read();
  }
  file.close();
  return content;
}

void writeFile(const char* path, const String& content) {
  File file = SPIFFS.open(path, FILE_WRITE);
  if (file) {
    file.print(content);
    file.close();
    Serial.println(path);
  }
}

void deleteFile(const char* path) {
  if (SPIFFS.exists(path)) {
    SPIFFS.remove(path);
    Serial.println(path);
  }
}

// wifi
void startAPMode() {
  Serial.println("Starting WIFI");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(config.deviceName, AP_PASSWORD_DEFAULT, config.apChannel, 0, AP_MAX_CONNECTIONS);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Running on: ");
  Serial.println(IP);
}

void startClientMode() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(WiFi.localIP());
  } else {
    config.apMode = true;
    startAPMode();
  }
}
// website
void handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Rubber Ducky Control</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { font-family: 'Courier New', monospace; background: #1a1a1a; color: #e0e0e0; }
    .container { max-width: 1000px; margin: 20px auto; padding: 0 20px; }
    h1 { color: #00ff88; margin: 30px 0 20px; }
    .nav { background: #222; padding: 15px; border-radius: 5px; margin-bottom: 20px; }
    .nav a { color: #00ff88; text-decoration: none; margin-right: 20px; }
    .nav a:hover { color: #fff; }
    .section { background: #2a2a2a; padding: 20px; border: 1px solid #444; border-radius: 5px; margin-bottom: 20px; }
    .section h2 { color: #00ff88; margin-bottom: 15px; border-bottom: 1px solid #444; padding-bottom: 10px; }
    .form-group { margin-bottom: 15px; }
    label { display: block; margin-bottom: 5px; color: #aaa; font-size: 12px; }
    input, select, textarea { width: 100%; padding: 8px; background: #1e1e1e; color: #e0e0e0; border: 1px solid #444; border-radius: 3px; font-family: monospace; }
    textarea { resize: vertical; min-height: 200px; }
    button { padding: 10px 20px; background: #00ff88; color: #000; border: none; border-radius: 3px; cursor: pointer; font-weight: bold; margin-right: 10px; }
    button:hover { background: #00cc66; }
    button.danger { background: #ff4444; }
    button.danger:hover { background: #ff2222; }
    .script-item { background: #1e1e1e; padding: 12px; margin: 8px 0; border-left: 3px solid #00ff88; border-radius: 3px; }
    .script-item-name { color: #00ff88; font-weight: bold; }
    .script-item-actions button { padding: 5px 10px; font-size: 12px; }
    .status { padding: 10px; margin-bottom: 15px; border-radius: 3px; }
    .status.success { background: #1a3a1a; border: 1px solid #00ff88; color: #00ff88; }
    .status.error { background: #3a1a1a; border: 1px solid #ff4444; color: #ff4444; }
    .status.info { background: #1a2a3a; border: 1px solid #00aaff; color: #00aaff; }
    .executing { color: #ffff00; }
    hr { border: none; border-top: 1px solid #444; margin: 15px 0; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Rubber Ducky Control</h1>
    <div class="nav">
      <a href="/">Dashboard</a>
      <a href="#scripts">Scripts</a>
      <a href="#editor">Editor</a>
      <a href="#config">Settings</a>
    </div>
    
    <div id="status"></div>
    
    <div id="dashboard" class="section">
      <h2>Dashboard</h2>
      <p id="status-text">Loading...</p>
      <hr>
      <button onclick="location.reload()">Refresh</button>
    </div>
    
    <div id="scripts" class="section">
      <h2>Scripts</h2>
      <div id="scriptList"></div>
      <button onclick="refreshScripts()">Refresh List</button>
    </div>
    
    <div id="editor" class="section">
      <h2>Script Editor</h2>
      <div class="form-group">
        <label>Script Name:</label>
        <input type="text" id="scriptName" placeholder="my_script.txt">
      </div>
      <div class="form-group">
        <label>Script Content (DuckyScript format):</label>
        <textarea id="scriptContent" placeholder="REM This is a comment&#10;DELAY 1000&#10;STRING Hello World&#10;ENTER"></textarea>
      </div>
      <button onclick="saveCurrentScript()" style="background: #00ff88;">Save Script</button>
      <button onclick="loadScriptToEditor()" id="editBtn" style="display:none;">Load Selected</button>
      <hr>
      <h3 style="color: #00aa88; margin-top: 20px;">DuckyScript Reference:</h3>
      <pre style="background: #1e1e1e; padding: 10px; border: 1px solid #444; overflow-x: auto; font-size: 11px;">
REM comment              - Comments (ignored)
DELAY [ms]              - Wait milliseconds
STRING text             - Type text
STRINGLN text           - Type text + ENTER
ENTER / RETURN          - Press Enter key
SPACE                   - Press Space
TAB                     - Press Tab
BACKSPACE               - Backspace key
DELETE                  - Delete key
ESCAPE / ESC            - Escape key
HOME / END              - Home/End keys
PAGEUP / PAGEDOWN       - Page up/down
UP / DOWN / LEFT / RIGHT - Arrow keys
F1 to F12               - Function keys
CTRL [key]              - Hold CTRL + key
SHIFT [key]             - Hold SHIFT + key
ALT [key]               - Hold ALT + key
GUI [key]               - Hold Windows key
CAPSLOCK / NUMLOCK      - Lock keys
DEFAULTDELAY [ms]       - Set delay between commands
REPEAT [n]              - Repeat last command n times
      </pre>
    </div>
    
    <div id="config" class="section">
      <h2>Settings</h2>
      <div class="form-group">
        <label>Device Name:</label>
        <input type="text" id="deviceName" placeholder="RubberDucky">
      </div>
      <div class="form-group">
        <label>Default Command Delay (ms):</label>
        <input type="number" id="defaultDelay" value="100" min="0" max="10000">
      </div>
      <div class="form-group">
        <label>Mode:</label>
        <select id="mode">
          <option value="ap">Access Point (AP)</option>
          <option value="client">Client Mode (WiFi)</option>
        </select>
      </div>
      <div id="clientSettings" style="display:none;">
        <div class="form-group">
          <label>WiFi SSID:</label>
          <input type="text" id="ssid">
        </div>
        <div class="form-group">
          <label>WiFi Password:</label>
          <input type="password" id="password">
        </div>
      </div>
      <button onclick="saveConfig()" style="background: #00ff88;">Save Settings</button>
    </div>
  </div>

  <script>
    let currentScript = null;
    
    function showStatus(msg, type = 'info') {
      const div = document.getElementById('status');
      div.innerHTML = `<div class="status ${type}">${msg}</div>`;
      setTimeout(() => { div.innerHTML = ''; }, 5000);
    }
    
    function refreshScripts() {
      fetch('/api/scripts')
        .then(r => r.json())
        .then(data => {
          let html = '';
          data.scripts.forEach(name => {
            html += `
              <div class="script-item">
                <div class="script-item-name">${name}</div>
                <div class="script-item-actions">
                  <button onclick="editScript('${name}')">Edit</button>
                  <button onclick="runScript('${name}')">Run</button>
                  <button onclick="deleteScript('${name}')" class="danger">Delete</button>
                </div>
              </div>
            `;
          });
          document.getElementById('scriptList').innerHTML = html || '<p>No scripts found</p>';
        });
    }
    
    function editScript(name) {
      currentScript = name;
      fetch(`/api/scripts/content?name=${name}`)
        .then(r => r.text())
        .then(content => {
          document.getElementById('scriptName').value = name;
          document.getElementById('scriptContent').value = content;
          window.location.hash = '#editor';
        });
    }
    
    function saveCurrentScript() {
      const name = document.getElementById('scriptName').value;
      const content = document.getElementById('scriptContent').value;
      
      if (!name) { showStatus('Enter script name', 'error'); return; }
      
      const formData = new FormData();
      formData.append('name', name);
      formData.append('content', content);
      
      fetch('/api/scripts/content', { method: 'POST', body: formData })
        .then(r => r.json())
        .then(d => {
          showStatus('Script saved!', 'success');
          refreshScripts();
        });
    }
    
    function deleteScript(name) {
      if (!confirm(`Delete "${name}"?`)) return;
      fetch('/api/scripts/delete', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name })
      })
      .then(r => r.json())
      .then(d => { showStatus('Deleted', 'success'); refreshScripts(); });
    }
    
    function runScript(name) {
      showStatus(`<span class="executing">Running: ${name}</span>`, 'info');
      fetch('/api/scripts/run', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name })
      })
      .then(r => r.json())
      .then(d => showStatus('Script executed!', 'success'))
      .catch(e => showStatus('Error: ' + e, 'error'));
    }
    
    function saveConfig() {
      const data = {
        deviceName: document.getElementById('deviceName').value,
        defaultDelay: parseInt(document.getElementById('defaultDelay').value),
        apMode: document.getElementById('mode').value === 'ap',
        ssid: document.getElementById('ssid').value,
        password: document.getElementById('password').value,
        apChannel: 6
      };
      
      fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
      })
      .then(r => r.json())
      .then(d => showStatus('Config saved!', 'success'));
    }
    
    document.getElementById('mode').addEventListener('change', (e) => {
      document.getElementById('clientSettings').style.display = 
        e.target.value === 'client' ? 'block' : 'none';
    });
    fetch('/api/config')
      .then(r => r.json())
      .then(d => {
        document.getElementById('deviceName').value = d.deviceName;
        document.getElementById('defaultDelay').value = d.defaultDelay || 100;
        document.getElementById('mode').value = d.apMode ? 'ap' : 'client';
        document.getElementById('ssid').value = d.ssid || '';
        document.getElementById('password').value = d.password || '';
        if (!d.apMode) document.getElementById('clientSettings').style.display = 'block';
      });
    
    refreshScripts();
  </script>
</body>
</html>
  )";
  
  server.send(200, "text/html", html);
}

void handleGetConfig() {
  loadConfig();
  StaticJsonDocument<512> doc;
  doc["deviceName"] = config.deviceName;
  doc["apMode"] = config.apMode;
  doc["ssid"] = config.ssid;
  doc["password"] = config.password;
  doc["apChannel"] = config.apChannel;
  doc["defaultDelay"] = config.defaultDelay;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSaveConfig() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, server.arg("plain"));
    
    strcpy(config.deviceName, doc["deviceName"] | "RubberDucky");
    strcpy(config.ssid, doc["ssid"] | "");
    strcpy(config.password, doc["password"] | "");
    config.apMode = doc["apMode"] | true;
    config.apChannel = doc["apChannel"] | 6;
    config.defaultDelay = doc["defaultDelay"] | 100;
    
    saveConfig();
    
    StaticJsonDocument<128> response;
    response["message"] = "Configuration saved. Rebooting...";
    String jsonStr;
    serializeJson(response, jsonStr);
    server.send(200, "application/json", jsonStr);
    
    delay(1000);
    ESP.restart();
  }
}

void handleGetScripts() {
  StaticJsonDocument<1024> doc;
  JsonArray scripts = doc.createNestedArray("scripts");
  
  File root = SPIFFS.open("/scripts");
  File file = root.openNextFile();
  
  while (file) {
    if (!file.isDirectory()) {
      scripts.add(file.name());
    }
    file = root.openNextFile();
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetScriptContent() {
  if (server.hasArg("name")) {
    String name = server.arg("name");
    String path = "/scripts/" + name;
    String content = readFile(path.c_str());
    server.send(200, "text/plain", content);
  } else {
    server.send(400, "text/plain", "Missing name parameter");
  }
}

void handleSaveScriptContent() {
  if (server.hasArg("name") && server.hasArg("content")) {
    String name = server.arg("name");
    String content = server.arg("content");
    
    String path = "/scripts/" + name;
    writeFile(path.c_str(), content);
    
    StaticJsonDocument<128> doc;
    doc["message"] = "Script saved";
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleDeleteScript() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));
    
    String name = doc["name"];
    String path = "/scripts/" + name;
    deleteFile(path.c_str());
    
    StaticJsonDocument<128> response;
    response["message"] = "Script deleted";
    String jsonStr;
    serializeJson(response, jsonStr);
    server.send(200, "application/json", jsonStr);
  }
}

void handleRunScript() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));
    
    String name = doc["name"];
    String path = "/scripts/" + name;
    String content = readFile(path.c_str());
    
    if (content.length() == 0) {
      StaticJsonDocument<128> response;
      response["message"] = "Script not found";
      String jsonStr;
      serializeJson(response, jsonStr);
      server.send(404, "application/json", jsonStr);
      return;
    }
    
    // Execute the DuckyScript
    Serial.printf("[Web] Executing script: %s\n", name.c_str());
    executeDuckyScript(content);
    
    StaticJsonDocument<128> response;
    response["message"] = "Script execution started";
    String jsonStr;
    serializeJson(response, jsonStr);
    server.send(200, "application/json", jsonStr);
  } else {
    server.send(400, "text/plain", "Bad request");
  }
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handleSaveConfig);
  server.on("/api/scripts", HTTP_GET, handleGetScripts);
  server.on("/api/scripts/content", HTTP_GET, handleGetScriptContent);
  server.on("/api/scripts/content", HTTP_POST, handleSaveScriptContent);
  server.on("/api/scripts/delete", HTTP_POST, handleDeleteScript);
  server.on("/api/scripts/run", HTTP_POST, handleRunScript);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not Found");
  });
}

// Setup
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n Rubber Ducky");
  Serial.println("[*] With DuckyScript Support & USB HID");
  initUSBHID();
  delay(500);
  initSPIFFS();
  initPreferences();
  loadConfig();
  if (config.apMode) {
    startAPMode();
  } else {
    startClientMode();
  }
  setupWebServer();
  server.begin();
}

void loop() {
  server.handleClient();
  delay(10);
}
