// startup.h — avvio automatico con Windows, senza toccare il registro.
//
// Una scorciatoia nella cartella Esecuzione automatica dell'utente: è il
// meccanismo che l'utente può vedere e togliere da solo con Esplora file, ed è
// l'unico coerente col vincolo "app portable" (§2 del design). Lo stato non si
// tiene nell'INI ma si legge dall'esistenza della scorciatoia: così resta vero
// anche se qualcuno la cancella da fuori.
#pragma once
#include "common.h"

namespace startup {

bool Enabled();
bool Enable();   // crea la scorciatoia all'eseguibile corrente
bool Disable();  // la rimuove

}  // namespace startup
