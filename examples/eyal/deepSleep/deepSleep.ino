/*
 * This sketch used to measure the Arduino performance on the esp8266.
 */

#include <ESP8266WiFi.h>
#include <WiFiUDP.h>

#define ONEWIRE_SEARCH    0
#include <OneWire.h>

extern "C" {
  #include <user_interface.h>   // for RTC functions
}

#include "user_config.h"    // user config

static OneWire            ds(OW_PIN);

/* DS18x20 registers
 */
#define CONVERT_T         0x44
#define COPY_SCRATCHPAD   0x48
#define WRITE_SCRATCHPAD  0x4E
#define RECALL_EEPROM     0xB8
#define READ_SCRATCHPAD   0xBE
#define CHIP_DS18B20      0x10  // 16
#define CHIP_DS18S20      0x28  // 40

static struct {
  uint32_t magic;
#define RTC_magic         0xd1dad1d1  // L
  uint32_t runCount;      // count
  uint32_t failSoft;      // count
  uint32_t failHard;      // count
  uint32_t failRead;      // count
  uint32_t lastTime;      // us
  uint32_t totalTime;     // ms
} rtcMem;

static WiFiUDP            UDP;

static unsigned long      time_start;
static unsigned long      time_read;
static unsigned long      time_wifi;
static unsigned long      time_udp_bug;
static float              dCf;


static void
show_frac(char *buf, int bsize, byte precision, long v)
{
  long scale = 1;
  switch (precision) {
  case 4:
    scale *= 10;
  case 3:
    scale *= 10;
  case 2:
    scale *= 10;
  case 1:
    scale *= 10;
    break;
  case 0:
  default:
    snprintf (buf, bsize, "%lu", v);
    return;
  }
  long int u = v/scale;
  long int f = v -u*scale;
  char  fmt[16];
  sprintf (fmt, "%%ld.%%0%dld", precision);
 
  snprintf (buf, bsize, fmt, u, f);
}

/* first invocation will raise the pin
 */
static void
toggle()
{
  static byte level = 0;  // LOW

  digitalWrite(TIME_PIN, (level = ~level) ? HIGH : LOW);
}

static float
ds18b20_read(void)
{
  if (!ds.reset()) {
    ++rtcMem.failRead;
    return (85.0);
  }
  ds.select(addr);
  ds.write(READ_SCRATCHPAD);

  byte i, data[9];
  for (i = 0; i < 9; ++i)
    data[i] = ds.read();

  // if (OneWire::crc8(data, 9)) ERROR...

  int16_t dCi = (data[1] << 8) | data[0];  // 12 bit temp
  return ((float)dCi / 16.0);
}

static void
ds18b20_convert(void)
{
  if (!ds.reset()) return;
  ds.select(addr);
  ds.write(CONVERT_T, 1);
}

static boolean
set_up_wifi(void)
{
  time_wifi = micros();

#ifndef WIFI_USE_DHCP
    WiFi.config(ip, gw, dns);     // set static IP
#endif

#ifdef WIFI_SSID
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif

  return true;
}

static boolean
wait_for_wifi(void)
{
  int i = WIFI_TIMEOUT_MS;
  byte wstatus;
  byte old_wstatus = 100;
  Serial.print(WL_CONNECTED);
  Serial.print(": ");
  while ((wstatus = WiFi.status()) != WL_CONNECTED) {
    if (old_wstatus != wstatus) {
      Serial.print(wstatus);
      old_wstatus = wstatus;
    }
    toggle();
    delay(WIFI_WAIT_MS);
    if ((i -= WIFI_WAIT_MS) <= 0) {
      Serial.println(" no WiFi");
      ++rtcMem.failHard;
      return false;
    }
  }
  time_wifi = micros() - time_wifi;
  digitalWrite(TIME_PIN, LOW);
  Serial.println(wstatus);

#ifdef SERIAL_CHATTY
  Serial.print(" have WiFi in ");
  Serial.print(time_wifi);
  Serial.print("us, ip=");
  Serial.println(WiFi.localIP());
#endif

  return true;
}

static boolean
send_udp(char *message)
{
  UDP.beginPacket(WIFI_SERVER, WIFI_PORT);
  UDP.write(message);
  UDP.endPacket();
  time_udp_bug = millis() + UDP_DELAY_MS;

  return true;
}

static boolean
format_message(char *buf, unsigned int bsize)
{
  char lasts[16];
  show_frac (lasts, sizeof(lasts), 3, rtcMem.lastTime/1000);

  char totals[16];
  show_frac (totals, sizeof(totals), 3, rtcMem.totalTime);

  char starts[16];
  show_frac (starts, sizeof(starts), 3, time_start/1000);

  char reads[16];
  show_frac (reads, sizeof(reads), 3, time_read/1000);

  char wifis[16];
  show_frac (wifis, sizeof(wifis), 3, time_wifi/1000);

  char dCs[16];
  show_frac (dCs, sizeof(dCs), 4, (long)(dCf*10000));

  unsigned long time_now = micros();
  char nows[16];
  show_frac (nows, sizeof(nows), 3, (time_now-time_start)/1000);

  snprintf (buf, bsize,
      "show %s %lu times=L%s,T%s,s%s,u0.000,r%s,w%s,t%s stats=fs%lu,fh%lu,fr%lu %s",
      HOSTNAME, rtcMem.runCount, lasts, totals, starts, reads, wifis, nows,
      rtcMem.failSoft, rtcMem.failHard, rtcMem.failRead,
      dCs);

  return true;
}

static boolean
send_message(char *message)
{
  if (!send_udp(message))
    return false;

  return true;
}

static boolean
read_temp(void)
{
  time_read = micros();
  dCf = ds18b20_read();       // read old conversion
  ds18b20_convert();          // start next conversion
  time_read = micros()- time_read;

#ifdef SERIAL_CHATTY
  Serial.print("\nTemperature = ");
  Serial.print(dCf);
  Serial.println("dC");
#endif
  return true;
}

static void
do_stuff()
{
  char message[100];

  if (!set_up_wifi())
    return;

  if (!read_temp()) // read while wifi comes up
    return;

  if (!wait_for_wifi())
    return;

  if (!format_message(message, sizeof(message)))
    return;

  if (!send_message(message))
    return;
}

/////////////////////////// RTC memory /////////////////
static bool
rtc_read(void)
{
  uint32_t mem;

  system_rtc_mem_read (64, &rtcMem, sizeof(rtcMem));
  return (mem);
}

static bool
rtc_write(void)
{
  system_rtc_mem_write (64, &rtcMem, sizeof(rtcMem));
}

static bool
rtc_init(void)
{
  rtc_read ();
  if (RTC_magic != rtcMem.magic) {
    rtcMem.magic     = RTC_magic;
    rtcMem.runCount  = 0;
    rtcMem.failSoft  = 0;
    rtcMem.failHard  = 0;
    rtcMem.failRead  = 0;
    rtcMem.lastTime  = 0;
    rtcMem.totalTime = 0;
    rtc_write ();
} else
    rtc_read();

  ++rtcMem.runCount;

  return true;
}

static void
rtc_commit(void)
{
  rtcMem.lastTime = micros();
  rtcMem.totalTime += rtcMem.lastTime/1000;
  rtc_write();
}

/////////////////////// main program /////////////////

void
setup() {
  time_start = micros();

  rtc_init();

  /* after power up the pin is HIGH
   */
  pinMode(TIME_PIN, OUTPUT);
  digitalWrite(TIME_PIN, LOW);    // start marker

  Serial.begin(SERIAL_BAUD);
  Serial.println("");

#ifdef SERIAL_CHATTY
  Serial.print("start at ");
  Serial.print(time_start);
  Serial.println("us");
#endif

  do_stuff();   // put actual work there

  digitalWrite(TIME_PIN, HIGH);   // end marker
  delay(1);
  digitalWrite(TIME_PIN, LOW);

#ifdef SERIAL_CHATTY
  unsigned long time_now = micros();
  Serial.print("sleeping ");
  Serial.print(SLEEP_MS);
  Serial.print("ms at ");
  Serial.print(time_now);
  Serial.print("(+");
  Serial.print(time_now-time_start);
  Serial.println(")us");
#endif

  rtc_commit();

  uint32_t now = millis();
  if (now < time_udp_bug)
    delay (time_udp_bug - now);

// WAKE_RF_DEFAULT, WAKE_RFCAL, WAKE_NO_RFCAL, WAKE_RF_DISABLED
  ESP.deepSleep(SLEEP_MS*1000, WAKE_RFCAL);
}

void
loop() {
  // sleeping so wont get here
}
