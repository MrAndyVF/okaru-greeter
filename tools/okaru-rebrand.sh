#!/usr/bin/env bash
# Renombrado noctalia-greeter -> okaru-greeter: binarios, rutas (/var/lib, /run), variables de entorno,
# acción de polkit y título. Conserva los nombres internos (namespace noctalia::, paleta "Noctalia").
# Es idempotente: se puede volver a correr después de traer cambios de upstream.
set -euo pipefail
cd "$(dirname "$0")/.."

# Archivos con nombre de marca.
for f in scripts/noctalia-greeter-* data/tmpfiles.d/noctalia-greeter.conf data/org.noctalia.greeter.*; do
  [ -e "$f" ] || continue
  git mv "$f" "$(sed 's/noctalia-greeter/okaru-greeter/; s/org\.noctalia\.greeter/dev.okaru.greeter/' <<<"$f")"
done

# Contenido: todo lo que se compila, instala o prueba (la documentación y Nix quedan como upstream).
git ls-files src tests scripts data examples meson.build meson_options.txt Justfile .gitignore \
  | grep -v '^tools/okaru-' | xargs grep -lIi 'noctalia' 2>/dev/null | xargs -r perl -pi -e '
  s{noctalia-greeter}{okaru-greeter}g;
  s{NOCTALIA_GREETER}{OKARU_GREETER}g;
  s{noctalia_greeter}{okaru_greeter}g;
  s{org\.noctalia\.greeter}{dev.okaru.greeter}g;
  s{Noctalia Greeter}{OKARU Greeter}g;
  s{dev\.noctalia\.Greeter}{dev.okaru.Greeter}g;
  s{\.bak\.noctalia\.}{.bak.okaru.}g;
'
