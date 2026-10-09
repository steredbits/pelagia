# cJSON

Copie de [cJSON](https://github.com/DaveGamble/cJSON) v1.7.18 (licence MIT, voir `LICENSE`) :
`cJSON.c` et `include/cjson/cJSON.h`, sans modification. PacBrew ne fournit pas cJSON pour la PS5 ;
la même copie sert sous Linux (plus de dépendance à `libcjson-dev`). Le core l'inclut par
`<cjson/cJSON.h>` ; cible CMake `pelagia_cjson` (CMakeLists.txt racine).
