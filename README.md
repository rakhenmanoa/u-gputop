# u-gputop (adreno-smi)

Moniteur GPU Adreno en ligne de commande, façon `nvidia-smi`, en C sans dépendance.
Cible : Xiaomi 12T Pro (diting) sous Linux avec le pilote DRM msm (Mesa freedreno).

Affiche la fréquence, la température, la charge et les processus qui utilisent le GPU.

## Compilation

    gcc -O2 -Wall -Wextra -o adreno-smi adreno-smi.c

## Utilisation

    ./adreno-smi          # un seul affichage
    ./adreno-smi -l 1     # rafraîchit chaque seconde (Ctrl+C pour quitter)

Sans root, seuls les processus de l'utilisateur sont visibles ; `sudo` pour tout voir.

## Sources des données

- fréquence / gouverneur : `/sys/class/devfreq/*gpu*`
- température : `/sys/class/thermal/thermal_zone*` (type contenant « gpu »)
- charge et processus : `/proc/<pid>/fdinfo/<fd>` (`drm-engine-gpu`, `drm-client-id`, `drm-resident-memory`)

La charge et le % par processus demandent que le noyau expose les stats fdinfo du pilote msm.
