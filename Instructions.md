## DESCRIPTION:
  Code de contrôle pour thermocycleur PCR DIY basé sur Arduino Nano/Uno
  
  Fonctionnalités:
  - Contrôle PID de température haute précision
  - Cycles PCR programmables
  - Sécurités multiples (surchauffe, déconnexion sonde)
  - Interface série pour monitoring
  - LED d'état
  
## MATÉRIEL REQUIS:
  - Arduino Nano ou Uno
  - Thermocouple Type-K + MAX31855
  - Relais SSR 25A (contrôle résistances chauffantes)
  - Ventilateur 12V (refroidissement)
  - Transistor TIP120 (contrôle ventilateur)
  - Résistances chauffantes 2x50W

## CONNEXIONS:
  Pin 4  -> MAX31855 DO (MISO)
  Pin 5  -> MAX31855 CS
  Pin 6  -> MAX31855 CLK (SCK)
  Pin 7  -> Relais SSR (chauffage)
  Pin 9  -> Base transistor TIP120 (ventilateur)
  Pin 13 -> LED intégrée (status)

## BIBLIOTHÈQUES REQUISES:
  - Adafruit_MAX31855 (Thermocouple)
  
## INSTALLATION:
  1. Installer la bibliothèque Adafruit_MAX31855 via le gestionnaire
  2. Téléverser ce code sur l'Arduino
  3. Ouvrir le moniteur série (9600 bauds)
  4. Suivre les instructions

## COMMANDES SÉRIE:
  'S' - Démarrer le cycle PCR
  'X' - Arrêt d'urgence
  'T' - Afficher température actuelle
  'I' - Informations système
  
## SÉCURITÉ:
  ⚠️  NE JAMAIS laisser sans surveillance
  ⚠️  Température max: 100°C (sécurité matérielle)
  ⚠️  Vérifier connexions électriques avant utilisation
  ⚠️  Couper alimentation si comportement anormal
