# CloneCraft

CloneCraft ist ein data-driven C++-Voxel-Engine-Prototyp auf Basis von OgreNext.

Der aktuelle Entwicklungsstand wird über `INDEX.plan` und `docs/` dokumentiert.

## Einmaliger Umstieg vom bisherigen ZIP-Ordner auf Git

Lade `clonecraft-git.sh` in deinen vorhandenen CloneCraft-Quellordner, mache es ausführbar und starte:

```bash
chmod +x clonecraft-git.sh
./clonecraft-git.sh bootstrap
```

`bootstrap` verbindet den vorhandenen Quellbaum mit diesem Repository, übernimmt die bereits vorhandene `main`-Historie, commitet den lokalen Quellbaum und pusht ihn nach GitHub.

## Danach: keine ZIPs mehr

Frischer Checkout:

```bash
git clone https://github.com/Alfredlang1989/CloneCraft.git
cd CloneCraft
```

Updates holen:

```bash
./clonecraft-git.sh update
```

Eigene Änderungen committen und pushen:

```bash
./clonecraft-git.sh push "Beschreibung der Änderung"
```

Status anzeigen:

```bash
./clonecraft-git.sh status
```

Der Update-Befehl verweigert das Überschreiben lokaler Änderungen und verwendet `git pull --ff-only`, damit nicht unbemerkt Merge-Commits entstehen.
