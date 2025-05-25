// this code now works with a html code and firmware encoded.
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
const char* ssid     = "Colors 4G";
const char* password = "animesh4321*";

const uint16_t IR_RECEIVER_PIN = 15;
const uint16_t IR_EMITTER_PIN  = 4;   // IR LED (transistor‑driven recommended)
const uint32_t CARRIER_KHZ      = 38; // Typical IR carrier

// -----------------------------------------------------------------------------
// Embedded single‑page UI (served from flash)
// -----------------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32‑S3 IR Remote</title><style>body{font-family:Arial,Helvetica,sans-serif;margin:2rem;max-width:600px}h2{color:#333}section{margin-bottom:1rem}button{padding:.5rem 1rem;margin-left:.3rem;cursor:pointer}input{padding:.45rem;width:260px}#log{white-space:pre-wrap;background:#f7f7f7;border:1px solid #ccc;padding:1rem;height:140px;overflow:auto}</style></head><body><h2>ESP32‑S3 IR Remote Control</h2><section><button id="btnCapture">Capture IR</button><span id="irCode">No code captured</span></section><section><input id="irInput" placeholder="Enter hex code or leave blank to resend last" /><button id="btnEmit">Emit IR</button></section><h3>Debug Log</h3><div id="log">Ready…</div><script>const $=id=>document.getElementById(id);const log=m=>{console.log(m);$("log").textContent+="\n"+m;$("log").scrollTop=$("log").scrollHeight};$("btnCapture").onclick=()=>{fetch("/capture").then(r=>{log(`GET /capture → ${r.status}`);if(r.status===204)throw Error("No IR signal");return r.text()}).then(txt=>{const j=JSON.parse(txt);$("irCode").textContent=`Captured: ${j.proto} 0x${j.code}`;$("irInput").value=j.code}).catch(e=>alert(e.message))};$("btnEmit").onclick=()=>{const code=$("irInput").value.trim();fetch("/emit",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:`ir_code=${encodeURIComponent(code)}`}).then(r=>r.text()).then(t=>{alert(t);log(`POST /emit → ${t}`)}).catch(e=>alert(e.message))};</script></body></html>
)rawliteral";

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
IRrecv  irrecv(IR_RECEIVER_PIN);
IRsend  irsend(IR_EMITTER_PIN);
AsyncWebServer server(80);

decode_results         lastResults;      // holds the most recent capture
uint16_t               rawCopy[kRawBufSize];
uint16_t               rawLen = 0;

// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------
String protoToString(decode_type_t t){
  switch(t){
    case NEC: return "NEC"; case SONY: return "SONY"; case RC5: return "RC5";
    case RC6: return "RC6"; case SAMSUNG: return "SAMSUNG"; case PANASONIC: return "PANASONIC";
    default:  return "UNKNOWN"; }
}

void resendLast(){
  if(lastResults.decode_type==NEC){ irsend.sendNEC(lastResults.value, lastResults.bits); }
  else if(lastResults.decode_type==SONY){ irsend.sendSony(lastResults.value, lastResults.bits); }
  else if(lastResults.decode_type==RC5){ irsend.sendRC5(lastResults.value, lastResults.bits); }
  else if(lastResults.decode_type==RC6){ irsend.sendRC6(lastResults.value, lastResults.bits); }
  else if(lastResults.decode_type==SAMSUNG){ irsend.sendSAMSUNG(lastResults.value, lastResults.bits); }
  else if(lastResults.decode_type==PANASONIC){
    uint16_t addr = (lastResults.value >> 32) & 0xFFFF;
    uint32_t data =  lastResults.value & 0xFFFFFFFFULL;
    irsend.sendPanasonic(addr, data);
  }
  else { // UNKNOWN → raw
    if(rawLen>0) irsend.sendRaw(rawCopy, rawLen, CARRIER_KHZ);
  }
}

String irCodeToHex(uint64_t v, uint8_t bits){
  char buf[19]; // up to 64‑bit hex string
  sprintf(buf,"%0*llX", (bits+3)/4, v);
  return String(buf);
}

// -----------------------------------------------------------------------------
// Setup & routes
// -----------------------------------------------------------------------------
void setup(){
  Serial.begin(115200);
  irrecv.enableIRIn();
  irsend.begin();

  WiFi.begin(ssid, password);
  while(WiFi.status()!=WL_CONNECTED){ delay(500); Serial.print('.'); }
  Serial.printf("\n[IP] %s\n", WiFi.localIP().toString().c_str());

  // Root page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200,"text/html",INDEX_HTML);
  });

  // Capture route
  server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *request){
    if(irrecv.decode(&lastResults)){
      // copy raw buf for unknown protocols
      rawLen = lastResults.rawlen;
      if(rawLen>kRawBufSize) rawLen = kRawBufSize;
      memcpy(rawCopy, lastResults.rawbuf, rawLen*sizeof(uint16_t));

      String json = String("{\"proto\":\"")+protoToString(lastResults.decode_type)+"\",\"bits\":"+String(lastResults.bits)+",\"code\":\""+irCodeToHex(lastResults.value,lastResults.bits)+"\"}";
      irrecv.resume();
      Serial.printf("[CAPTURE] %s\n", json.c_str());
      request->send(200,"application/json", json);
    }else{
      request->send(204,"text/plain","No signal");
    }
  });

  // Emit route
  server.on("/emit", HTTP_POST, [](AsyncWebServerRequest *request){
    if(request->hasParam("ir_code", true)){
      String hex=request->getParam("ir_code", true)->value();
      if(hex.length()){
        uint64_t val=strtoull(hex.c_str(),nullptr,16);
        irsend.sendNEC(val, 32); // manual send as NEC 32‑bit
        Serial.printf("[EMIT] Manual NEC 0x%s\n", hex.c_str());
      }else{
        resendLast();
        Serial.println("[EMIT] Resent last capture");
      }
      request->send(200,"text/plain","Sent");
    }else{
      request->send(400,"text/plain","ir_code missing");
    }
  });

  server.begin();
  Serial.println("[OK] Server started");
}

void loop(){ /* async */ }
