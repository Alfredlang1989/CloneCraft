#!/usr/bin/env bash
set -euo pipefail

REPO_URL="${CLONECRAFT_REPO_URL:-https://github.com/Alfredlang1989/CloneCraft.git}"
BRANCH="${CLONECRAFT_BRANCH:-main}"
CMD="${1:-help}"

say() { printf '[CloneCraft Git] %s\n' "$*"; }
die() { printf '[ERROR] %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<'HELP'
CloneCraft Git helper

Einmalig, im vorhandenen CloneCraft-Quellordner:
  ./clonecraft-git.sh bootstrap

Neuen Rechner / frischen Checkout anlegen:
  ./clonecraft-git.sh clone [zielordner]

Danach im Repository:
  ./clonecraft-git.sh update
  ./clonecraft-git.sh status
  ./clonecraft-git.sh push "Commit-Nachricht"

Bedeutung:
  bootstrap  Vorhandenen, noch nicht von Git verwalteten Quellordner mit
             Alfredlang1989/CloneCraft verbinden, alles committen und pushen.
  clone      Repository erstmalig herunterladen.
  update     Remote-Änderungen holen. Bricht bei lokalen Änderungen ab.
  status     Kurzen Git-Status anzeigen.
  push       Alle lokalen Änderungen committen und nach main pushen.

Optionale Umgebungsvariablen:
  CLONECRAFT_REPO_URL   anderes Remote verwenden
  CLONECRAFT_BRANCH     anderer Branch (Standard: main)
HELP
}

require_git() {
  command -v git >/dev/null 2>&1 || die "git ist nicht installiert."
}

inside_repo() {
  git rev-parse --is-inside-work-tree >/dev/null 2>&1
}

ensure_origin() {
  if git remote get-url origin >/dev/null 2>&1; then
    git remote set-url origin "$REPO_URL"
  else
    git remote add origin "$REPO_URL"
  fi
}

case "$CMD" in
  bootstrap)
    require_git
    if inside_repo; then
      die "Dieser Ordner ist bereits ein Git-Repository. Benutze 'status', 'update' oder 'push'."
    fi

    say "Initialisiere Git im aktuellen Quellordner ..."
    git init -b "$BRANCH"
    ensure_origin

    # Das GitHub-Repo besitzt bereits einen kleinen Bootstrap-Commit. Wir setzen
    # HEAD/Index darauf, lassen aber den kompletten vorhandenen Arbeitsbaum unangetastet.
    say "Hole vorhandene $BRANCH-Historie von GitHub ..."
    git fetch origin "$BRANCH"
    git reset --mixed "origin/$BRANCH"

    say "Übernehme aktuellen CloneCraft-Quellbaum ..."
    git add -A
    if git diff --cached --quiet; then
      say "Keine lokalen Dateien zu übernehmen."
    else
      git commit -m "Import CloneCraft source tree"
    fi

    say "Push nach origin/$BRANCH ..."
    git push -u origin "$BRANCH"
    say "Fertig. Ab jetzt: ./clonecraft-git.sh update bzw. push"
    ;;

  clone)
    require_git
    TARGET="${2:-CloneCraft}"
    [[ ! -e "$TARGET" ]] || die "Ziel existiert bereits: $TARGET"
    say "Klone $REPO_URL nach $TARGET ..."
    git clone --branch "$BRANCH" "$REPO_URL" "$TARGET"
    ;;

  update)
    require_git
    inside_repo || die "'update' muss innerhalb eines Git-Checkouts laufen."
    [[ -z "$(git status --porcelain)" ]] || {
      git status --short >&2
      die "Lokale Änderungen vorhanden. Erst committen/pushen oder stashen."
    }
    ensure_origin
    say "Hole origin/$BRANCH ..."
    git fetch origin "$BRANCH"
    git switch "$BRANCH"
    git pull --ff-only origin "$BRANCH"
    ;;

  status)
    require_git
    inside_repo || die "Kein Git-Repository."
    git status --short --branch
    ;;

  push)
    require_git
    inside_repo || die "'push' muss innerhalb eines Git-Checkouts laufen."
    ensure_origin
    shift || true
    MESSAGE="${*:-Update CloneCraft}"
    git add -A
    if git diff --cached --quiet; then
      say "Nichts zu committen."
      exit 0
    fi
    git commit -m "$MESSAGE"
    git push -u origin "$BRANCH"
    ;;

  help|-h|--help)
    usage
    ;;

  *)
    usage >&2
    die "Unbekannter Befehl: $CMD"
    ;;
esac
