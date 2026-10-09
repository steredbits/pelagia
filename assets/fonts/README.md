# Polices embarquées

`NotoSans-Regular.ttf` et `NotoSans-Bold.ttf` : sous-ensembles de **Noto Sans** (Google, licence
SIL Open Font License 1.1, voir `OFL.txt` ; Noto n'a pas de nom de police réservé, la version
modifiée garde donc son nom). Source : paquet Debian/Ubuntu `fonts-noto-core` (20201225).

Les fichiers sont compilés dans le binaire (tableaux C générés par `cmake/embed_fonts.cmake`) :
aucun accès disque au démarrage, rien à installer à côté de l'exécutable sur PS5.

Couverture (sans CJK) : Latin de base (U+0020–U+007E), Latin-1 Supplément (U+00A0–U+00FF),
Latin étendu A (U+0100–U+017F), Ponctuation générale (U+2000–U+206F : ‐ ’ “ ” … etc.),
€ (U+20AC), ™ (U+2122), flèches U+2190–U+2193, U+2212 et U+FFFD (glyphe des caractères absents).
® « » ° sont dans Latin-1. Crénage (GPOS) conservé, hinting retiré (~30 Ko par graisse).

Regénération (python3-fonttools) :

```bash
for w in Regular Bold; do
  python3 -m fontTools.subset /usr/share/fonts/truetype/noto/NotoSans-$w.ttf \
    --unicodes="U+0020-007E,U+00A0-00FF,U+0100-017F,U+2000-206F,U+20AC,U+2122,U+2190-2193,U+2212,U+FFFD" \
    --layout-features='kern' --no-hinting --desubroutinize \
    --output-file=assets/fonts/NotoSans-$w.ttf
done
```

Écritures non latines : non couvertes en v1 (glyphe U+FFFD) ; un fallback est prévu après la v1.
