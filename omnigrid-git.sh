#!/usr/bin/env bash
set -euo pipefail

REPO_URL="${OMNIGRID_REPO_URL:-git@github.com:Alfredlang1989/OmniGrid.git}"
BRANCH="${OMNIGRID_BRANCH:-main}"
CMD="${1:-help}"

say() { printf '[OmniGrid Git] %s\n' "$*"; }
die() { printf '[ERROR] %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<'HELP'
OmniGrid Git helper

Einmalig, im vorhandenen OmniGrid-Quellordner:
  ./omnigrid-git.sh bootstrap

Neuen Rechner / frischen Checkout anlegen:
  ./omnigrid-git.sh clone [zielordner]

Danach im Repository:
  ./omnigrid-git.sh update
  ./omnigrid-git.sh status
  ./omnigrid-git.sh push "Commit-Nachricht"

Bedeutung:
  bootstrap  Vorhandenen OmniGrid-Quellordner mit Alfredlang1989/OmniGrid
             verbinden, alles committen und pushen. Bestehende Git-Historie
             wird erhalten.
  clone      Repository erstmalig herunterladen.
  update     Remote-Änderungen holen. Bricht bei lokalen Änderungen ab.
  status     Kurzen Git-Status anzeigen.
  push       Alle lokalen Änderungen committen und nach main pushen.

Optionale Umgebungsvariablen:
  OMNIGRID_REPO_URL   anderes Remote verwenden
  OMNIGRID_BRANCH     anderer Branch (Standard: main)
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
      say "Vorhandenes Git-Repository erkannt; lokale Historie bleibt erhalten."
      ensure_origin

      # Der Bootstrap soll den kompletten aktuellen Quellbaum übernehmen. Deshalb
      # werden vorhandene lokale Änderungen zunächst bewusst als eigener Commit gesichert.
      git add -A
      if ! git diff --cached --quiet; then
        git commit -m "Prepare OmniGrid GitHub bootstrap"
      fi

      git branch -M "$BRANCH"
      say "Hole vorhandene $BRANCH-Historie von GitHub ..."
      git fetch origin "$BRANCH"

      if git merge-base HEAD "origin/$BRANCH" >/dev/null 2>&1; then
        # Gemeinsame Historie: nur Fast-Forward/normalen Stand prüfen. Ein echter
        # Divergenzfall wird nicht automatisch mit Gewalt überschrieben.
        if git merge-base --is-ancestor "origin/$BRANCH" HEAD; then
          :
        elif git merge-base --is-ancestor HEAD "origin/$BRANCH"; then
          git merge --ff-only "origin/$BRANCH"
        else
          die "Lokale und Remote-Historie sind bereits divergent. Bitte manuell prüfen."
        fi
      else
        # Typischer Erstimport: lokales altes Git und das neue GitHub-Bootstrap-Repo
        # haben keine gemeinsame Wurzel. 'ours' verbindet nur die Historien; der
        # lokale OmniGrid-Dateibaum bleibt vollständig unverändert.
        git merge --allow-unrelated-histories -s ours "origin/$BRANCH" \
          -m "Attach OmniGrid GitHub repository"
      fi
    else
      say "Initialisiere Git im aktuellen Quellordner ..."
      git init -b "$BRANCH"
      ensure_origin

      # Das GitHub-Repo besitzt bereits einen kleinen Bootstrap-Commit. Wir setzen
      # HEAD/Index darauf, lassen aber den kompletten vorhandenen Arbeitsbaum unangetastet.
      say "Hole vorhandene $BRANCH-Historie von GitHub ..."
      git fetch origin "$BRANCH"
      git reset --mixed "origin/$BRANCH"

      say "Übernehme aktuellen OmniGrid-Quellbaum ..."
      git add -A
      if git diff --cached --quiet; then
        say "Keine lokalen Dateien zu übernehmen."
      else
        git commit -m "Import OmniGrid source tree"
      fi
    fi

    say "Push nach origin/$BRANCH ..."
    git push -u origin "$BRANCH"
    say "Fertig. Ab jetzt: ./omnigrid-git.sh update bzw. push"
    ;;

  clone)
    require_git
    TARGET="${2:-OmniGrid}"
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
    MESSAGE="${*:-Update OmniGrid}"
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
