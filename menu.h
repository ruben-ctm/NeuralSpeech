#ifndef MENU_H
#define MENU_H

#include "types.h"
#include "vocal.h"

void menu_afficher();
Etat menu_boucle_vad();               // VAD continu : bloque jusqu'à commande reconnue
Etat menu_update(int commande, bool bouton); // compatibilité

#endif