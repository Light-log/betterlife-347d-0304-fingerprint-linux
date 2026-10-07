#!/usr/bin/env bash
# Revierte install-local.sh: restaura la libfprint original de Fedora desde el
# backup, recarga el linker y para fprintd (se reactiva por D-Bus).
# Idempotente: si el sistema ya es el original, no hace nada.
#
# Uso: packaging/uninstall-local.sh     (pedira sudo donde haga falta)
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
soname_link="/usr/lib64/libfprint-2.so.2"
backup="$repo/packaging/backup/libfprint-2.so.2.0.0.fedora-backup"

log(){ printf '==> %s\n' "$*"; }
die(){ printf 'ERROR: %s\n' "$*" >&2; exit 1; }
sum(){ sha256sum "$1" | awk '{print $1}'; }

[ -f "$backup" ] || die "No hay backup que restaurar: $backup
     (si nunca instalaste, no hay nada que revertir)"

target="$(readlink -f "$soname_link")"
[ -f "$target" ] || die "No puedo resolver el .so real del sistema: $target"

# Verifica integridad del backup si guardamos su sha
if [ -f "$backup.sha256" ]; then
  [ "$(sum "$backup")" = "$(cat "$backup.sha256")" ] || die "El backup esta corrupto (sha no coincide). No restauro."
fi

if [ "$(sum "$target")" = "$(sum "$backup")" ]; then
  log "El sistema ya usa la libfprint original de Fedora. Nada que hacer."
  exit 0
fi

log "Restaurando original de Fedora en $target (requiere sudo)..."
sudo install -m 0755 "$backup" "$target"
[ "$(sum "$target")" = "$(sum "$backup")" ] || die "La restauracion no coincide; revisa manualmente."
log "Restaurado y verificado."

sudo ldconfig
log "ldconfig ejecutado."

sudo systemctl stop fprintd 2>/dev/null || true
log "fprintd parado; se reactivara por D-Bus con la libfprint de Fedora."

log "LISTO. Sistema revertido a la libfprint de Fedora."
