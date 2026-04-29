#include <Adafruit_NeoPixel.h>

#define PIN_DATOS    4
#define PIN_BOTON    23
#define NUM_LEDS     21
#define PARPADEO_MS  200   // velocidad del parpadeo blanco (tipo 4)

Adafruit_NeoPixel tira(NUM_LEDS, PIN_DATOS, NEO_GRB + NEO_KHZ800);

// Tipos:
// 0 = Apagado
// 1 = Verde          - Barco Normal
// 2 = Azul           - Barco Pesquero
// 3 = Magenta        - Barco Patrulla
// 4 = Blanco parpad. - Barrera activa (entrada/salida)
// 5 = Rojo fijo      - Barrera interrupción (canal bloqueado)
// 6 = Amarillo       - Indicador flujo derecha
// 7 = Verde claro    - Indicador flujo izquierda

int estadoActual[NUM_LEDS] = {0};  // guarda la lista actual

uint32_t colorPorTipo(int tipo) {
  switch (tipo) {
    case 1: return tira.Color(0,   200, 0);    // Verde        - Normal
    case 2: return tira.Color(0,   0,   200);  // Azul         - Pesquero
    case 3: return tira.Color(200, 0,   200);  // Magenta      - Patrulla
    // caso 4 se maneja en el loop (parpadeo)
    case 5: return tira.Color(200, 0,   0);    // Rojo fijo    - Interrupción
    case 6: return tira.Color(200, 150, 0);    // Amarillo     - Flujo derecha
    case 7: return tira.Color(50,  200, 50);   // Verde claro  - Flujo izquierda
    default:return tira.Color(0,   0,   0);    // Apagado
  }
}

// Refresca la tira según estadoActual
// parpadeoOn indica si los LEDs tipo 4 están en ON u OFF en este ciclo
void refrescarTira(bool parpadeoOn) {
  for (int i = 0; i < NUM_LEDS; i++) {
    if (estadoActual[i] == 4) {
      // Blanco parpadeante
      tira.setPixelColor(i, parpadeoOn ? tira.Color(255, 255, 255) : tira.Color(0, 0, 0));
    } else {
      tira.setPixelColor(i, colorPorTipo(estadoActual[i]));
    }
  }
  tira.show();
}

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
      if (token.length() > 0) salida[count++] = token.toInt();
      break;
    } else {
      token = entrada.substring(inicio, coma);
      token.trim();
      if (token.length() > 0) salida[count++] = token.toInt();
      inicio = coma + 1;
    }
  }
  return count;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOTON, INPUT_PULLUP);
  tira.begin();
  tira.setBrightness(80);
  tira.clear();
  tira.show();

  Serial.println("Listo. Tipos:");
  Serial.println("  0=Apagado  1=Normal(verde)  2=Pesquero(azul)  3=Patrulla(magenta)");
  Serial.println("  4=Barrera activa(blanco parpad.)  5=Barrera bloqueada(rojo)");
  Serial.println("  6=Flujo derecha(amarillo)  7=Flujo izquierda(verde claro)");
  Serial.println("Envía: [0,1,2,3,4,5,6,7,0,1,2,3,0,0,0,4,0,1,2,0,6]");
  delay(1000);  // espera que el booteo termine
Serial.begin(115200);
}

unsigned long ultimoParpadeo = 0;
bool parpadeoOn = true;

void loop() {
  // Parpadeo para los LEDs tipo 4
  if (millis() - ultimoParpadeo >= PARPADEO_MS) {
    parpadeoOn = !parpadeoOn;
    ultimoParpadeo = millis();
    refrescarTira(parpadeoOn);
  }

  // Lectura por serial
  if (Serial.available()) {
    String entrada = Serial.readStringUntil('\n');
    entrada.trim();

    if (entrada.startsWith("[")) {
      int lista[NUM_LEDS];
      int largo = parsearLista(entrada, lista, NUM_LEDS);

      // Actualiza estado global
      for (int i = 0; i < NUM_LEDS; i++) {
        estadoActual[i] = (i < largo) ? lista[i] : 0;
      }

      Serial.print("Recibidos ");
      Serial.print(largo);
      Serial.println(" valores. Tira actualizada.");

      refrescarTira(parpadeoOn);
    } else {
      Serial.println("Formato inválido. Usa: [1,2,0,3,...]");
    }
  }
}