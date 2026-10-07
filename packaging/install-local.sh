#!/usr/bin/env bash
# Sustituye la libfprint del sistema por la NUESTRA (./libfprint/build) para
# poder probar fprintd-enroll/verify con el driver betterlife347d (347d:0304).
# REVERSIBLE: guarda un backup del .so original de Fedora en packaging/backup/.
# Reviertelo con packaging/uninstall-local.sh.
#
# Idempotente: si ya esta instalada la nuestra, no hace nada. El backup pristino
# NUNCA se sobrescribe (si existe, se respeta como el original de Fedora).
#
# Uso: packaging/install-local.sh      (pedira sudo donde haga falta)
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
ours="$repo/libfprint/build/libfprint/libfprint-2.so.2.0.0"
soname_link="/usr/lib64/libfprint-2.so.2"
backup_dir="$repo/packaging/backup"
backup="$backup_dir/libfprint-2.so.2.0.0.fedora-backup"

log(){ printf '==> %s\n' "$*"; }
die(){ printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# 1) Comprobaciones previas
[ -f "$ours" ] || die "No existe nuestra build: $ours
     Compila primero: ninja -C $repo/libfprint/build"
[ -L "$soname_link" ] || [ -f "$soname_link" ] || die "No encuentro libfprint del sistema en $soname_link"

# Fichero real del sistema (resuelve el symlink soname -> .so.2.0.0)
target="$(readlink -f "$soname_link")"
[ -f "$target" ] || die "No puedo resolver el .so real del sistema: $target"
log "libfprint del sistema (fichero real): $target"
log "nuestra libfprint:                   $ours"

sum(){ sha256sum "$1" | awk '{print $1}'; }
sum_ours="$(sum "$ours")"
sum_target="$(sum "$target")"

# Ya instalada?
if [ "$sum_ours" = "$sum_target" ]; then
  log "El sistema ya usa NUESTRA libfprint (sha coincide). Nada que hacer."
  exit 0
fi

# 2) Backup del original de Fedora (solo si no existe uno pristino)
mkdir -p "$backup_dir"
if [ -f "$backup" ]; then
  log "Backup ya existe, se respeta como original de Fedora: $backup"
else
  cp -p "$target" "$backup"
  sum "$backup" > "$backup.sha256"
  log "Backup creado: $backup"
fi

# 3) Copiar la nuestra encima del fichero real (misma ruta, perms 0755)
log "Instalando nuestra libfprint (requiere sudo)..."
sudo install -m 0755 "$ours" "$target"
[ "$(sum "$target")" = "$sum_ours" ] || die "La copia no coincide tras instalar; aborta."
log "Copiada y verificada."

# 4) Recargar cache del linker
sudo ldconfig
log "ldconfig ejecutado."

# 5) Parar fprintd: es Type=dbus (activado por D-Bus), se relanzara solo en la
#    proxima llamada (fprintd-enroll) ya con la .so nueva.
sudo systemctl stop fprintd 2>/dev/null || true
log "fprintd parado; se reactivara por D-Bus al usar fprintd-enroll/verify."

log "LISTO. Prueba:  fprintd-enroll   y luego  fprintd-verify"
log "Revertir:       packaging/uninstall-local.sh"
