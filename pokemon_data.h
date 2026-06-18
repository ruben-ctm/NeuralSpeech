#ifndef POKEMON_DATA_H
#define POKEMON_DATA_H

struct Pokemon {
  uint8_t numero;
  char    nom[12];
  char    type[10];
  uint8_t hp;
  uint8_t atk;
  uint8_t def;
  uint8_t spd;
};

const uint8_t NB_POKEMON = 20;

const Pokemon pokedex_data[NB_POKEMON] = {
  {  6, "Dracaufeu",  "Feu",      78,  84,  78, 100 },
  {  9, "Tortank",    "Eau",      79,  83, 100,  78 },
  {  3, "Florizarre", "Plante",   80,  82,  83,  80 },
  { 25, "Pikachu",    "Electrik", 35,  55,  40,  90 },
  { 39, "Rondoudou",  "Normal",  115,  45,  20,  20 },
  { 52, "Miaouss",    "Normal",   40,  45,  35,  90 },
  { 59, "Arcanin",    "Feu",      90, 110,  80,  95 },
  { 94, "Gengar",     "Spectre",  60,  65,  60, 110 },
  {131, "Lokhlass",   "Eau",     130,  85,  80,  60 },
  {143, "Ronflex",    "Normal",  160, 110,  65,  30 },
  {144, "Artikodin",  "Glace",    90,  85, 100,  85 },
  {145, "Electhor",   "Electrik", 90,  90,  85, 100 },
  {146, "Sulfura",    "Feu",      90, 100,  90,  90 },
  {149, "Dracolosse", "Dragon",   91, 134,  95,  80 },
  {150, "Mewtwo",     "Psy",     106, 110,  90, 130 },
  {157, "Typhlosion", "Feu",      78,  84,  78, 100 },
  {160, "Aligatueur", "Eau",      85, 105, 100,  78 },
  {196, "Mentali",    "Psy",      65,  65,  60, 110 },
  {248, "Tyranocif",  "Dragon",  100, 134, 110,  61 },
  {249, "Lugia",      "Vol",     106,  90, 130, 110 },
};

#endif
