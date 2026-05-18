# Notas internas: apropiación y flujo

## Objetivo

Esta versión separa dos decisiones:

1. El flujo decide si un lado puede entrar al canal.
2. El calendarizador apropiativo decide si un barco que ya está cruzando puede quitarle una posición/recurso a otro barco menos conveniente.

Esto mantiene la restricción del proyecto: no hay choques, no hay rebase y no hay dos barcos en la misma posición lógica.

## Algoritmos no apropiativos

- FCFS
- SJF
- PRIORITY

Estos algoritmos ordenan la cola READY, pero no expulsan barcos que ya están dentro del canal.

## Algoritmos apropiativos

- RR
- STRN
- EDF

### RR

RR puede sacar un barco del canal cuando consumió su quantum y existe otro barco esperando en READY.

### STRN y EDF

STRN y EDF ya no expulsan barcos apenas aparece un candidato mejor. La apropiación ocurre cuando el barco apropiativo intenta tomar un recurso ocupado:

- siguiente posición lógica ocupada;
- o siguiente segmento físico/LED ocupado.

En ese momento el bloqueador sale del canal, vuelve a READY y conserva `saved_position` para restaurarse posteriormente.

## Equidad y restauración

Cuando un barco apropiado vuelve a entrar usando `saved_position`, esa restauración no cuenta como un barco nuevo para `W` de Equidad. El barco ya había recibido turno antes de ser apropiado. Contarlo de nuevo causaba que `W=3` se agotara con menos barcos realmente admitidos.

## Flujo con mayor peso

El flujo sigue decidiendo la admisión desde READY hacia el canal por medio de `canal_flow_allows_entry()`. La apropiación directa solo ocurre entre barcos ya admitidos y que están disputando una posición/recurso dentro del canal.
