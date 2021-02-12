
/*
   GliderScore portable Driver Chrono Soft
   Initial release 1.01 June 28th, 2019
   Release 1.0 Initial release

   Based on O.Segouin wireless big display for GliderScore

   Because we had some conflicts on the SPI bus when both the display and the nRF24L01 module were connected
   on the same bus, we had to use a soft SPI for the 5110 display.

   ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   This is a beerware; if you like it and if we meet some day, you can pay us a beer in return!
   ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/
//--------------------------------------------------------------------------------------------------------------------------------------
#include "RF24.h"
#include <U8g2lib.h>      // Oled U8g2 library          from https://github.com/olikraus/U8g2_Arduino/archive/master.zip
#include <ClickEncoder.h> // from https://github.com/0xPIT/encoder
#include <TimerOne.h>     // from https://github.com/PaulStoffregen/TimerOne
#include <Arduino.h>
//--------------------------------------------------------------------------------------------------------------------------------------
int compt = 0;
String chainerecu = "";
String chainecourante = "";
char chaineradio[] = "G01R01T0230WT";
String act = "";
String manche = "00";
String groupe = "00";
char chrono[32] = "";
char txt[50];
volatile unsigned long debut;
volatile byte marche = false;
volatile unsigned long le_temps = 0, le_temps1 = 0;
unsigned long minutes;
unsigned long secondes;
unsigned long le_temps_deb ;
unsigned long tempo = 0;
int idx = 0;
boolean flip_flop = false;
String temps;
char aff[] = "x+*x";
// Variables and constants
const int RADIO_SS_PIN  = 8;
const int RADIO_CSN_PIN = 9;
RF24 radio(RADIO_SS_PIN, RADIO_CSN_PIN); // CE, CSN
const byte address[6] = "00001";
int bp = 2;
int raz = 14;
int voltagePin = A4;
char sum;
float a_mini = 6.4;   //minimum battery voltage for display 2s
float a_maxi = 8.4;   //maximum battery voltage for display 2s
float tension;
float iTension = 0;
float ax, bx;
bool bat_2s = true; // 2S battery used then TRUE, if 1s used should be FALSE

ClickEncoder *encoder;
int16_t last[] = { -1, -1, -1, -1};
int16_t value[] = {0, 10, 0, 0};
int16_t max_value[] = {1, 16, 59, 0};
int16_t min_value[] = {0, 0, 0, 0};
int champs = 0;

void timerIsr() {
  encoder->service();
}

//--------------------------------------------------------------------------------------------------------------------------------------
U8G2_PCD8544_84X48_F_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ 7, /* data=*/ 6, /* cs=*/ 4, /* dc=*/ 3, /* reset=*/ 5);  // Nokia 5110 Display
//--------------------------------------------------------------------------------------------------------------------------------------
String lead_zero(int num) {
  String t = "";
  if (num < 10) t = "0";
  return t + String(num);
}
//--------------------------------------------------------------------------------------------------------------------------------------
ISR(TIMER2_COMPA_vect) { //timer2 interrupt 2kHz
  if (marche) {
    if (le_temps > 0) {
      le_temps1 = le_temps1 - 1;
      le_temps = le_temps1 / 2;
    }
  }
}
//--------------------------------------------------------------------------------------------------------------------------------------
void isr1(void) {
  detachInterrupt(digitalPinToInterrupt(bp));
  if (!marche) {
    marche = true;
  } else marche = false;
  delay(50);
  attachInterrupt(digitalPinToInterrupt(bp), isr1, FALLING);
}
//--------------------------------------------------------------------------------------------------------------------------------------
void setup(void) {
  Serial.begin(9600);
  if (!bat_2s) {
    bx = 1744 / 81;
    ax = (4 - bx) / 710;
    a_mini = 3.9;   //minimum battery voltage for display 1s
    a_maxi = 4.2;   //maximum battery voltage for display 1s
  } else {
    bx = ((6.48 * 855) - 8.4 * 659) / 658;
    ax = (8.4 - bx) / 855;
    a_mini = 6.2;   //minimum battery voltage for display 2s
    a_maxi = 8.4;   //maximum battery voltage for display 2s
  }
  le_temps_deb = 600000;
  le_temps = le_temps_deb; le_temps1 = le_temps * 2;

  cli();//stop interrupts
  TCCR2A = 0;// set entire TCCR0A register to 0
  TCCR2B = 0;// same for TCCR0B
  TCNT2  = 0;//initialize counter value to 0
  // set compare match register for 2khz increments
  OCR2A = 249;// = (16*10^6) / (400*64) - 1 (must be <256)
  // turn on CTC mode
  TCCR2A |= (1 << WGM01);
  // Set CS01 and CS00 bits for 64 prescaler
  TCCR2B |= (1 << CS01) | (1 << CS00);
  // enable timer compare interrupt
  TIMSK2 |= (1 << OCIE0A);
  sei();//allow interrupts

  radio.begin();
  radio.openWritingPipe(address);
  radio.setChannel(1);// 1 2.401 Ghz
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_1MBPS);
  //radio.setDataRate(RF24_250KBPS);
  radio.setAutoAck(1);                     // Ensure autoACK is enabled
  radio.setRetries(2, 15);                 // Optionally, increase the delay between retries & # of retries
  radio.setCRCLength(RF24_CRC_8);          // Use 8-bit CRC for performance
  radio.stopListening();

  u8g2.begin();

  pinMode(bp, INPUT);
  digitalWrite(bp, HIGH); //Pullup
  pinMode(raz, INPUT);
  digitalWrite(raz, HIGH); //Pullup
  attachInterrupt(digitalPinToInterrupt(bp), isr1, FALLING);

  encoder = new ClickEncoder(A2, A1, A3);
  encoder->setAccelerationEnabled(true);// use acceleration for faster change

  Timer1.initialize(1000);
  Timer1.attachInterrupt(timerIsr);


}
//--------------------------------------------------------------------------------------------------------------------------------------
String cnv_temps(unsigned long t) {
  unsigned long  ce, se, mi;
  double tt;
  String l_temps;
  tt = t;
  mi = t / 60000;
  t = t - (mi * 60000);
  se = t / 1000;
  t = t - (1000 * se);
  ce = t / 10;
  l_temps = lead_zero(mi) + ":" + lead_zero(se) + ":" + lead_zero(ce);
  return l_temps;
}
//--------------------------------------------------------------------------------------------------------------------------------------
void Affiche_ecran(void) {
  u8g2.clearBuffer();
  u8g2.setDrawColor(2); /* color 2 for the text */
  u8g2.setFontMode(1);  /* activate transparent font mode */

  u8g2.drawFrame(0, 0, 41, 14); //RND Frame
  u8g2.drawFrame(43, 0, 41, 14); //GRP Frame
  u8g2.drawFrame(0, 16, 83, 14); //Working time Frame
  u8g2.drawFrame(0, 32, 83, 14); //Flight time Frame

  u8g2.setFont(u8g2_font_7x14_tf ); //u8g2_font_8x13B_mn );
  u8g2.setCursor(2, 1 + 1 * 11);
  u8g2.print("RND");
  u8g2.setFont(u8g2_font_7x14B_tf); //u8g2_font_8x13B_mn );
  u8g2.setCursor(31 - 6, 1 + 1 * 11);
  u8g2.print(manche);

  u8g2.setFont(u8g2_font_7x14_tf ); //u8g2_font_8x13B_mn );
  u8g2.setCursor(45, 1 + 1 * 11);
  u8g2.print("GRP");
  u8g2.setFont(u8g2_font_7x14B_tf); //u8g2_font_8x13B_mn );
  u8g2.setCursor(74 - 6, 1 + 1 * 11);
  u8g2.print(groupe);

  u8g2.setCursor(2 + 11, 17 + 11);
  u8g2.setFont(u8g2_font_7x14_tf);
  u8g2.print(act);

  u8g2.setDrawColor(1);
  iTension = analogRead(voltagePin);
  tension = (iTension * ax) + bx;
  //    Serial.println (tension);
  if (tension > a_maxi) tension = a_maxi;
  if (tension < a_mini) tension = a_mini;

  u8g2.drawFrame(4, 28 - 10, 5, 3);
  u8g2.drawFrame(3, 28 - 8, 7, 8);
  int pile  = ((tension - a_mini) / (a_maxi - a_mini)) * 8; //compute battery level
  u8g2.drawBox(3, 28 - pile, 7, pile);

  if (champs == 1) {
    u8g2.setDrawColor(1); /* color 1 for the box */
    u8g2.drawBox(34, 17, 17, 12);
    u8g2.setDrawColor(2); /* color 2 for the text */
  }
  if (champs == 2) {
    u8g2.setDrawColor(1); /* color 1 for the box */
    u8g2.drawBox(58, 17, 17, 12);
    u8g2.setDrawColor(2); /* color 2 for the text */
  }
  u8g2.setFont(u8g2_font_8x13B_mn );
  u8g2.setCursor(35, 17 + 11);
  u8g2.print(cnv_temps(le_temps_deb).substring(0, 5));

  if (marche) {
    u8g2.setCursor(5, 33 + 11);
    u8g2.print(aff[idx]);
  }

  u8g2.setCursor(35, 33 + 11);
  u8g2.print(temps.substring(0, 5));
  u8g2.sendBuffer();          // transfer internal memory to the display
}
//--------------------------------------------------------------------------------------------------------------------------------------
void l_encodeur() {
  value[champs] += encoder->getValue();
  if (value[champs] > max_value[champs]) value[champs] = min_value[champs];
  if (value[champs] < min_value[champs]) value[champs] = max_value[champs];

  if (value[champs] != last[champs]) {
    last[champs] = value[champs];
    //Serial.println("Encoder Value: " + String(value[1]) + " - " + String(value[2]));
  }

  ClickEncoder::Button b = encoder->getButton();
  if (b == 5) { //Click sur bouton codeur
    //Serial.println("champs suivant");
    champs += 1;
    if (champs > 2) champs = 1;
  }
}

//--------------------------------------------------------------------------------------------------------------------------------------
String checksum(String chaine)
{
  sum = 0;
  for (byte i = 0; i < (chaine.length() - 2); i++)
  {
    sum = chaine[i] + sum;
  }
  sum = (sum & 0x3F) + 0x20;
  return (chaine + sum);
}
//--------------------------------------------------------------------------------------------------------------------------------------
void loop(void) {
  if (!marche) {
    act = "ST";
    l_encodeur();
    minutes = value[1] * 60000L; // conversion en milisecondes
    secondes = value[2] * 1000L;
    le_temps_deb = minutes + secondes;
    if (!digitalRead(raz)) {
      le_temps = le_temps_deb; le_temps1 = le_temps * 2;
      champs = 0;
    }
  } else {
    if ((millis() - tempo) >= 200) {
      idx += 1;
      if (idx > 4) idx = 0;
      tempo = millis();
    }
    act = "WT";
  }

  temps = cnv_temps(le_temps);
  chainerecu = "R00G00T" + temps.substring(0, 2) + temps.substring(3, 5) + act;
  chainerecu = checksum(chainerecu);
  chainerecu.toCharArray(chaineradio, chainerecu.length() + 1); // récupère la chaine recu dans le tableau de char de chaine radio
  radio.write(&chaineradio, sizeof(chaineradio)); // envoi de la chaine recu RS en radio
  //Serial.println ("Envoiradio:" + chainerecu);

  Affiche_ecran();

  delay(50);
}
