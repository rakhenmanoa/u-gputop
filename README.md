# u-gputop (adreno-smi)

Moniteur GPU Adreno en ligne de commande, façon `nvidia-smi`, en C sans dépendance.
Cible : Xiaomi 12T Pro (diting) sous Linux avec le pilote DRM msm (Mesa freedreno).

Affiche la fréquence, la température, la charge et les processus qui utilisent le GPU.

## Licence

[BSD Zero Clause (0BSD)](LICENSE) : utilisation, copie, modification et redistribution
libres pour n'importe quel usage, sans aucune obligation (pas même de citer l'auteur).

## Compilation

    gcc -O2 -Wall -Wextra -o adreno-smi adreno-smi.c

## Utilisation

    ./adreno-smi          # un seul affichage
    ./adreno-smi -l 1     # rafraîchit chaque seconde (Ctrl+C pour quitter)

Sans root, seuls les processus de l'utilisateur sont visibles ; `sudo` pour tout voir.

## waydroid-focus : libérer le GPU pour une appli Waydroid

Script bash qui lance une appli Android et la favorise tant qu'elle tourne :

    ./waydroid-focus guitartuna              # alias de com.ovelin.guitartuna
    ./waydroid-focus -n guitartuna           # simulation : n'écrit rien
    ./waydroid-focus --gpu-max 500 --size 720x1560 guitartuna

- gèle (SIGSTOP) les autres processus de l'utilisateur qui utilisent le GPU, sauf le bureau,
  le terminal et Waydroid (`--keep REGEX` pour en protéger d'autres, `--no-freeze` pour désactiver) ;
- côté Android : coupe les animations et arrête les applis en cache (`--no-android` pour désactiver) ;
- en option : réduit la résolution (`--size`) et plafonne la fréquence max du GPU (`--gpu-max`, en MHz) ;
- restaure tout à la fermeture de l'appli ou sur Ctrl+C (`--stop` arrête aussi la session Waydroid).

Il demande `sudo` (shell Waydroid, fréquence GPU). Surveille l'effet avec `./adreno-smi -l 1`.

## Sources des données

- fréquence / gouverneur : `/sys/class/devfreq/*gpu*`
- température : `/sys/class/thermal/thermal_zone*` (type contenant « gpu »)
- charge et processus : `/proc/<pid>/fdinfo/<fd>` (`drm-engine-gpu`, `drm-client-id`, `drm-resident-memory`)

La charge et le % par processus demandent que le noyau expose les stats fdinfo du pilote msm.
