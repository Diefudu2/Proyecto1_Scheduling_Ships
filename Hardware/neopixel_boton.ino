#include <Adafruit_NeoPixel.h>

#define PIN_DATOS    4
#define PIN_LDR      34

#define NUM_LEDS     21
#define PARPADEO_MS  200

/*
 * Ajustar este valor según la lectura real de la fotoresistencia.
 * En ESP32 el ADC normalmente va de 0 a 4095.
 *
 * Con conexión:
 * 3.3V ---- LDR ---- GPIO34 ---- resistencia 10k ---- GND
 *
 * Normalmente:
 *   luz    -> lectura alta
 *   sombra -> lectura baja
 */
#define UMBRAL_LDR   1800

/*
 * Histeresis para evitar que el sensor oscile cerca del umbral.
 */
#define HISTERESIS_LDR 150

/*
 * Tiempo mínimo entre interrupciones por sensor.
 */
#define SENSOR_COOLDOWN_MS 800

Adafruit_NeoPixel tira(NUM_LEDS, PIN_DATOS, NEO_GRB + NEO_KHZ800);

// Tipos:
// 0 = Apagado
// 1 = Verde          - Barco Normal
// 2 = Azul           - Barco Pesquero
// 3 = Magenta        - Barco Patrulla
// 4 = Blanco parpad. - Barrera activa
// 5 = Rojo fijo      - Barrera interrupción
// 6 = Amarillo       - Indicador flujo derecha
// 7 = Verde claro    - Indicador flujo izquierda

int estadoActual[NUM_LEDS] = {0};

unsigned long ultimoParpadeo = 0;
bool parpadeoOn = true;

int sensorActivo = 0;
unsigned long ultimoEventoSensor = 0;

/* =========================================================
 * Color por tipo
 * ========================================================= */
uint32_t colorPorTipo(int tipo) {
  switch (tipo) {
    case 1: return tira.Color(0,   200, 0);    // Verde - Normal
    case 2: return tira.Color(0,   0,   200);  // Azul - Pesquero
    case 3: return tira.Color(200, 0,   200);  // Magenta - Patrulla
    case 5: return tira.Color(200, 0,   0);    // Rojo fijo - Interrupción
    case 6: return tira.Color(200, 150, 0);    // Amarillo - Flujo derecha
    case 7: return tira.Color(50,  200, 50);   // Verde claro - Flujo izquierda
    default:return tira.Color(0,   0,   0);    // Apagado
  }
}

/* =========================================================
 * Refrescar tira LED
 * ========================================================= */
void refrescarTira(bool parpadeoOn) {
  for (int i = 0; i < NUM_LEDS; i++) {
    if (estadoActual[i] == 4) {
      tira.setPixelColor(
        i,
        parpadeoOn ? tira.Color(255, 255, 255) : tira.Color(0, 0, 0)
      );
    } else {
      tira.setPixelColor(i, colorPorTipo(estadoActual[i]));
    }
  }

  tira.show();
}

/* =========================================================
 * Parsear lista recibida desde C
 *
 * Formato esperado:
 * [0,1,2,3,4,5,6,7,...]
 * ========================================================= */
int parsearLista(String entrada, int* salida, int maxLen) {
  int count = 0;

  entrada.replace("[", "");
  entrada.replace("]", "");
  entrada.trim();

  int inicio = 0;

  while (count < maxLen) {
    int coma = entrada.indexOf(',', inicio);
    String token;

    if (coma == -1) {
      token = entrada.substring(inicio);
      token.trim();

      if (token.length() > 0) {
        salida[count++] = token.toInt();
      }

      break;
    } else {
      token = entrada.substring(inicio, coma);
      token.trim();

      if (token.length() > 0) {
        salida[count++] = token.toInt();
      }

      inicio = coma + 1;
    }
  }

  return count;
}

/* =========================================================
 * Leer lista serial enviada por el programa en C
 * ========================================================= */
void revisarSerial() {
  if (!Serial.available()) {
    return;
  }

  String entrada = Serial.readStringUntil('\n');
  entrada.trim();

  if (entrada.startsWith("[")) {
    int lista[NUM_LEDS];
    int largo = parsearLista(entrada, lista, NUM_LEDS);

    for (int i = 0; i < NUM_LEDS; i++) {
      estadoActual[i] = (i < largo) ? lista[i] : 0;
    }

    Serial.print("Recibidos ");
    Serial.print(largo);
    Serial.println(" valores. Tira actualizada.");

    refrescarTira(parpadeoOn);
  } else {
    /*
     * No imprimimos demasiado para no saturar el canal serial.
     * El programa en C ignorará cualquier línea que no sea SENSOR:1/SENSOR:0.
     */
  }
}

/* =========================================================
 * Leer fotoresistencia y enviar evento de sensor
 *
 * Envía:
 *   SENSOR:1  cuando detecta interrupción
 *   SENSOR:0  cuando se libera
 * ========================================================= */
void revisarFotoresistencia() {
  int lectura = analogRead(PIN_LDR);
  unsigned long ahora = millis();

  /*
   * Caso asumido:
   *   lectura baja = sombra / barco detectado
   *   lectura alta = sin obstáculo
   *
   * Con histéresis:
   *   Para activarse: lectura < UMBRAL_LDR
   *   Para liberarse: lectura > UMBRAL_LDR + HISTERESIS_LDR
   */
  if (sensorActivo == 0) {
    if (lectura < UMBRAL_LDR &&
        ahora - ultimoEventoSensor >= SENSOR_COOLDOWN_MS) {

      sensorActivo = 1;
      ultimoEventoSensor = ahora;
      Serial.println("SENSOR:1");
    }
  } else {
    if (lectura > UMBRAL_LDR + HISTERESIS_LDR) {
      sensorActivo = 0;
      Serial.println("SENSOR:0");
    }
  }
}

/* =========================================================
 * Setup
 * ========================================================= */
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(10);

  pinMode(PIN_LDR, INPUT);

  tira.begin();
  tira.setBrightness(80);
  tira.clear();
  tira.show();

  Serial.println("ESP32-D listo.");
  Serial.println("Formato LEDs: [0,1,2,3,4,5,6,7,...]");
  Serial.println("Sensor envia: SENSOR:1 / SENSOR:0");

  delay(1000);
}

/* =========================================================
 * Loop
 * ========================================================= */
void loop() {
  revisarFotoresistencia();
  revisarSerial();

  if (millis() - ultimoParpadeo >= PARPADEO_MS) {
    parpadeoOn = !parpadeoOn;
    ultimoParpadeo = millis();
    refrescarTira(parpadeoOn);
  }
}