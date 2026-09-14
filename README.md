# Voip-SIP-Protocol

# ESP32 SIP VoIP Speaker

โปรเจกต์ลำโพง VoIP บน **ESP32** สำหรับรับสายผ่านระบบ **SIP** โดยเชื่อมต่อกับ **Asterisk Server** ผ่านเครือข่าย Wi-Fi

### Tech

* ESP32 / C++
* SIP / RTP / UDP
* Asterisk
* G.711 μ-law
* I2S / PCM5102 DAC
* Wi-Fi

### Workflow

มี **Server บน Linux** สำหรับรันและจัดการ **Asterisk**
ผู้ส่งสามารถใช้แอปพลิเคชัน VoIP ที่ลงทะเบียนหมายเลขไว้กับ Server เพื่อโทรมายัง ESP32

ESP32 ทำหน้าที่เป็น **SIP Client** รับสัญญาณเสียงผ่าน **RTP** จาก Asterisk จากนั้นถอดรหัส **G.711 μ-law** และส่งออกเสียงผ่าน **I2S → PCM5102 → Speaker**
