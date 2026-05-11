#include <WiFi.h>
#include <WiFiUdp.h>
#include <driver/i2s.h> // เปลี่ยนมาใช้ I2S สำหรับ PCM5102

// --- ข้อมูล WiFi ---
const char* ssid = "KMITL-WiFi";
const char* password = "";

// --- ข้อมูล Asterisk ---
const char* sip_user = "1011";
const char* asterisk_ip = "172.16.8.100";
const int asterisk_port = 5060;

WiFiUDP udp;    // สำหรับ SIP (5060)
WiFiUDP rtpUdp; // สำหรับ Audio (19836)
const int rtp_port = 19836;

bool inCall = false; // ป้องกันเสียงซ่า/รบกวนเวลาไม่ได้อยู่ในสาย

// --- การตั้งค่าขา I2S สำหรับ PCM5102 ---
#define I2S_BCK_PIN 27 // ขา BCLK
#define I2S_WS_PIN 26  // ขา LRC (Word Select)
#define I2S_DATA_PIN 25 // ขา DIN (Data In)

// ฟังก์ชันแปลงเสียง G.711 u-law (จาก Asterisk) เป็น 16-bit PCM (สำหรับ PCM5102)
int16_t mulaw_decode(uint8_t mulaw) {
  uint8_t u_val = ~mulaw;
  int sign = (u_val & 0x80) ? -1 : 1;
  int exponent = (u_val >> 4) & 0x07;
  int mantissa = u_val & 0x0F;
  int sample = (mantissa << 3) + 132;
  sample <<= exponent;
  return sign * (sample - 132);
}

String extractHeader(String msg, String headerName) {
  int start = msg.indexOf(headerName);
  if (start == -1) return "";
  int valueStart = start + headerName.length();
  int end = msg.indexOf("\r\n", valueStart);
  String result = msg.substring(valueStart, end);
  result.trim(); 
  return result;
}

void sendSipRegister() {
  String localIp = WiFi.localIP().toString();
  String packet = "REGISTER sip:" + String(asterisk_ip) + " SIP/2.0\r\n";
  packet += "Via: SIP/2.0/UDP " + localIp + ":5060;branch=z9hG4bK-" + String(random(1000, 9999)) + "\r\n";
  packet += "From: <sip:" + String(sip_user) + "@" + String(asterisk_ip) + ">;tag=" + String(random(1000, 9999)) + "\r\n";
  packet += "To: <sip:" + String(sip_user) + "@" + String(asterisk_ip) + ">\r\n";
  packet += "Call-ID: " + String(random(100000, 999999)) + "@" + localIp + "\r\n";
  packet += "CSeq: 1 REGISTER\r\n";
  packet += "Contact: <sip:" + String(sip_user) + "@" + localIp + ":5060>\r\n";
  packet += "Max-Forwards: 70\r\n";
  packet += "Expires: 60\r\n";
  packet += "Content-Length: 0\r\n\r\n";

  udp.beginPacket(asterisk_ip, asterisk_port);
  udp.print(packet);
  udp.endPacket();
  Serial.println(">>> REGISTER Sent");
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected!");
  
  // --- ตั้งค่า I2S สำหรับชิปเสียง PCM5102 ---
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 8192, // Asterisk ส่งเสียงมาที่ 8000Hz
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, // PCM5102 ต้องการเสียง 16-bit
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true // ป้องกันเสียงค้างตอนไม่มีคนพูด
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_DATA_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  
  udp.begin(5060);
  rtpUdp.begin(rtp_port); // เปิดพอร์ต 19836
  sendSipRegister();
}

void loop() {
  // --- ส่วนจัดการ SIP ---
  int sipSize = udp.parsePacket();
  if (sipSize) {
    char buf[1500];
    int len = udp.read(buf, 1500);
    buf[len] = 0;
    String msg = String(buf);

    String via = extractHeader(msg, "Via:");
    String from = extractHeader(msg, "From:");
    String to = extractHeader(msg, "To:");
    String callId = extractHeader(msg, "Call-ID:");
    String cseq = extractHeader(msg, "CSeq:");

    // 1. ตอบรับสายเรียกเข้า (รวมถึงสายที่ดึงเข้า ConfBridge)
    if (msg.startsWith("INVITE")) {
      Serial.println("\n[SIP] Incoming Call (Auto Answer)!");
      String localIp = WiFi.localIP().toString();
      String tag = String(random(100, 999));

      String ring = "SIP/2.0 180 Ringing\r\nVia: " + via + "\r\nFrom: " + from + "\r\nTo: " + to + ";tag=" + tag + "\r\nCall-ID: " + callId + "\r\nCSeq: " + cseq + "\r\nContent-Length: 0\r\n\r\n";
      udp.beginPacket(asterisk_ip, 5060); udp.print(ring); udp.endPacket();
      delay(100);

      String sdp = "v=0\r\no=- 123 123 IN IP4 " + localIp + "\r\ns=-\r\nc=IN IP4 " + localIp + "\r\nt=0 0\r\nm=audio 19836 RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\n";
      String ok = "SIP/2.0 200 OK\r\nVia: " + via + "\r\nFrom: " + from + "\r\nTo: " + to + ";tag=" + tag + "\r\nCall-ID: " + callId + "\r\nCSeq: " + cseq + "\r\nContact: <sip:" + String(sip_user) + "@" + localIp + ">\r\nContent-Type: application/sdp\r\nContent-Length: " + String(sdp.length()) + "\r\n\r\n" + sdp;
      udp.beginPacket(asterisk_ip, 5060); udp.print(ok); udp.endPacket();
      
      Serial.println("[SIP] In Call / Connected to ConfBridge!");
      inCall = true; 
    }
    
    // 2. ออกจากห้องประชุม / วางสาย
    else if (msg.startsWith("BYE") || msg.startsWith("CANCEL")) {
      Serial.println("\n[SIP] Call Ended / Kicked from ConfBridge");
      String ok = "SIP/2.0 200 OK\r\nVia: " + via + "\r\nFrom: " + from + "\r\nTo: " + to + "\r\nCall-ID: " + callId + "\r\nCSeq: " + cseq + "\r\nContent-Length: 0\r\n\r\n";
      udp.beginPacket(asterisk_ip, 5060); udp.print(ok); udp.endPacket();
      inCall = false; 
    }
  }

  // --- ส่วนจัดการเสียง (RTP ไปยัง PCM5102) ---
  int rtpSize = rtpUdp.parsePacket();
  if (rtpSize > 0) {
    uint8_t rtpBuf[rtpSize];
    rtpUdp.read(rtpBuf, rtpSize);

    // เล่นเสียงเฉพาะตอนอยู่ในสายเท่านั้น
    if (inCall && rtpSize > 12) {
      int num_samples = rtpSize - 12;
      int16_t pcm_samples[num_samples]; // เตรียมบัฟเฟอร์สำหรับ 16-bit
      
      // แปลง 8-bit u-law เป็น 16-bit PCM เพื่อให้เสียงใสขึ้น
      for (int i = 0; i < num_samples; i++) {
        pcm_samples[i] = mulaw_decode(rtpBuf[12 + i]);
      }

      // โยนข้อมูลเสียงลงบอร์ด PCM5102
      size_t bytes_written;
      i2s_write(I2S_NUM_0, pcm_samples, num_samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    }
  }

  // ส่ง Register ทุก 30 วินาที
  static unsigned long reg = 0;
  if (millis() - reg > 30000) { sendSipRegister(); reg = millis(); }
}