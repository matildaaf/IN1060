#include <Encoder.h>
#include <Adafruit_NeoPixel.h>
#include <math.h> // For ceil()

// Oppsett av LED-stripe
const int NUM_LEDS = 27;  // Vi bruker 27 LED
const int LED_PIN = 6;    // Pinne for LED-stripen
const int BRIGHTNESS = 250; // Sterk lysstyrke

// Oppsett av roterende encoder
const int ENCODER_PIN_1 = 2;  // Encoder pinne 1
const int ENCODER_PIN_2 = 3;  // Encoder pinne 2
const int ENCODER_BUTTON = 8; // Enkoderens trykknapp

// Definerer tre tilstander for systemet
const int MODE_VALG = 0;   // Velg fokustid
const int MODE_FOKUS = 1;   // Fokusmodus
const int MODE_PAUSE = 2;   // Pausemodus

// Initialiserer NeoPixel og enkoder
Adafruit_NeoPixel led(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
Encoder encoder(ENCODER_PIN_1, ENCODER_PIN_2);

// Globale variabler
int currentMode = MODE_VALG; // Holder styr på gjeldene modus
int ledIndex = 0;  // Antall LED som er tent
const long TIME_PER_LED = 133333; // Tid per LED (60 min / 27 led ≈ 133333 ms)
const long DEFAULT_FOCUS_TIME = 2700000; // 45 min
const long DEFAULT_PAUSE_TIME = 900000;  // 15 min
const int MAX_FOCUS_LEDS = 20; // Maks 45 min fokustid (2700000 / 133333 ≈ 20 LED-er)
long focusTime = 0; // tid i fokusmodus
long pauseTime = 0; // tid i pausemodus
long startTime = 0; // tidspunkt når et modus starter
long lastButtonPress = 0; // håndterer debouncing av knappen
const long DEBOUNCE_DELAY = 250; // håndterer debouncing av knappen
int selectedLeds = 0; // Antall valgt LEDer for fokustid
bool buttonPressed = false; // Spor om knappen er trykket
const long BLINK_INTERVAL = 500; // 500 ms på/av for blink i valgmodus
long lastBlinkTime = 0; // Tidspunkt for siste blink
bool blinkState = false; // På eller av for blinking
const long PAUSE_FULL_LIGHT_DURATION = 5000; // 5 sekunder for full sirkel med lys i pausemodus
bool isFullLight = false; // Spor om vi er i full sirkel-modus

// Setup
void setup() {
  Serial.begin(9600);
  pinMode(ENCODER_BUTTON, INPUT_PULLUP);
  led.begin();
  led.setBrightness(BRIGHTNESS);
  led.clear();
  led.show();
  lastBlinkTime = millis(); // Initialiser blinketid
}

// Slukker alle LEDer og oppdaterer ledstripen
void initialiserSlukket() {
  led.clear();
  led.show();
  delay(50); // 50ms forsinkelse for stabilitet
}

// Oppdater LEDstripen basert på gjeldene modus
void updateLEDs() {
  led.clear();
  if (currentMode == MODE_VALG) { // valgmodus
    if (ledIndex == 0) {
      // én LED blinker når ingen tid er valgt
      if (blinkState) {
        led.setPixelColor(0, led.Color(0, 50, 0)); // Grønn for blink
      }
    } else {
      int pauseLeds = ceil(ledIndex / 3.0); // Rund opp for å vise minst én rød pauseLED
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i < ledIndex) {
          led.setPixelColor(i, led.Color(0, 50, 0)); // Grønn for fokus
        } else if (i < ledIndex + pauseLeds && i < NUM_LEDS) {
          led.setPixelColor(i, led.Color(50, 0, 0)); // Rød for pause
        } else {
          led.setPixelColor(i, led.Color(0, 0, 0)); // Slukket
        }
      }
    }
  } else if (currentMode == MODE_PAUSE && isFullLight) {
    // Lys opp hele sirkelen rødt i pausemodus
    for (int i = 0; i < NUM_LEDS; i++) {
      led.setPixelColor(i, led.Color(50, 0, 0)); // Rød for pause
    }
  } else {
    for (int i = 0; i < NUM_LEDS; i++) {
      if (i < ledIndex) {
        led.setPixelColor(i, currentMode == MODE_PAUSE ? led.Color(50, 0, 0) : led.Color(0, 50, 0));
      } else {
        led.setPixelColor(i, led.Color(0, 0, 0));
      }
    }
  }
  led.show();
  delay(50);
}

// Blink alle LED-ene én gang for å bekrefte start på fokusøkt
void confirmBlink() {
  led.clear();
  for (int i = 0; i < NUM_LEDS; i++) {
    led.setPixelColor(i, led.Color(0, 50, 0)); // Grønn farge
  }
  led.show();
  delay(200); // På i 200 ms
  led.show();
  delay(200); // Av i 200 ms
}

// Start fokusmodus når encoderknappen er trykket i valgmodus
void startFokusMode() {
  if (ledIndex == 0) { // ingen LED er valgt og da starter default tid 45/15
    focusTime = DEFAULT_FOCUS_TIME;
    pauseTime = DEFAULT_PAUSE_TIME;
    selectedLeds = round(DEFAULT_FOCUS_TIME / TIME_PER_LED);
  } else { // setter valgt tid
    selectedLeds = ledIndex; // antall led som er valgt
    focusTime = selectedLeds * TIME_PER_LED; // antall led * tiden på hver led
    pauseTime = focusTime / 3; // pausetiden er den valgte fokustiden delt på 3
  }
  if (selectedLeds < 1) { // passer på at minst en LED er valgt
    selectedLeds = 1;
    Serial.println("Advarsel: selectedLeds = 0, satt til 1");
  }
  ledIndex = selectedLeds;
  confirmBlink(); // Blink alle LED-ene for bekreftelse
  startTime = millis();
  currentMode = MODE_FOKUS;
  updateLEDs(); // oppdaterer LEDene basert på det valgte fokusmoduset
  // Print for å kontrollere at alt funker som det skal
  Serial.print("Fokusmodus startet: ");
  Serial.print(focusTime / 60000);
  Serial.println(" min");
  Serial.print("Pausetid: ");
  Serial.print(pauseTime / 60000);
  Serial.println(" min");
  Serial.print("LED-er: ");
  Serial.println(selectedLeds);
  Serial.print("Tid per LED: ");
  Serial.println(focusTime / selectedLeds / 1000);
}

// Håndterer nedtelling i fokusmodus
void haandtereFokus() {
  long elapsedTime = millis() - startTime; // kalkulerer tiden
  if (elapsedTime < 0) { // resetter startTime hvis elapsedTime er negativ
    startTime = millis();
    elapsedTime = 0;
    Serial.println("Advarsel: elapsedTime negativ");
  }
  long timePerLed = focusTime / selectedLeds; 
  int ledsToTurnOff = elapsedTime / timePerLed; // regner ut hvor mange LED som skal slukkes
  int newLedIndex = selectedLeds - ledsToTurnOff; 
  
  if (newLedIndex < 0) {
    newLedIndex = 0;
  }
  
  if (newLedIndex != ledIndex) {
    ledIndex = newLedIndex;
    updateLEDs();
    Serial.print("Fokus LED-er igjen: ");
    Serial.print(ledIndex);
    Serial.print(", Tid: ");
    Serial.print(elapsedTime / 1000);
    Serial.println(" sek");
  }
  
  if (elapsedTime >= focusTime && ledIndex == 0) { // fokusøkt er ferdig
    currentMode = MODE_PAUSE; // pausemodus starter
    ledIndex = selectedLeds; // Start med valgte LED-er
    startTime = millis();
    isFullLight = true; // Aktiver full sirkel i 5 sekunder for å tydliggjøre pausen
    led.clear();
    led.show();
    delay(50);
    updateLEDs(); 
    Serial.print("Pausemodus: ");
    Serial.print(pauseTime / 60000);
    Serial.println(" min");
    Serial.println("Hele sirkelen lyser rødt i 5 sekunder");
  }
}

// Håndtere nedtelling i pausemodus
void haandterePause() {
  long elapsedTime = millis() - startTime;
  if (elapsedTime < 0) {
    startTime = millis();
    elapsedTime = 0;
    isFullLight = true; // Resett full sirkel-modus
    Serial.println("Advarsel: elapsedTime negativ");
  }

  // Lys opp hele sirkelen i 5 sekunder
  if (isFullLight && elapsedTime < PAUSE_FULL_LIGHT_DURATION) {
    updateLEDs();
    return; // Fortsett å vise full sirkel
  } else if (isFullLight && elapsedTime >= PAUSE_FULL_LIGHT_DURATION) {
    isFullLight = false; // Deaktiver full sirkel-modus
    led.clear();
    led.show();
    delay(50);
    updateLEDs();
    Serial.println("Full sirkel ferdig, tilbake til normal pausemodus");
  }

  // Normal pausemodus
  if (ledIndex == selectedLeds && elapsedTime < (PAUSE_FULL_LIGHT_DURATION + 100)) {
    led.clear();
    led.show();
    delay(50);
    updateLEDs();
  }

  long timePerLed = pauseTime / selectedLeds;
  int ledsToTurnOff = elapsedTime / timePerLed;
  int newLedIndex = selectedLeds - ledsToTurnOff;
  
  if (newLedIndex < 0) {
    newLedIndex = 0;
  }
  
  if (newLedIndex != ledIndex) {
    ledIndex = newLedIndex;
    updateLEDs();
    Serial.print("Pause LED-er igjen: ");
    Serial.print(ledIndex);
    Serial.print(", Tid: ");
    Serial.print(elapsedTime / 1000);
    Serial.println(" sek");
  }
  
  // pausemodus er over
  if (elapsedTime >= pauseTime) {
    currentMode = MODE_VALG; // tilbake til valgmodus
    ledIndex = 0;
    selectedLeds = 0;
    encoder.write(0);
    updateLEDs();
    lastBlinkTime = millis(); // Resett blinketid for ny valgmodus
    Serial.println("Tilbake til valgmodus");
  }
}

// Velg fokustid i valgmodus
void valgAvFokustid() {
  long knobValue = encoder.read(); // leser encoderens verdi
  int newLedIndex = constrain(knobValue / 4, 0, MAX_FOCUS_LEDS); // Maks 45 min
  
  if (newLedIndex != ledIndex) {
    ledIndex = newLedIndex;
    int pauseLeds = ceil(ledIndex / 3.0); // Rund opp for å vise minst én rød LED
    updateLEDs();
    Serial.print("Valg fokustid: ");
    Serial.print((ledIndex * TIME_PER_LED) / 60000);
    Serial.println(" min");
    Serial.print("Pausetid: ");
    Serial.print((ledIndex * TIME_PER_LED / 3) / 60000);
    Serial.println(" min");
    Serial.print("LED-er (fokus/pause): ");
    Serial.print(ledIndex);
    Serial.print("/");
    Serial.println(pauseLeds);
  } else if (ledIndex == 0) {
    // Blink én LED når ingen tid er valgt
    long currentTime = millis();
    if (currentTime - lastBlinkTime >= BLINK_INTERVAL) {
      blinkState = !blinkState;
      lastBlinkTime = currentTime;
      updateLEDs();
    }
  }
}

// Loop som kjører konstant
void loop() {
  bool currentButtonState = digitalRead(ENCODER_BUTTON) == LOW;
  if (currentButtonState && !buttonPressed && (millis() - lastButtonPress) > DEBOUNCE_DELAY) { // sjekker om knappen er trykket ned riktig
    lastButtonPress = millis();
    buttonPressed = true; // knappen er trykket
    if (currentMode == MODE_VALG) { 
      startFokusMode(); // nå kan man velge tid til fokusøkten
    } else if (currentMode == MODE_FOKUS) {
      currentMode = MODE_VALG; // resetter tiden og går nå tilbake til valgmodus
      ledIndex = 0;
      selectedLeds = 0;
      encoder.write(0);
      initialiserSlukket();
      lastBlinkTime = millis(); // Resett blinketid
      Serial.println("Tilbake til valg");
    }
  }
  if (!currentButtonState) { // sjekker om knappen ikke lengre er trykket
    buttonPressed = false;
  }
  
  switch (currentMode) {
    case MODE_VALG:
      valgAvFokustid();
      break;
    case MODE_FOKUS:
      haandtereFokus();
      break;
    case MODE_PAUSE:
      haandterePause();
      break;
  }
}