/*
╔═══════════════════════════════════════════════════════════════════════════╗
║                                                                           ║
║   ███╗   ██╗███████╗ ██████╗       ██████╗  ██████╗██████╗                ║
║   ████╗  ██║██╔════╝██╔═══██╗      ██╔══██╗██╔════╝██╔══██╗               ║
║   ██╔██╗ ██║█████╗  ██║   ██║█████╗██████╔╝██║     ██████╔╝               ║
║   ██║╚██╗██║██╔══╝  ██║   ██║╚════╝██╔═══╝ ██║     ██╔══██╗               ║
║   ██║ ╚████║███████╗╚██████╔╝      ██║     ╚██████╗██║  ██║               ║
║   ╚═╝  ╚═══╝╚══════╝ ╚═════╝       ╚═╝      ╚═════╝╚═╝  ╚═╝               ║
║                                                                           ║
║              🧬 Neo-PCR - Thermocycleur DIY v1.0 🧬                       ║
║                                                                           ║
║  Développé avec passion pour la communauté biohacking                     ║
║  Avec l'aide de Claude (Anthropic)                                        ║
║                                                                           ║
║  GNU General Public License v3.0                                          ║
║  Copyright (C) 2026                                                       ║
║                                                                           ║
╚═══════════════════════════════════════════════════════════════════════════╝
*/

// ═══════════════════════════════════════════════════════════════════════════
// INCLUDES & CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#include <Adafruit_MAX31855.h>

// Configuration des pins
#define THERMO_DO   4     // MAX31855 Data Out
#define THERMO_CS   5     // MAX31855 Chip Select  
#define THERMO_CLK  6     // MAX31855 Clock
#define RELAY_PIN   7     // Relais SSR (chauffage)
#define FAN_PIN     9     // Ventilateur (PWM)
#define STATUS_LED  13    // LED de statut

// Températures PCR (en °C)
#define TEMP_DENATURATION  94.0   // Dénaturation ADN
#define TEMP_ANNEALING     60.0   // Hybridation amorces
#define TEMP_EXTENSION     72.0   // Élongation
#define TEMP_INIT_DENAT    95.0   // Dénaturation initiale
#define TEMP_FINAL_EXT     72.0   // Extension finale
#define TEMP_HOLD          4.0    // Conservation (simulation)

// Durées des phases (en secondes)
#define TIME_INIT_DENAT    180    // 3 minutes dénaturation initiale
#define TIME_DENATURATION  30     // 30 secondes
#define TIME_ANNEALING     30     // 30 secondes
#define TIME_EXTENSION     30     // 30 secondes (1 min/kb recommandé)
#define TIME_FINAL_EXT     600    // 10 minutes extension finale
#define NUM_CYCLES         30     // Nombre de cycles PCR

// Paramètres de contrôle
#define TEMP_TOLERANCE     0.5    // Tolérance température (±0.5°C)
#define TEMP_MAX_SAFE      100.0  // Température max sécurité
#define TEMP_OVERSHOOT     2.0    // Anticipation surchauffe
#define HEATING_PULSE_MS   500    // Durée pulse chauffage (ms)
#define TEMP_CHECK_INTERVAL 1000  // Intervalle vérif temp (ms)
#define MAX_HEATING_TIME   15000  // Timeout chauffage (15s)

// Paramètres PID (à ajuster selon votre matériel)
#define PID_KP  50.0     // Proportionnel
#define PID_KI  0.5      // Intégral
#define PID_KD  10.0     // Dérivé

// ═══════════════════════════════════════════════════════════════════════════
// VARIABLES GLOBALES
// ═══════════════════════════════════════════════════════════════════════════

// Objet thermocouple
Adafruit_MAX31855 thermocouple(THERMO_CLK, THERMO_CS, THERMO_DO);

// État du système
enum SystemState {
  STATE_IDLE,           // En attente
  STATE_INIT_DENAT,     // Dénaturation initiale
  STATE_CYCLE_DENAT,    // Dénaturation (cycle)
  STATE_CYCLE_ANNEAL,   // Hybridation (cycle)
  STATE_CYCLE_EXTEND,   // Extension (cycle)
  STATE_FINAL_EXT,      // Extension finale
  STATE_HOLD,           // Conservation
  STATE_COMPLETE,       // Terminé
  STATE_ERROR           // Erreur
};

SystemState currentState = STATE_IDLE;
int currentCycle = 0;
unsigned long phaseStartTime = 0;
unsigned long lastTempCheck = 0;

// Variables PID
float pidIntegral = 0;
float pidLastError = 0;
unsigned long pidLastTime = 0;

// Statistiques
unsigned long totalRunTime = 0;
unsigned long cycleStartTime = 0;
int heatingPulses = 0;
int coolingCycles = 0;

// Flags
bool systemRunning = false;
bool emergencyStop = false;

// ═══════════════════════════════════════════════════════════════════════════
// SETUP - INITIALISATION
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
  // Initialisation série
  Serial.begin(9600);
  delay(500);
  
  // Configuration des pins
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  
  // État initial sécurisé
  digitalWrite(RELAY_PIN, LOW);   // Chauffage OFF
  analogWrite(FAN_PIN, 0);        // Ventilateur OFF
  digitalWrite(STATUS_LED, LOW);
  
  // Attendre stabilisation thermocouple
  delay(1000);
  
  // Afficher splash screen
  printSplashScreen();
  
  // Vérifier thermocouple
  if (!checkThermocouple()) {
    currentState = STATE_ERROR;
    Serial.println(F("❌ ERREUR: Thermocouple non détecté!"));
    Serial.println(F("   Vérifiez les connexions et redémarrez."));
    blinkError();
  } else {
    Serial.println(F("✅ Thermocouple OK"));
    Serial.println(F("✅ Système initialisé"));
    printHelp();
  }
  
  Serial.println();
}

// ═══════════════════════════════════════════════════════════════════════════
// LOOP PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
  // Vérifier commandes série
  if (Serial.available() > 0) {
    handleSerialCommand();
  }
  
  // Gestion des états
  if (currentState != STATE_IDLE && currentState != STATE_ERROR && !emergencyStop) {
    runPCRStateMachine();
  }
  
  // Clignotement LED selon état
  updateStatusLED();
  
  // Petit délai pour stabilité
  delay(100);
}

// ═══════════════════════════════════════════════════════════════════════════
// MACHINE À ÉTATS PCR
// ═══════════════════════════════════════════════════════════════════════════

void runPCRStateMachine() {
  float currentTemp = readTemperature();
  
  // Vérification sécurité
  if (currentTemp > TEMP_MAX_SAFE) {
    emergencyShutdown("Température trop élevée!");
    return;
  }
  
  if (isnan(currentTemp)) {
    emergencyShutdown("Perte signal thermocouple!");
    return;
  }
  
  // Machine à états
  switch (currentState) {
    
    case STATE_INIT_DENAT:
      if (phaseStartTime == 0) {
        Serial.println(F("\n▶ PHASE: Dénaturation initiale"));
        Serial.print(F("   Cible: ")); Serial.print(TEMP_INIT_DENAT); Serial.println(F("°C"));
        phaseStartTime = millis();
        cycleStartTime = millis();
      }
      
      if (maintainTemperature(TEMP_INIT_DENAT, currentTemp)) {
        if (millis() - phaseStartTime >= TIME_INIT_DENAT * 1000) {
          Serial.println(F("✓ Dénaturation initiale complète\n"));
          currentState = STATE_CYCLE_DENAT;
          currentCycle = 1;
          phaseStartTime = 0;
        }
      }
      break;
      
    case STATE_CYCLE_DENAT:
      if (phaseStartTime == 0) {
        Serial.print(F("\n═══ CYCLE ")); Serial.print(currentCycle); 
        Serial.print(F(" / ")); Serial.print(NUM_CYCLES); Serial.println(F(" ═══"));
        Serial.println(F("▶ Dénaturation"));
        phaseStartTime = millis();
      }
      
      if (maintainTemperature(TEMP_DENATURATION, currentTemp)) {
        if (millis() - phaseStartTime >= TIME_DENATURATION * 1000) {
          Serial.println(F("✓ Dénaturation OK"));
          currentState = STATE_CYCLE_ANNEAL;
          phaseStartTime = 0;
        }
      }
      break;
      
    case STATE_CYCLE_ANNEAL:
      if (phaseStartTime == 0) {
        Serial.println(F("▶ Hybridation"));
        phaseStartTime = millis();
      }
      
      if (maintainTemperature(TEMP_ANNEALING, currentTemp)) {
        if (millis() - phaseStartTime >= TIME_ANNEALING * 1000) {
          Serial.println(F("✓ Hybridation OK"));
          currentState = STATE_CYCLE_EXTEND;
          phaseStartTime = 0;
        }
      }
      break;
      
    case STATE_CYCLE_EXTEND:
      if (phaseStartTime == 0) {
        Serial.println(F("▶ Extension"));
        phaseStartTime = millis();
      }
      
      if (maintainTemperature(TEMP_EXTENSION, currentTemp)) {
        if (millis() - phaseStartTime >= TIME_EXTENSION * 1000) {
          Serial.println(F("✓ Extension OK"));
          
          // Cycle suivant ou extension finale
          if (currentCycle < NUM_CYCLES) {
            currentCycle++;
            currentState = STATE_CYCLE_DENAT;
          } else {
            currentState = STATE_FINAL_EXT;
          }
          phaseStartTime = 0;
        }
      }
      break;
      
    case STATE_FINAL_EXT:
      if (phaseStartTime == 0) {
        Serial.println(F("\n▶ PHASE: Extension finale"));
        phaseStartTime = millis();
      }
      
      if (maintainTemperature(TEMP_FINAL_EXT, currentTemp)) {
        if (millis() - phaseStartTime >= TIME_FINAL_EXT * 1000) {
          Serial.println(F("✓ Extension finale complète"));
          currentState = STATE_HOLD;
          phaseStartTime = 0;
        }
      }
      break;
      
    case STATE_HOLD:
      if (phaseStartTime == 0) {
        Serial.println(F("\n▶ PHASE: Conservation"));
        Serial.println(F("   (Refroidissement actif)"));
        phaseStartTime = millis();
      }
      
      // Refroidissement avec ventilateur
      digitalWrite(RELAY_PIN, LOW);
      analogWrite(FAN_PIN, 255);
      
      if (currentTemp <= 25.0) {  // Température ambiante atteinte
        Serial.println(F("\n╔═══════════════════════════════════════╗"));
        Serial.println(F("║   ✅ PCR TERMINÉE AVEC SUCCÈS!       ║"));
        Serial.println(F("╚═══════════════════════════════════════╝"));
        printStatistics();
        currentState = STATE_COMPLETE;
        systemRunning = false;
        analogWrite(FAN_PIN, 0);
      }
      break;
      
    case STATE_COMPLETE:
      // Attendre nouvelle commande
      break;
  }
  
  // Affichage température périodique
  if (millis() - lastTempCheck >= TEMP_CHECK_INTERVAL) {
    Serial.print(F("   Temp: ")); 
    Serial.print(currentTemp, 1);
    Serial.println(F("°C"));
    lastTempCheck = millis();
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// CONTRÔLE DE TEMPÉRATURE
// ═══════════════════════════════════════════════════════════════════════════

bool maintainTemperature(float targetTemp, float currentTemp) {
  float error = targetTemp - currentTemp;
  
  // Température atteinte ?
  if (abs(error) <= TEMP_TOLERANCE) {
    // Maintien avec PID fin
    float pidOutput = calculatePID(error);
    
    if (pidOutput > 0) {
      // Pulse chauffage très court
      digitalWrite(RELAY_PIN, HIGH);
      delay(pidOutput * 10);  // Proportionnel à la sortie PID
      digitalWrite(RELAY_PIN, LOW);
      heatingPulses++;
    }
    
    return true;  // Température stable
  }
  
  // Chauffe ou refroidissement nécessaire
  if (error > 0) {
    // Besoin de chauffer
    heatUp(targetTemp, currentTemp);
  } else {
    // Besoin de refroidir
    coolDown(targetTemp, currentTemp);
  }
  
  return false;  // Température pas encore atteinte
}

void heatUp(float target, float current) {
  float error = target - current;
  
  // Anticipation surchauffe
  if (error <= TEMP_OVERSHOOT) {
    // Approche fine
    digitalWrite(RELAY_PIN, HIGH);
    delay(HEATING_PULSE_MS / 4);  // Pulse court
    digitalWrite(RELAY_PIN, LOW);
  } else {
    // Chauffage plus agressif
    digitalWrite(RELAY_PIN, HIGH);
    delay(HEATING_PULSE_MS);
    digitalWrite(RELAY_PIN, LOW);
  }
  
  heatingPulses++;
  
  // Ventilateur OFF pendant chauffage
  analogWrite(FAN_PIN, 0);
}

void coolDown(float target, float current) {
  float error = current - target;
  
  // Chauffage OFF
  digitalWrite(RELAY_PIN, LOW);
  
  // Ventilateur proportionnel à l'écart
  int fanSpeed = constrain(map(error * 10, 0, 200, 100, 255), 0, 255);
  analogWrite(FAN_PIN, fanSpeed);
  
  coolingCycles++;
}

float calculatePID(float error) {
  unsigned long now = millis();
  float dt = (now - pidLastTime) / 1000.0;  // en secondes
  
  if (dt <= 0) dt = 0.1;
  
  // Calcul PID
  pidIntegral += error * dt;
  pidIntegral = constrain(pidIntegral, -100, 100);  // Anti-windup
  
  float derivative = (error - pidLastError) / dt;
  
  float output = (PID_KP * error) + (PID_KI * pidIntegral) + (PID_KD * derivative);
  
  // Sauvegarder pour prochaine itération
  pidLastError = error;
  pidLastTime = now;
  
  return constrain(output, 0, 100);
}

// ═══════════════════════════════════════════════════════════════════════════
// LECTURE THERMOCOUPLE
// ═══════════════════════════════════════════════════════════════════════════

float readTemperature() {
  double temp = thermocouple.readCelsius();
  
  if (isnan(temp)) {
    Serial.println(F("⚠️  Erreur lecture thermocouple!"));
    return NAN;
  }
  
  return (float)temp;
}

bool checkThermocouple() {
  float temp = readTemperature();
  return !isnan(temp) && temp > 0 && temp < 150;
}

// ═══════════════════════════════════════════════════════════════════════════
// COMMANDES SÉRIE
// ═══════════════════════════════════════════════════════════════════════════

void handleSerialCommand() {
  char cmd = Serial.read();
  
  switch (toupper(cmd)) {
    case 'S':
      if (!systemRunning) {
        startPCR();
      } else {
        Serial.println(F("❌ PCR déjà en cours!"));
      }
      break;
      
    case 'X':
      emergencyShutdown("Arrêt d'urgence utilisateur");
      break;
      
    case 'T':
      printTemperature();
      break;
      
    case 'I':
      printSystemInfo();
      break;
      
    case 'H':
      printHelp();
      break;
      
    default:
      // Ignorer caractères invalides
      break;
  }
}

void startPCR() {
  Serial.println(F("\n╔═══════════════════════════════════════╗"));
  Serial.println(F("║   🧬 DÉMARRAGE PCR NEO-PCR 🧬        ║"));
  Serial.println(F("╚═══════════════════════════════════════╝"));
  
  // Vérifications pré-démarrage
  Serial.print(F("\n🔍 Vérifications... "));
  
  if (!checkThermocouple()) {
    Serial.println(F("\n❌ Thermocouple défaillant!"));
    return;
  }
  
  float temp = readTemperature();
  if (temp > 40) {
    Serial.println(F("\n⚠️  Température initiale trop élevée!"));
    Serial.print(F("   Temp actuelle: ")); Serial.print(temp); Serial.println(F("°C"));
    Serial.println(F("   Laissez refroidir avant de démarrer."));
    return;
  }
  
  Serial.println(F("OK"));
  
  // Afficher paramètres
  Serial.println(F("\n📋 Paramètres du protocole:"));
  Serial.print(F("   Dénaturation: ")); Serial.print(TEMP_DENATURATION); Serial.println(F("°C"));
  Serial.print(F("   Hybridation:  ")); Serial.print(TEMP_ANNEALING); Serial.println(F("°C"));
  Serial.print(F("   Extension:    ")); Serial.print(TEMP_EXTENSION); Serial.println(F("°C"));
  Serial.print(F("   Cycles:       ")); Serial.println(NUM_CYCLES);
  Serial.println();
  
  // Estimation durée totale
  int estimatedTime = (TIME_INIT_DENAT + (TIME_DENATURATION + TIME_ANNEALING + TIME_EXTENSION) * NUM_CYCLES + TIME_FINAL_EXT) / 60;
  Serial.print(F("⏱️  Durée estimée: ")); Serial.print(estimatedTime); Serial.println(F(" minutes"));
  Serial.println();
  
  // Démarrage!
  systemRunning = true;
  emergencyStop = false;
  currentState = STATE_INIT_DENAT;
  currentCycle = 0;
  phaseStartTime = 0;
  totalRunTime = millis();
  heatingPulses = 0;
  coolingCycles = 0;
  
  // Reset PID
  pidIntegral = 0;
  pidLastError = 0;
  pidLastTime = millis();
  
  Serial.println(F("🚀 PCR démarrée!\n"));
}

// ═══════════════════════════════════════════════════════════════════════════
// SÉCURITÉ & ARRÊT D'URGENCE
// ═══════════════════════════════════════════════════════════════════════════

void emergencyShutdown(const char* reason) {
  Serial.println(F("\n\n╔═════════════════════════════════════╗"));
  Serial.println(F("║   ⚠️  ARRÊT D'URGENCE!  ⚠️          ║"));
  Serial.println(F("╚═════════════════════════════════════╝"));
  Serial.print(F("Raison: ")); Serial.println(reason);
  
  // Couper tout
  digitalWrite(RELAY_PIN, LOW);
  analogWrite(FAN_PIN, 255);  // Ventilateur à fond
  
  systemRunning = false;
  emergencyStop = true;
  currentState = STATE_ERROR;
  
  // Clignotement d'erreur
  for (int i = 0; i < 10; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(100);
    digitalWrite(STATUS_LED, LOW);
    delay(100);
  }
  
  Serial.println(F("\n⚠️  Système en sécurité."));
  Serial.println(F("   Vérifiez le matériel avant de redémarrer."));
  Serial.println(F("   Appuyez sur RESET pour réinitialiser.\n"));
}

// ═══════════════════════════════════════════════════════════════════════════
// AFFICHAGE & STATISTIQUES
// ═══════════════════════════════════════════════════════════════════════════

void printSplashScreen() {
  Serial.println(F("\n\n"));
  Serial.println(F("╔═══════════════════════════════════════════════════════════════════════════╗"));
  Serial.println(F("║                                                                           ║"));
  Serial.println(F("║   ███╗   ██╗███████╗ ██████╗       ██████╗  ██████╗██████╗              ║"));
  Serial.println(F("║   ████╗  ██║██╔════╝██╔═══██╗      ██╔══██╗██╔════╝██╔══██╗             ║"));
  Serial.println(F("║   ██╔██╗ ██║█████╗  ██║   ██║█████╗██████╔╝██║     ██████╔╝             ║"));
  Serial.println(F("║   ██║╚██╗██║██╔══╝  ██║   ██║╚════╝██╔═══╝ ██║     ██╔══██╗             ║"));
  Serial.println(F("║   ██║ ╚████║███████╗╚██████╔╝      ██║     ╚██████╗██║  ██║             ║"));
  Serial.println(F("║   ╚═╝  ╚═══╝╚══════╝ ╚═════╝       ╚═╝      ╚═════╝╚═╝  ╚═╝             ║"));
  Serial.println(F("║                                                                           ║"));
  Serial.println(F("║              🧬 Thermocycleur DIY Open Source 🧬                         ║"));
  Serial.println(F("║                                                                           ║"));
  Serial.println(F("║  Version: 1.0                     GNU GPL v3.0                           ║"));
  Serial.println(F("║  Développé avec l'aide de Claude (Anthropic)                            ║"));
  Serial.println(F("║                                                                           ║"));
  Serial.println(F("╚═══════════════════════════════════════════════════════════════════════════╝"));
  Serial.println();
}

void printHelp() {
  Serial.println(F("📖 COMMANDES DISPONIBLES:"));
  Serial.println(F("   S - Démarrer le cycle PCR"));
  Serial.println(F("   X - Arrêt d'urgence"));
  Serial.println(F("   T - Afficher température actuelle"));
  Serial.println(F("   I - Informations système"));
  Serial.println(F("   H - Afficher cette aide"));
  Serial.println();
}

void printTemperature() {
  float temp = readTemperature();
  Serial.print(F("\n🌡️  Température: "));
  if (!isnan(temp)) {
    Serial.print(temp, 2);
    Serial.println(F("°C"));
  } else {
    Serial.println(F("ERREUR"));
  }
  Serial.println();
}

void printSystemInfo() {
  Serial.println(F("\n╔══════════════════════════════════╗"));
  Serial.println(F("║   INFORMATIONS SYSTÈME          ║"));
  Serial.println(F("╚══════════════════════════════════╝"));
  
  Serial.print(F("État: "));
  switch (currentState) {
    case STATE_IDLE: Serial.println(F("En attente")); break;
    case STATE_INIT_DENAT: Serial.println(F("Dénaturation initiale")); break;
    case STATE_CYCLE_DENAT: Serial.println(F("Dénaturation (cycle)")); break;
    case STATE_CYCLE_ANNEAL: Serial.println(F("Hybridation")); break;
    case STATE_CYCLE_EXTEND: Serial.println(F("Extension")); break;
    case STATE_FINAL_EXT: Serial.println(F("Extension finale")); break;
    case STATE_HOLD: Serial.println(F("Conservation")); break;
    case STATE_COMPLETE: Serial.println(F("Terminé")); break;
    case STATE_ERROR: Serial.println(F("ERREUR")); break;
  }
  
  if (systemRunning) {
    Serial.print(F("Cycle: ")); Serial.print(currentCycle); 
    Serial.print(F(" / ")); Serial.println(NUM_CYCLES);
  }
  
  printTemperature();
  
  Serial.print(F("Mémoire libre: ")); 
  Serial.print(freeMemory()); 
  Serial.println(F(" bytes"));
  Serial.println();
}

void printStatistics() {
  unsigned long totalTime = (millis() - totalRunTime) / 1000;
  int minutes = totalTime / 60;
  int seconds = totalTime % 60;
  
  Serial.println(F("\n📊 STATISTIQUES:"));
  Serial.print(F("   Durée totale: ")); 
  Serial.print(minutes); Serial.print(F("m ")); 
  Serial.print(seconds); Serial.println(F("s"));
  Serial.print(F("   Cycles chauffage: ")); Serial.println(heatingPulses);
  Serial.print(F("   Cycles refroidissement: ")); Serial.println(coolingCycles);
  Serial.println();
}

// ═══════════════════════════════════════════════════════════════════════════
// LED DE STATUT
// ═══════════════════════════════════════════════════════════════════════════

void updateStatusLED() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  
  unsigned long now = millis();
  
  if (currentState == STATE_ERROR) {
    // Clignotement rapide en erreur
    if (now - lastBlink >= 200) {
      ledState = !ledState;
      digitalWrite(STATUS_LED, ledState);
      lastBlink = now;
    }
  } else if (systemRunning) {
    // Clignotement lent en fonctionnement
    if (now - lastBlink >= 500) {
      ledState = !ledState;
      digitalWrite(STATUS_LED, ledState);
      lastBlink = now;
    }
  } else {
    // LED fixe en attente
    digitalWrite(STATUS_LED, HIGH);
  }
}

void blinkError() {
  while (true) {
    digitalWrite(STATUS_LED, HIGH);
    delay(100);
    digitalWrite(STATUS_LED, LOW);
    delay(100);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// UTILITAIRES
// ═══════════════════════════════════════════════════════════════════════════

int freeMemory() {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

// ═══════════════════════════════════════════════════════════════════════════
// FIN DU CODE
// ═══════════════════════════════════════════════════════════════════════════
