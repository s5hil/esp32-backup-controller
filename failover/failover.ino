#include <WiFi.h>
#include <esp_now.h>
#include <esp_task_wdt.h>

#include "leader_fsm.h"
#include "soc/gpio_reg.h"

#if CONFIG_IDF_TARGET_ESP32S3
  const int PIN_LED = 4;
  const int PIN_ACTIVE = 5;
  const int PIN_HEARTBEAT = 6;
  const uint32_t BOOT_WAIT_MS = 50; // lower MAC, claimed first at powering on
  uint8_t PEER_MAC[6] = {0xA4, 0xF0, 0x0F, 0x90, 0x6F, 0x84};  // ESP32D
#else // esp32d
  const int PIN_LED = 25;
  const int PIN_ACTIVE = 26;
  const int PIN_HEARTBEAT = 27;
  const uint32_t BOOT_WAIT_MS = 50 + 500; // higher MAC, waits extra margin
  uint8_t PEER_MAC[6] = {0x1C, 0xDB, 0xD4, 0x40, 0x37, 0x60};  // ESP32S3
#endif

// timing
const uint32_t LEASE_MS = 25;
const uint32_t HEARTBEAT_MS = 5; // toggle period
const uint32_t STATE_SEND_MS = 10;
const uint32_t WDT_TIMEOUT_MS = 30; // hung board resets within ~100ms

struct __attribute__((packed)) StateMsg {
  uint32_t seq;
  uint32_t fadePos;
};

// sender side
uint32_t txSeq = 0;
uint32_t lastStateSend = 0;

int dropPercent = 0; // simulated radio loss, serial command "drop x"

// receiver side, written by esp-now callback, read by loop()
portMUX_TYPE rxLock = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t rxSeq = 0;
volatile uint32_t rxFadePos = 0;
volatile uint32_t rxAtMs = 0;   // standby's own clock when it arrived
volatile uint32_t rxCount = 0;
volatile uint32_t rxLost = 0;

// fade settings
const uint32_t PWM_FREQ = 5000; // Hz
const uint8_t PWM_BITS = 8;
const uint32_t STEP_MS = 4;
const uint32_t STEPS_PER_CYCLE = 510; // 255 up + 255 down (~2s per cycle)

// loop time tracking
uint32_t lastLoopUs = 0;
uint32_t maxLoopUs = 0;

struct FadeState {
  uint32_t position; // 0 to STEPS_PER_CYCLE-1
};
FadeState fade = {0};

LeaderFsm fsm(LEASE_MS, BOOT_WAIT_MS, 0); // boot wait counted from powering on

int lastHbLevel = LOW;
uint32_t lastHbToggle = 0;
uint32_t lastFadeStep = 0;

uint8_t brightnessFor(uint32_t pos) {
  uint32_t tri = (pos < 255) ? pos : (STEPS_PER_CYCLE - pos); // 0 to 255 to 1
  return (tri * tri) / 255; // squared so fade is smoother
}

void setActive(uint32_t t) {
  // resume from replicated stage, if any was received
  portENTER_CRITICAL(&rxLock);
  uint32_t count = rxCount, seq = rxSeq, pos = rxFadePos, at = rxAtMs;
  portEXIT_CRITICAL(&rxLock);

  if (count > 0) {
    uint32_t elapsed = ((int32_t)(t - at) > 0) ? (t - at) : 0; // how behind the snapshot is, on our clock
    fade.position = (pos + elapsed / STEP_MS) % STEPS_PER_CYCLE;
    txSeq = seq + 1; // continue the same sequence stream
    Serial.printf("[%lu ms] resume: seq=%lu, %lu ms stale, pos %lu -> %lu\n", t, seq, elapsed, pos, fade.position);
  }

  lastStateSend = t;
  pinMode(PIN_HEARTBEAT, OUTPUT);
  digitalWrite(PIN_HEARTBEAT, HIGH); // first edge immediately
  lastHbLevel = HIGH;
  lastHbToggle = t;
  lastFadeStep = t;
  digitalWrite(PIN_ACTIVE, HIGH);
  ledcAttach(PIN_LED, PWM_FREQ, PWM_BITS);
  ledcWrite(PIN_LED, brightnessFor(fade.position)); // show resumed brightness immediately
  Serial.printf("[%lu ms] -> ACTIVE\n", t);
}

void runHeartbeat(uint32_t t) {
  if (t - lastHbToggle >= HEARTBEAT_MS) {
    lastHbToggle += HEARTBEAT_MS;
    lastHbLevel = !lastHbLevel;
    digitalWrite(PIN_HEARTBEAT, lastHbLevel);
  }
}

void runFade(uint32_t t) {
  if (t - lastFadeStep >= STEP_MS) {
    lastFadeStep += STEP_MS;
    fade.position = (fade.position + 1) % STEPS_PER_CYCLE;
    ledcWrite(PIN_LED, brightnessFor(fade.position));
  }
}

void onStateReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(StateMsg)) return;
  StateMsg msg;
  memcpy(&msg, data, sizeof(msg));
  uint32_t now = millis();

  portENTER_CRITICAL(&rxLock);
  if (rxCount > 0 && msg.seq > rxSeq + 1) {
    rxLost += msg.seq - rxSeq - 1; // gap in sequence means lost messages
  }
  rxSeq = msg.seq;
  rxFadePos = msg.fadePos;
  rxAtMs = now;
  rxCount++;
  portEXIT_CRITICAL(&rxLock);
}

void sendState(uint32_t t) {
  if (t - lastStateSend >= STATE_SEND_MS) {
    lastStateSend += STATE_SEND_MS;
    StateMsg msg = {txSeq++, fade.position};
    if (random(100) < dropPercent) return; // simulated loss: seq still advanced
    esp_now_send(PEER_MAC, (const uint8_t*)&msg, sizeof(msg));
  }
}

void printRxStats(uint32_t t) {
  static uint32_t lastPrint = 0;
  if (t - lastPrint < 1000) return;
  lastPrint = t;

  portENTER_CRITICAL(&rxLock);
  uint32_t seq = rxSeq, pos = rxFadePos, count = rxCount, lost = rxLost;
  portEXIT_CRITICAL(&rxLock);

  Serial.printf("rx seq=%lu pos=%lu count=%lu lost=%lu wire=%s radio=%s\n", seq, pos, count, lost, fsm.wireSilent(t)  ? "SILENT" : "ok", fsm.radioSilent(t) ? "SILENT" : "ok");
}

void runCommand(const char* cmd) {
  int p;
  if (sscanf(cmd, "drop %d", &p) == 1) {
    dropPercent = constrain(p, 0, 100);
    Serial.printf("drop = %d%%\n", dropPercent);
  } else if (strcmp(cmd, "hang") == 0) {
    Serial.println("hanging...");
    Serial.flush();
    while (true) {} // simulated freeze
  }
}

void handleSerial() {
  static char buf[32];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      buf[len] = '\0';
      if (len > 0) runCommand(buf);
      len = 0;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

extern "C" void esp_task_wdt_isr_user_handler(void) {
  REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << PIN_ACTIVE) | (1UL << PIN_HEARTBEAT));  // latch LOW
  REG_WRITE(GPIO_ENABLE_W1TC_REG, (1UL << PIN_LED) | (1UL << PIN_ACTIVE) | (1UL << PIN_HEARTBEAT));  // stop driving
}

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_ACTIVE, OUTPUT);
  digitalWrite(PIN_ACTIVE, LOW);

  pinMode(PIN_HEARTBEAT, INPUT_PULLDOWN);
  lastHbLevel = digitalRead(PIN_HEARTBEAT);

  pinMode(PIN_LED, INPUT); // standby, not driving the shared led

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP NOW init failed");
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, PEER_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  esp_now_register_recv_cb(onStateReceive);

  esp_task_wdt_config_t wdt = {
    .timeout_ms = WDT_TIMEOUT_MS,
    .idle_core_mask = 0, // only watch our loop and not any idle tasks
    .trigger_panic = true // on timeout, panic will reset chip
  };

  if (esp_task_wdt_reconfigure(&wdt) == ESP_ERR_INVALID_STATE) { // if watchdog not running then start it
    esp_task_wdt_init(&wdt);
  }
  esp_task_wdt_add(NULL); // null = current task (the loop)

  fsm = LeaderFsm(LEASE_MS, BOOT_WAIT_MS, millis()); // boot wait starts now when we are able to listen
}

void printLoopStats(uint32_t t) {
  static uint32_t lastPrint = 0;
  if (t - lastPrint < 1000) return;
  lastPrint = t;
  Serial.printf("max loop gap = %lu us\n", maxLoopUs);
  maxLoopUs = 0;
}

void loop() {
  esp_task_wdt_reset(); // ping watchdog to show loop is alive
  handleSerial();

  uint32_t nowUs = micros();
  if (lastLoopUs != 0) {
    uint32_t dt = nowUs - lastLoopUs;
    
    if (dt > maxLoopUs) {
      maxLoopUs = dt;
    }
  }
  lastLoopUs = nowUs;

  uint32_t t = millis();
  Role before = fsm.role();

  if (before == Role::STANDBY) {
    int level = digitalRead(PIN_HEARTBEAT);
    if (level != lastHbLevel) {
      lastHbLevel = level;
      fsm.onWire(t);
    }

    static uint32_t seenCount = 0;
    portENTER_CRITICAL(&rxLock);
    uint32_t count = rxCount;
    portEXIT_CRITICAL(&rxLock);
    if (count != seenCount) { // new espnow message arrived since last loop
      seenCount = count;
      fsm.onRadio(t);
    }
  }

  Role role = fsm.update(t);

  if (before == Role::STANDBY && role == Role::ACTIVE) {
    setActive(t);
  }

  if (role == Role::ACTIVE) {
    runHeartbeat(t);
    runFade(t);
    sendState(t);
    printLoopStats(t);
  } else {
    printRxStats(t);
  }
}
