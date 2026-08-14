# OmniGrid — Primus impetus
## Verbindliche Arbeitsanweisung für Coding Agents

> **Zweck:** Diese Datei ist der operative Arbeitsvertrag für Coding-Agents, die an OmniGrid arbeiten.
>
> **Leitregel:**  
> **Kleinste generische Schicht bauen → an einem echten Verbraucher beweisen → testen → Architektur prüfen → AST prüfen → Dokumentation aktualisieren → Graphify aktualisieren → Git committen → GitHub dokumentieren → erst dann zum nächsten Milestone.**
>
> Kein zweiter World-State-Pfad. Kein zweites Eventsystem. Keine getrennten Action-/Event-/Network-Message-Welten. Keine Subsystem-Datenbank. Kein Singleplayer-Sonderweg. Keine Mega-Refactors auf Vorrat.

---

# 0. Nicht verhandelbare Grundregeln

Der Agent arbeitet **inkrementell und milestone-basiert**.

Jeder Milestone muss einen kleinen, überprüfbaren und funktionierenden Git-Zustand erzeugen.

Der Agent darf einen Milestone **nicht** als DONE melden, solange:

- Tests fehlen oder fehlschlagen;
- der Architektur-Gate fehlschlägt;
- der clang-tidy/AST-Gate fehlschlägt;
- relevante Dokumentation veraltet ist;
- Graphify nach relevanten Änderungen nicht aktualisiert wurde;
- der Milestone nicht committed wurde;
- der Commit nicht dem zugehörigen GitHub-Issue zugeordnet/dokumentiert wurde.

## Verboten

- keine Mega-Refactors auf Vorrat;
- keine zweite World-State-API;
- keine zweite Persistence-Architektur;
- keine zweite Event-/Action-Architektur;
- keine getrennte Network-Message-Semantik neben dem gemeinsamen Communication-Envelope;
- kein öffentliches Busmodell nach dem Muster `event_id -> vector<callbacks>` als Ersatz für adressierbare Kommunikation;
- keine rohen Funktionspointer/`std::function`-Callbacks im transportierbaren Nachrichtenvertrag;
- keine subsystemeigene Datenbank;
- kein direkter Input/UI → `ChunkManager::setBlock()`-Pfad;
- kein direkter UI → ECS/Sidecar/RocksDB-Zugriff;
- kein direkter Lua → Raw-Sidecar-Zugriff;
- keine ECS-Entity pro normalem Voxel;
- kein `if (singleplayer) mutateDirectly();`;
- kein Servercode mit OgreNext-/Renderer-Abhängigkeit;
- keine C++-Spezialfälle für konkrete Default-Inhalte;
- keine stillen Änderungen an Architekturregeln nur damit ein fehlerhafter Patch wieder grün wird;
- kein `git reset --hard`, kein Force-Push und kein Überschreiben fremder Arbeit ohne ausdrücklichen Auftrag;
- keine automatische Installation oder Änderung von OS-Paketen.

---

# 1. Pflichtprogramm am Anfang JEDER Agent-Session

Bevor Produktionscode geändert wird:

1. `INDEX.plan` lesen.
2. `docs/STATUS.md` lesen.
3. `docs/ARCHITECTURE.md` lesen.
4. `docs/DECISIONS.md` lesen.
5. `docs/MILESTONES.md` lesen.
6. Relevante Spezialdokumentation lesen.
7. `graphify-out/GRAPH_REPORT.md` lesen, sofern vorhanden.
8. Graphify für Architektur-/Abhängigkeitsfragen verwenden.
9. Das aktuelle GitHub-Issue vollständig lesen.
10. Vorgelagerte/abhängige GitHub-Issues prüfen.
11. `git status` ausführen.
12. `git log --oneline -10` ausführen.
13. `git diff` und ggf. `git diff --cached` prüfen.
14. Fremde oder unzusammenhängende Änderungen identifizieren und nicht anfassen.
15. Den exakten Milestone-Scope festlegen.

## GitHub ist Teil der Arbeitsquelle

Der Agent darf GitHub nicht nur als Linkablage behandeln.

Vor einem Milestone:

- zugehöriges Issue öffnen/lesen;
- Acceptance Criteria prüfen;
- Abhängigkeiten und verwandte Issues prüfen;
- vorhandene Diskussionen/Kommentare berücksichtigen;
- Parent-/Master-Tracker (#15) bei Architekturentscheidungen berücksichtigen.

Der Agent soll Änderungen gegen **Repo + Issues + Dokumentation gemeinsam** planen.

---

# 2. Baseline vor jeder Feature-Arbeit

Der Agent beweist zuerst den Ausgangszustand.

Mindestens:

```bash
python3 tools/architecture_check.py --root .
```

Wenn die Zielmaschine die benötigten Abhängigkeiten besitzt:

```bash
./compile.sh --analyze-only
```

und anschließend bei normaler Entwicklungsarbeit:

```bash
./compile.sh
```

Relevante vorhandene Tests zusätzlich gezielt ausführen.

## Wenn die Baseline bereits fehlschlägt

Dann:

- Fehler dokumentieren;
- prüfen, ob er den Milestone blockiert;
- nicht still dem eigenen Patch zurechnen;
- nicht nebenbei völlig fremde Fehler reparieren;
- bei Blockierung den Milestone als `BLOCKED`/`PARTIAL` melden, nicht als DONE.

---

# 3. Graphify ist Pflicht, aber kein Ersatz für clang-tidy

OmniGrid nutzt zwei unterschiedliche Analyseebenen.

## 3.1 Graphify

Graphify dient dem Agenten als Code-/Architekturgraph.

Vor größeren Änderungen bevorzugt nutzen:

- `query`
- `path`
- `explain`

Nach relevanten Codeänderungen:

```bash
graphify update .
```

Wenn Dokumentation, Konfiguration, Schemas oder andere für den Knowledge Graph relevante Dateien verändert wurden, zusätzlich über OpenCode:

```text
/graphify . --update
```

## 3.2 clang-tidy / C++ AST

Der verbindliche C++-AST-/Semantic-Gate bleibt:

```bash
./compile.sh --analyze-only
```

bzw. der darin aufgerufene statische Analysepfad.

**Graphify grün ersetzt clang-tidy nicht.**  
**clang-tidy grün ersetzt ein aktuelles Graphify nicht.**

Beides hat unterschiedliche Aufgaben.

---

# 4. Dokumentationspflicht pro Milestone

Der Agent muss am Ende jedes Milestones prüfen, welche Dokumentation betroffen ist.

## 4.1 `docs/STATUS.md`

Aktualisieren, wenn sich Projektzustand geändert hat.

Dokumentieren:

- was implementiert wurde;
- was getestet wurde;
- Einschränkungen;
- Performance-/Determinismus-Ergebnisse;
- bekannte Blocker;
- aktuellen Milestone-Status.

## 4.2 `docs/MILESTONES.md`

Nach jedem abgeschlossenen Milestone prüfen und bei Bedarf aktualisieren.

Die Datei muss die **aktuelle Realität** beschreiben, nicht einen historischen Plan.

## 4.3 `INDEX.plan`

Nach jedem Milestone zwingend prüfen.

Aktualisieren, wenn sich geändert haben:

- Projektname;
- Entwicklungsphase;
- Architektur-Invarianten;
- Read-first-Liste;
- wichtige Dateien;
- Build-/Analysebefehle;
- Agenten-Workflow;
- Abhängigkeiten;
- aktuelle Reihenfolge.

Wenn keine Änderung nötig ist, im Abschlussbericht ausdrücklich:

```text
INDEX.plan reviewed: no change required.
```

## 4.4 `docs/ARCHITECTURE.md`

Aktualisieren bei:

- neuen Modulen;
- Ownership-Änderungen;
- Dependency-Änderungen;
- neuen öffentlichen Abstraktionen;
- neuen Client-/Server-/World-State-Grenzen;
- neuen dauerhaft relevanten Datenflüssen;
- Communication-Envelope-, Routing-, Reply-/Correlation- oder Transport-Verträgen.

## 4.5 `docs/DECISIONS.md`

Nur für echte langfristige Architekturentscheidungen.

Nicht jede Funktion bekommt einen ADR.

Ein ADR ist erforderlich, wenn spätere Agents eine Entscheidung **nicht still wieder umkehren dürfen**.

## 4.6 Fachspezifische Dokumentation

Beispiele:

- Worldgen → Worldgen-Doku;
- Renderer/LOD → Renderer-/Performance-Doku;
- Coordinates → Coordinates/Dynamic-Space-Doku;
- Persistence → Persistence-Doku;
- Client/Server → Netzwerk-/Serverarchitektur;
- Construction → Construction-Doku;
- Lua → Lua-/Scripting-Doku;
- UI → UI-Doku.

## Grundsatz

**Code und Dokumentation müssen im selben Milestone denselben Zustand beschreiben.**

Kein „Doku später“.

---

# 5. Git-Pflichtworkflow

Git ist Teil der Definition of Done.

## 5.1 Vor der Arbeit

```bash
git status
git log --oneline -10
git diff
```

Bei bestehender fremder Arbeit:

- nicht überschreiben;
- nicht automatisch committen;
- eigenen Scope sauber trennen.

## 5.2 Während der Arbeit

Regelmäßig prüfen:

```bash
git diff --stat
git diff
```

Der Agent muss erkennen, wenn der Diff beginnt, den Milestone-Scope zu sprengen.

## 5.3 Vor dem Milestone-Commit

Pflicht:

```bash
git status
git diff
git diff --cached
```

Dann nur passende Dateien stagen.

Bevorzugt gezielt:

```bash
git add <files>
```

oder interaktiv:

```bash
git add -p
```

Keine fremden/unrelated Änderungen mitnehmen.

## 5.4 Jeder Milestone MUSS committed werden

Ein Milestone ist ohne Commit **nicht abgeschlossen**.

Mindestens ein sauberer Commit pro Milestone.

Beispiel:

```bash
git commit -m "feat(worldstate): add prototype registry foundation (#3)"
```

oder:

```bash
git commit -m "feat(interaction): complete voxel place/remove slice (#18)"
```

Commit-Messages sollen:

- kurz beschreiben, was tatsächlich abgeschlossen wurde;
- das relevante GitHub-Issue nennen;
- keine Behauptungen enthalten, die Tests/Gates nicht belegen.

## 5.5 Tags

Wenn das Projekt für den Milestone einen Tag vorsieht:

```bash
git tag <milestone-tag>
```

Nur auf einem getesteten, dokumentierten, grünen Zustand.

Keine Tags auf `PARTIAL` oder `BLOCKED`.

## 5.6 Push

Wenn der Agent Push-Rechte und einen expliziten Arbeitsauftrag für Remote-Änderungen hat:

```bash
git push
```

und ggf.:

```bash
git push --tags
```

Kein Force-Push ohne ausdrücklichen Auftrag.

---

# 6. GitHub-Pflichtworkflow pro Milestone

Nach dem Milestone-Commit muss der Agent GitHub aktualisieren.

## Im zugehörigen Issue dokumentieren

Mindestens:

- Milestone-Name;
- kurze Zusammenfassung;
- Commit-SHA;
- Tests/Gates;
- relevante Dokuänderungen;
- verbleibende Arbeit;
- bekannte Einschränkungen.

Beispielinhalt:

```text
Milestone M06 completed.

Commit: abc1234

Implemented:
- voxel raycast
- remove_block action
- place_block action
- shared World State mutation path
- mesh/neighbor invalidation

Validation:
- architecture_check: PASS
- clang-tidy AST: PASS
- build: PASS
- tests: PASS
- graphify update: DONE

Remaining in #18:
- none
```

## Issue nicht vorschnell schließen

Ein Parent-Issue wird erst geschlossen, wenn **alle Acceptance Criteria** erfüllt sind.

Ein Teil-Milestone in #3 bedeutet nicht, dass #3 geschlossen wird.

## Master-Tracker #15

Wenn ein Primus-impetus-Milestone abgeschlossen wurde, soll der Agent auch den Master-Tracker #15 berücksichtigen:

- Status abgleichen;
- Fortschritt dort dokumentieren, sofern passend;
- keine divergierende zweite Roadmap erzeugen.

---

# 7. Definition of Done für JEDEN Milestone

Ein Milestone ist nur DONE, wenn alles zutrifft:

- [ ] Scope vollständig implementiert.
- [ ] Acceptance Criteria des Milestones erfüllt.
- [ ] Relevante Regressionstests hinzugefügt/aktualisiert.
- [ ] Bestehende relevante Tests grün.
- [ ] `python3 tools/architecture_check.py --root .` grün.
- [ ] clang-tidy/AST grün.
- [ ] Build grün, sofern Zielsystem-Abhängigkeiten vorhanden sind.
- [ ] Keine neue verbotene Dependency.
- [ ] Kein neuer Bypass-Pfad.
- [ ] Graphify-Codegraph aktualisiert.
- [ ] Graphify-Dokument-/Semantikupdate durchgeführt, wenn nötig.
- [ ] `STATUS.md` geprüft/aktualisiert.
- [ ] `MILESTONES.md` geprüft/aktualisiert.
- [ ] `INDEX.plan` geprüft/aktualisiert.
- [ ] `ARCHITECTURE.md` geprüft/aktualisiert.
- [ ] `DECISIONS.md` geprüft/aktualisiert.
- [ ] Fachspezifische Doku geprüft/aktualisiert.
- [ ] `git diff` auf unbeabsichtigte Änderungen geprüft.
- [ ] Fremde Änderungen nicht mitcommitted.
- [ ] Milestone committed.
- [ ] Commit-SHA ermittelt.
- [ ] GitHub-Issue mit Milestone-Ergebnis aktualisiert.
- [ ] Parent-Issue nur geschlossen, wenn wirklich vollständig erledigt.

---

# 8. Verbindlicher Milestone-Abschlussbericht

Der Agent gibt nach jedem Milestone aus:

```text
Milestone:
Issues:
Status: DONE / PARTIAL / BLOCKED

Implemented:
- ...

Explicitly not implemented:
- ...

Tests added/changed:
- ...

Architecture gate: PASS / FAIL
clang-tidy AST: PASS / FAIL / BLOCKED
Build: PASS / FAIL / BLOCKED
Tests: PASS / FAIL

Graphify code update: DONE / NOT NEEDED / FAILED
Graphify docs/semantic update: DONE / NOT NEEDED / FAILED

STATUS.md: UPDATED / REVIEWED
MILESTONES.md: UPDATED / REVIEWED
INDEX.plan: UPDATED / REVIEWED
ARCHITECTURE.md: UPDATED / REVIEWED
DECISIONS.md: UPDATED / REVIEWED
Domain docs: ...

Git status:
Commit: <sha>
Tag: <tag or none>
GitHub issue updated: YES / NO

Remaining work:
- ...
```

Der Agent darf **erst danach** zum nächsten Milestone wechseln.

---

# 9. OmniGrid Primus impetus — Hauptreihenfolge

## M00 — Agent-/Dokumentations-Baseline

Noch keine Featureentwicklung.

Ziele:

- `INDEX.plan` auf OmniGrid/aktuellen Agentenworkflow bringen;
- Primus-impetus-Read-First-Reihenfolge sauber dokumentieren;
- Graphify-Regeln aufnehmen;
- AST-vs-Graphify-Trennung dokumentieren;
- `docs/MILESTONES.md` mit aktuellem Plan abgleichen;
- `docs/STATUS.md` auf aktuellen Zustand bringen;
- keine persistenten technischen IDs kosmetisch umbenennen.

**Gate:** Verhalten/Gameplay unverändert.

**Pflicht:** Commit + GitHub-Dokumentation.

---

## M01 — #14 frühe OmniGrid-Umbenennung

Nur sichere Branding-/Projektflächen:

- README;
- Fenster-/Anwendungstitel;
- Startup-Banner;
- Entwicklerdokumentation;
- Build-/Produktnamen in kleinen kontrollierten Schritten.

Nicht blind umbenennen:

- persistente Block-/Prototype-IDs;
- Mod-Namespaces;
- Save-Schema-IDs;
- Worldgen-Kompatibilitäts-IDs;
- spätere RocksDB-Keyspaces;
- Protokollidentitäten.

**Pflicht:** eigener Commit für diesen Milestone.

---

## M02 — #8 Worldgen-Baseline

Ziele:

- Desert Mask verbreitern;
- keine reine Threshold-Pfusche;
- `BiomeDef::weight`-Semantik klären;
- Statistiktests für Seed 1337 + mindestens einen weiteren Seed;
- Determinismus;
- Hierarchie-/Grenztests;
- relevante Worldgen-Doku aktualisieren.

Danach Worldgen-Baseline wieder einfrieren.

---

# CORE SPINE

## M03 — #3 Contracts + Prototype Foundation

Nur früher #3-Teil:

- Ownership-/Dependency Contracts;
- stabile namespaced IDs;
- generischer World/Block/Object Ref;
- Content-Root-Vertrag;
- `MODS/Default`;
- Prototype Registry;
- genau ein echter Pilotblock.

Noch keine vollständige ECS-/Persistence-/Event-Welt.

---

## M04 — #3 Sidecar Pilot

- generisches Sidecar Framework;
- Lazy Allocation;
- Lazy Destruction;
- Orientation als erster Pilot;
- Tests für leer/belegt;
- Serialisierungs-/Versionsmetadaten vorbereiten.

Temperature/Damage erst nach bewiesenem Orientation-Pilot.

---

## M05 — #3 Unified World State

- `get`;
- `has`;
- `set`;
- Prototype defaults;
- Sidecar Resolver;
- zentrale Blockmutation;
- Dirty Hooks;
- Mesh-/Neighbor-Invalidation;
- Persistence-Dirty-Abstraktion ohne RocksDB.

Harte Regel:

Lua/Gamecode darf nicht wissen, ob ein Wert aus Prototype, Sidecar oder später ECS kommt.

---

## M06 — #3 Communication Foundation + Minimal Actions + #18 Player Interaction

### Harte Kommunikationsarchitektur ab M06

M06 beginnt **nicht** mit einem isolierten Action-System, das M07 oder M10 später durch einen anderen Bus ersetzt.

Ab M06 gilt ein gemeinsamer transportierbarer Kommunikationsvertrag für:

- Commands;
- Queries;
- Events;
- Replies;
- Input/Player Actions;
- Lua;
- UI;
- Jolt/Physics;
- interne Worker/Systeme;
- später #13 Client/Server.

Die öffentliche Semantik muss als First-Class-Nachrichtenobjekt modelliert werden, konzeptionell mindestens:

```text
CommunicationEnvelope
{
    message_id
    kind            // Command | Query | Event | Reply

    sender
    receiver

    context
    action
    target

    payload

    reply_to
    correlation_id
}
```

Zusätzliche Metadaten dürfen ergänzt werden, wenn sie einen klaren generischen Zweck besitzen, z. B. Trace-/Session-/Authorization-Kontext. Keine content-spezifischen Felder in den Core-Umschlag einbauen.

### Feldsemantik

`sender`

- Identität des logischen Auslösers/Absenders;
- nicht zwangsläufig identisch mit `target`;
- muss für Logging, Rechte, Tracing und später Netzwerkbetrieb erhalten bleiben.

`receiver`

- konkretes System/Objekt **oder** logische Adresse/Capability/Scope;
- muss gerichtete Kommunikation ermöglichen;
- Broadcast ist ein Routingmodus, nicht der einzige Busmodus.

`context`

- semantischer Zusammenhang der Nachricht, z. B. `interaction`, `physics`, `inventory`, `simulation`;
- als stabile namespaced ID definieren, Runtime-Mapping auf kompakte IDs erlaubt.

`action`

- Verb/Operation, z. B. `block.break`, `block.place`, `damage.apply`;
- stabile namespaced ID, nicht C++-Enum aus Content-Namen;
- `context + action` ergänzt die Semantik, ersetzt aber nicht die Registry-/Schema-Validierung.

`target`

- generische Zielreferenz;
- für M06 mindestens kompatibel mit dem vorhandenen `WorldObjectRef` / `BlockAddress`;
- später erweiterbar auf Entity/System/Inventory/Scope, ohne einen zweiten Bus einzuführen.

`message_id`

- eindeutige Identität einer Nachricht innerhalb des relevanten Runtime-/Session-Kontexts.

`correlation_id`

- verbindet Reply/Folgekommunikation mit der ursprünglichen Nachricht.

`reply_to`

- logische Antwortadresse;
- darf **kein** roher C++-Funktionspointer sein.

### Callback-Regel

Ein bequemer InProcess-Aufruf wie:

```text
send(message, callback)
```

ist als lokaler Adapter zulässig.

Der öffentliche/transportierbare Vertrag darf aber **keinen echten Funktionscallback voraussetzen**. Intern muss Callback-Semantik auf Reply/Correlation abbildbar bleiben.

Grund:

> Was in einer Binary funktioniert, muss später auch über Loopback, Netzwerk oder Worker-Grenzen transportierbar sein.

Lua-`luaL_ref`-Handles aus #7 sind ebenfalls **lokale Runtime-Adapter** und niemals Teil des transportierbaren Communication-Envelopes.

### Eine Semantik, mehrere Dispatch-Wege

Performance-Optimierungen dürfen später unterschiedliche interne Dispatch-Wege besitzen:

- direkter synchroner InProcess-Dispatch;
- bounded Queue;
- Worker Queue;
- Loopback Transport;
- Network Transport.

Aber alle müssen denselben logischen Envelope und dieselben Action-/Signal-/Reply-Semantiken beobachten.

**Transport ist Implementierung. Kommunikation ist Vertrag.**

### Minimaler M06-Scope

M06 implementiert nur so viel Communication Foundation, wie für zwei echte Commands nötig ist:

- `place_block`;
- `remove_block`;
- `Command`-Kind;
- Message ID;
- Sender;
- Receiver;
- Context;
- Action;
- Target;
- typed Payload;
- Result/Reply-Grundlage;
- Correlation-Grundlage;
- Handler Registry;
- Validation.

Noch **kein** vollständiger Event-/Broadcast-Kosmos auf Vorrat.

Dann #18:

- Voxel Raycast;
- Hit Block;
- Face;
- Adjacent Position;
- Chunkgrenzen;
- Block entfernen;
- Block setzen;
- minimale Creative-Auswahl.

Pflichtpfad:

```text
Input
-> Raycast
-> CommunicationEnvelope(kind=Command)
-> Router/Handler
-> World State
-> Dirty/Invalidation
-> sichtbare Änderung
-> optional Reply mit correlation_id
```

Pflichtbeweis M06:

1. `place_block` und `remove_block` laufen als Commands durch denselben Envelope-Vertrag;
2. Sender/Receiver/Context/Action/Target sind im Test beobachtbar;
3. mindestens ein erfolgreicher und ein abgelehnter/ungültiger Command liefern ein Resultat über denselben Reply-/Result-Vertrag;
4. kein Input-Code mutiert `ChunkManager` direkt;
5. kein zweiter Spezialpfad für lokalen Singleplayer entsteht.

Wenn dafür Input direkt den ChunkManager mutieren muss oder ein separater Action-Umschlag entsteht, der später nicht als Bus-/Network-Nachricht taugt:

**STOP. #3 korrigieren.**

---

## M07 — #3 Communication Router / Events + #7 Lua Callback Cache

M07 **generalisiert exakt die M06-Communication-Foundation**. Kein zweites Event-Objekt neben dem M06-Command-Objekt einführen.

### #3 Communication Router / Event semantics

Erweitern um:

- `Event`-Kind;
- `Query`-Kind;
- `Reply`-Kind vollständig;
- Signal Registry;
- Slot/Receiver Registry;
- Action Registry generalisieren;
- Context Registry/IDs soweit nötig;
- typed Payload Schemas;
- Load-/Dispatch-Time Validation;
- Native Handler;
- Lua Handler;
- Receiver-/Capability-Routing;
- gerichtete Nachrichten;
- Broadcast/Spatial Scope als Routingmodus;
- bounded A/B Queues;
- Correlation/Reply Routing;
- Tracebarkeit von Message-Ketten;
- No-op erzeugt keinen unnötigen Folge-Event;
- Cactus Contact als zweiter Proof Case.

### Harte Bus-Regeln

Nicht ausreichend:

```text
event_id -> vector<callbacks>
```

Ein solcher Callback-Fanout darf intern als Optimierung existieren, ist aber **nicht** das öffentliche Kommunikationsmodell.

Jede routbare Kommunikation muss logisch mindestens beantworten können:

```text
WHO sent it?
WHO/WHAT should receive it?
IN WHICH context?
WHAT action/event/query is this?
WHAT is the target?
WHAT payload belongs to it?
IS a reply expected / what message does it correlate to?
```

Commands, Queries, Events und Replies dürfen unterschiedliche Validierungs-/Dispatchregeln besitzen, aber **keine getrennten inkompatiblen Nachrichtentypen/Busse**.

### M07 Proof Cases

#### A. Event

```text
sender   = physics:jolt
receiver = capability:contact
context  = physics
kind     = Event
action   = contact
target   = WorldObjectRef / EntityRef
payload  = contact data
```

Cactus-Verhalten wird anschließend über Registry/Slot/Action auf generischen Damage-Flow geroutet. Kein Cactus-Spezialfall im Bus.

#### B. Query + Reply

Mindestens ein kleiner synthetischer Query-Test muss zeigen:

```text
Query(message_id=X)
    -> handler
    -> Reply(correlation_id=X)
```

ohne Funktionspointer im transportierbaren Envelope.

#### C. Command bleibt kompatibel

Die bereits aus M06 funktionierenden `place_block`-/`remove_block`-Commands laufen nach der M07-Generalisation **unverändert weiter**.

### #7 Lua Callback Cache

- `luaL_ref` Callback Handles;
- RAII/Lifetime;
- `luaL_unref`;
- Hot Reload;
- stale-reference Tests;
- Fehlerkontext;
- Benchmark.

Wichtig:

`luaL_ref` ist eine lokale Implementierungsoptimierung für Lua-Handler. Es darf nicht zu einem `callback`-Pointer im Communication-Envelope werden.

Bytecode Cache optional.

---

## M08 — #3 Hot State / enTT

- enTT nur als aktive Projektion;
- ECS Mapping Sidecar;
- Promotion;
- Demotion;
- State Transfer;
- Cold → Warm → Hot;
- Hot → Warm;
- eine Furnace-/Testinstanz.

Pflichtbeweis:

Die öffentliche World-State-API bleibt vor/während/nach Promotion gleich.

---

## M09 — #3 RocksDB Persistence

- backend-neutrales Interface;
- Dirty Tracking;
- Serializer;
- Schema-Versionen;
- IO Queue;
- Worker;
- RocksDB;
- WriteBatch;
- stabile Keys;
- Recovery.

Pflichttest:

1. generierten Block entfernen;
2. Spielerblock setzen;
3. beenden;
4. neu starten;
5. Base World regenerieren;
6. Deltas anwenden;
7. beide Änderungen wieder vorhanden.

Gameplaycode kennt RocksDB nicht.

---

## M10 — #13 Embedded Client/Server

- GameServer owns authoritative World State;
- Server owns ECS;
- Server owns Persistence;
- ClientSession;
- ServerSession;
- Transport Interface;
- InProcess/Loopback;
- zuerst #18 `place_block`/`remove_block`;
- **denselben CommunicationEnvelope aus M06/M07 transportieren**;
- Sender/Receiver/Context/Action/Target/Payload/Message-ID/Correlation semantisch erhalten;
- Replies über denselben Reply-/Correlation-Vertrag zurückführen;
- alten direkten Übergang entfernen;
- Determinism-/Content-Handshake;
- Client rekonstruiert Base World;
- Server liefert Deltas.

Harte Regeln:

Standalone = Client + Embedded Server.

Kein anderer Gameplaypfad für Singleplayer.

#13 darf **keine zweite `NetworkMessage`-Semantik** erfinden, die Actions/Events in ein neues fachliches Nachrichtenmodell übersetzt. Der Transport darf serialisieren, rahmen, komprimieren, authentifizieren und zustellen. Die logische Kommunikation stammt aus M06/M07.

Pflichtbeweis:

```text
M06/M07 CommunicationEnvelope
-> InProcess/Loopback Transport
-> GameServer Router/Handler
-> Reply/Event zurück über denselben Vertrag
```

Ein Wechsel von InProcess zu echtem Netzwerk darf keine Gameplay-Action-API neu definieren müssen.

---

# CONSTRUCTION SPINE

## M11 — #16 Construction Foundation

- `get`;
- `set`;
- `setBulk`;
- Bulk Dirty/Invalidation;
- Chunkgrenzen;
- logische Properties;
- Lua `draw`;
- `fill`;
- `floor`;
- `wall`;
- `line`;
- `box`;
- `hollowBox`;
- eine gekrümmte Form;
- Writer-Abstraktion.

Pflichtbeweis:

**100 × 100 Floor ohne 10.000 Lua → C++ Einzelcalls.**

Dann genau einen existierenden Consumer migrieren.

---

## M12 — #17 Construction Blueprints

- `BlueprintWriter` nutzt #16;
- keine zweite Geometry Engine;
- Blueprint ID/Anchor/Bounds;
- kompakte Shape-Repräsentation;
- A/B Selection;
- 100 × 100 Ghost Floor;
- missing/fulfilled/conflict;
- chunk-aware Ghost Rendering;
- Construction Job;
- Resource Requirements;
- bounded Task Decomposition;
- generischer Test Executor.

Keine Village-/Drone-KI im Blueprint-Core.

---

# 10. Parallele Lanes

Diese Issues können parallel zur Core-Spine bearbeitet werden, wenn ihre lokalen Voraussetzungen erfüllt sind.

Ein Agent muss trotzdem **einen begonnenen Milestone sauber abschließen und committen**, bevor er die Baustelle wechselt.

## P01 — #4 Render / Streaming / LOD

P01 behandelt **drei getrennte Zustandsfragen**, die nicht in einen einzigen Radius oder State zusammenfallen dürfen:

1. **Materialization State:** Wie vollständig ist generierter Content bereits verfügbar?
2. **Residency State:** Welche World-/Chunk-Daten bleiben im Speicher?
3. **Render LOD State:** Welche visuelle Repräsentation wird für die Kamera benötigt?

### Progressive Materialisierung

Ein Chunk darf sinnvoll sichtbar werden, bevor jedes späte Dekorationsdetail fertig ist.

Konzeptionell zulässige Stufen sind zum Beispiel:

```text
BASE_READY
-> STRUCTURES_READY
-> DECORATION_READY
```

Exakte Namen bleiben Implementierungsdetail.

Harte Regeln:

- Base Terrain / wesentliche Oberfläche hat Priorität vor später Kleindekoration;
- Terrain darf gerendert/angezeigt werden, während Bäume/Strukturen oder insbesondere Blumen/Gras/Detail noch nachlaufen;
- spätere Materialisierungsstufen müssen deterministisch bleiben;
- spätere Completion invalidiert nur die erforderlichen Chunks/Meshes/Metadaten;
- Render-Komfort darf keinen logisch ungültigen autoritativen Gameplay-State erzeugen;
- Materialization State und Render LOD State sind **verschiedene Verträge**;
- bei Backlog gilt: nützliche Terrain-Abdeckung vor kosmetischer Vollständigkeit.

### Sticky Residency / Hysterese

Der aktuelle Prototyp darf nicht dauerhaft bei `ein Radius lädt und derselbe Radius wirft sofort wieder weg` bleiben.

Mindestens gilt konzeptionell:

```text
load_radius < unload_radius
```

Ein fehlender Chunk wird innerhalb des Load-Radius zum Load-Kandidaten. Ein bereits residenter Chunk bleibt über diese Grenze hinaus sticky und wird erst außerhalb des größeren Keep-/Unload-Radius **evictable**.

Das Verlassen des Load-Radius allein darf einen residenten Chunk nicht sofort zerstören.

### Radius ist nicht Budget

Entfernung entscheidet, was aktuell gebraucht wird. Speicher-/Residency-Budget entscheidet, wie viel bereits geladenes Material behalten werden darf.

Ein Chunk außerhalb des Keep-/Unload-Radius wird zunächst **evictable**, nicht zwangsläufig sofort evicted.

Eviction darf unter anderem berücksichtigen:

- Distanz;
- Last-Use/Alter;
- Memory Pressure / Chunk-Budget;
- aktive Simulation/Physics-Relevanz;
- Dirty-/Persistence-Zustand;
- noch nützliche Render-Repräsentation.

Kernregel:

> **Nicht entladen, nur weil es möglich ist. Evicten, wenn Policy/Budget es verlangt.**

Dirty/authoritative Daten müssen vor destruktiver Eviction den normalen World-State-/Persistence-Vertrag einhalten.

### Degrade before disappear

Wo die jeweilige P01-Ausbaustufe es erlaubt, soll Entfernung vorzugsweise zu einer günstigeren Darstellung führen statt direkt von Vollchunk auf Nichts zu springen:

```text
LOD0/full
-> simplified LOD
-> coarse/surface representation
-> optional Far WorldGen Preview
-> no representation
```

Full voxel residency, Simulation Relevance, Render Representation und Persistence Lifetime sind getrennte Konzepte.

### Data-driven Tuning

Praktische Radien/Budgets/Thresholds dürfen nicht als neue Magic Constants auf dem aktuellen Radius `3` festgeschrieben werden.

Mindestens konzeptionell konfigurierbar halten:

- near/load radius;
- sticky/keep/unload radius;
- far LOD radius;
- max resident chunks und/oder Memory Budget;
- eviction grace time;
- Materialization-/Generation-Prioritäten/Budgets;
- Mesh-/Upload-/Render-Work-Budgets.

Exakte Settings-Namen sind Implementierungsdetail und müssen dem normalen Config-/Settings-Vertrag folgen.

### P01 Reihenfolge

1. Surface Metadata
2. camera-aware Queue
3. Progressive Materialization Contract / Priorisierung
4. Streaming Residency Hysteresis + data-driven Load/Unload/Keep Policy
5. LOD Meshes
6. LOD Transitions/Seams
7. Near/Far Residency Split
8. Visibility Metadata
9. Far Worldgen Preview
10. Diagnostics/Tuning für Materialization, Residency, Eviction und LOD

Ab M06 muss Player-Blockedit als Invalidation-/LOD-Regressionstest dienen.

Zusätzliche Pflichtbeweise für P01:

- schnelles `raus -> zurück` verursacht deutlich weniger Evict/Regenerate-Thrashing;
- ein residenter Chunk verschwindet nicht direkt beim Überschreiten des Load-Radius;
- Base Terrain kann sichtbar sein, obwohl Late Decoration noch pending ist;
- Materialization- und LOD-State sind im Test/Diagnostic getrennt beobachtbar;
- relevante Radien/Budgets sind data-driven;
- Altitude-/Fast-Flight fällt nicht allein wegen eines gemeinsamen Load/Unload-Radius in den bisherigen `3 Chunks -> nichts`-Flying-Islands-Effekt zurück.

---

## P02 — #11 Vegetation

Nach stabiler Worldgen-/Content-Basis:

1. Context Audit
2. generische Vegetation
3. Desert
4. Riparian
5. Underwater
6. Multi-Block
7. Balancing

#16 später wiederverwenden, wo sinnvoll.

---

## P03 — #6 RmlUi

RmlUi-Basis darf früh entstehen.

Gameplay-mutierende UI-Aktionen als Commands über den gemeinsamen M06/M07 CommunicationEnvelope und später über #13 Server Boundary.

Keine UI → ECS/Sidecar/World Direktmutation.

---

## P04 — #9 Audio

1. Abstraction
2. Profiles
3. 3D
4. Loops/Buses
5. Doppler
6. Environment
7. Limits

#18 `place`/`break` sind ideale reale Event-Consumer.

---

## P05 — #10 Celestial

1. WorldClock
2. Body Registry
3. Orbit
4. Sky Space
5. Lighting
6. Moon Phases
7. Multiple Bodies
8. Seasons

Nach M10 wird Shared World Time server-authoritativ.

---

## P06 — #12 Fluids

Nicht vor brauchbaren:

- Sidecars;
- Unified World State;
- bounded Queues;
- Persistence-Abstraktion.

Reihenfolge:

1. Fluid State
2. Gravity
3. Horizontal Balance
4. `delta <= 1/8 = REST`
5. bounded A/B Queues
6. Overfill
7. Chunk Boundaries
8. Persistence
9. Rendering
10. Mod-defined Fluid

Keine eigene Fluid-DB.
Keine ECS-Entity pro Fluidzelle.

---

## P07 — #5 Villages

Reihenfolge:

1. Anchor
2. Terrain Query
3. VillagePlan
4. Roads/Plots
5. Procedural Building über #16
6. Cross Chunk
7. Version/Persistence
8. Aggregate Society
9. Hot NPCs
10. Jobs über #17

Keine Village-eigene Geometry Engine.

---

## P08 — #19 Jolt Physics Foundation

Physics ist eine eigene Engine-Säule, aber muss die bestehende World-/Communication-Spine konsumieren.

Reihenfolge:

1. Physics Contracts / lokaler Physics Anchor;
2. data-driven `collision_enabled` + Physics-Material Defaults;
3. sparse Sidecar Overrides nur bei Abweichung vom Prototype;
4. renderer-unabhängiger Physics Greedy Mesher;
5. statische Chunk-Collider / Jolt Residency;
6. Character Controller / Grounding / Gravity;
7. Pflichtbeweis: Block unter Spieler via #18 entfernen -> Collider invalid -> Support weg -> Spieler fällt;
8. Friction/Restitution Proof Cases;
9. Jolt Contact -> **M07 CommunicationEnvelope/Event Router**;
10. enTT <-> Jolt Dynamic-Body Lifecycle nach brauchbarem M08.

Harte Regeln:

- kein Jolt Body pro Terrain-Voxel;
- Jolt ist nicht authoritative World State;
- keine Ogre-Mesh-Abhängigkeit im Physics Core;
- keine astronomischen absoluten Koordinaten direkt in Jolt;
- keine C++-Spezialfälle für Ice/Slime/Cactus;
- Contact Events nutzen M07, kein zweiter Physics-Eventbus.

---

# 11. Harte STOP-Bedingungen

Der Agent muss die Architektur korrigieren, wenn einer dieser Fälle entsteht:

- Lua braucht rohe Sidecars;
- Lua muss wissen, ob ein Objekt ECS-backed ist;
- UI mutiert World/ECS/Sidecars direkt;
- Input mutiert `ChunkManager` direkt;
- M06 baut einen Action-Umschlag, der nicht als gemeinsamer CommunicationEnvelope für M07/M10 taugt;
- M07 baut nur `event_id -> vector<callbacks>` und verliert Sender/Receiver/Context/Target/Reply-Semantik;
- M07 führt einen zweiten inkompatiblen Event-Nachrichtentyp neben M06 Commands ein;
- ein transportierbarer Message-Vertrag enthält rohe Funktionspointer/`std::function`/`luaL_ref`;
- M10 erfindet eine fachlich neue NetworkMessage-Semantik statt den M06/M07-Envelope zu transportieren;
- Physics/Jolt erfindet einen eigenen Gameplay-/Contact-Bus neben M07;
- Construction und Blueprint besitzen verschiedene Shape-Libraries;
- Village erfindet eine dritte Shape-Library;
- Fluid erfindet eine zweite Queue;
- Fluid erfindet eigene Persistence;
- P01 koppelt Laden und sofortige Eviction dauerhaft an denselben Radius;
- P01 blockiert Base-Terrain-Sichtbarkeit unnötig auf Late Decoration/Blumen/Detail;
- P01 verschmilzt Materialization State und Render LOD State zu einem einzigen Vertrag;
- P01 hardcodiert die aktuelle `3 Chunks`-Prototype-Grenze statt data-driven Settings/Budgets zu verwenden;
- Client schreibt Saves;
- Client besitzt authoritative Mutable World State;
- Singleplayer umgeht den Server;
- Server benötigt OgreNext;
- Worldgen hängt von Thread-/Chunk-Reihenfolge ab;
- Default-Contentname wird C++-Gameplay-Spezialfall;
- der Patch wächst massiv über den Milestone-Scope hinaus;
- Architekturregeln sollen abgeschwächt werden, nur damit der Patch grün wird.

Dann gilt:

> **Nicht den Gate passend machen. Den Patch passend machen.**

---

# 12. Commit- und Milestone-Politik

## Jeder Milestone endet mit einem Commit

Keine Ausnahme.

Ein Milestone darf mehrere kleine vorbereitende Commits enthalten, aber es muss einen klaren Abschlusscommit geben, der den Milestone-Zustand repräsentiert.

## Empfohlene Commit-Konvention

```text
feat(scope): description (#issue)
fix(scope): description (#issue)
refactor(scope): description (#issue)
docs(scope): description (#issue)
test(scope): description (#issue)
```

Beispiele:

```text
feat(worldstate): add prototype registry foundation (#3)
feat(interaction): complete voxel place/remove slice (#18)
feat(persistence): persist world deltas through RocksDB backend (#3)
feat(server): route block actions through embedded server (#13)
```

## Keine Fake-Milestones

Kein Milestone-Commit, wenn:

- Build/Test-Gates nicht gelaufen sind;
- Dokumentation nicht synchron ist;
- Graphify veraltet ist;
- fremde Änderungen enthalten sind;
- der Agent nicht sagen kann, welche Acceptance Criteria erfüllt wurden.

---

# 13. GitHub-Kommunikation ist Teil der Arbeit

Zu jedem abgeschlossenen Milestone gehört ein GitHub-Update.

Der Agent dokumentiert im Issue:

```text
Milestone:
Commit:
Tests:
Architecture gate:
clang-tidy AST:
Build:
Graphify:
Docs updated:
Remaining work:
```

Wenn ein Issue vollständig abgeschlossen ist:

- Acceptance Criteria nochmal einzeln prüfen;
- erst dann schließen.

Wenn nur ein Teil abgeschlossen ist:

- Issue offen lassen;
- Milestone-Status kommentieren;
- nächsten offenen Teil benennen.

Master-Tracker #15 soll den realen Stand widerspiegeln.

---

# 14. Primus-impetus-Hauptstraße

```text
M00 Agent-/Doku-Baseline
  ↓
M01 Rename
  ↓
M02 Worldgen Baseline
  ↓
M03 Contracts + Prototypes
  ↓
M04 Sidecars
  ↓
M05 Unified World State
  ↓
M06 Communication Foundation + Commands + echte Blockinteraktion
  ↓
M07 Communication Router + Events/Queries/Replies + Lua Callback Cache
  ↓
M08 enTT Hot State
  ↓
M09 RocksDB Persistence
  ↓
M10 Embedded Client/Server
  ↓
M11 Construction Foundation
  ↓
M12 Construction Blueprints
```

Parallel, sofern Voraussetzungen erfüllt:

```text
#4 Render/LOD
#6 UI
#9 Audio
#10 Celestial
#11 Vegetation
#12 Fluids
#5 Villages
#19 Jolt Physics
```

---

# 15. Warum #18 mitten im Core-Pfad liegt

#18 ist der erste echte Architekturtest.

Sobald World State und die minimale M06-Communication-Foundation mit echten Commands stehen, muss ein Spieler wirklich:

- einen Block raycasten;
- ihn entfernen;
- einen anderen Block platzieren;
- eine sichtbare Meshänderung bekommen;
- Dirty State erzeugen.

Erst danach werden Communication Router/Eventsemantik, ECS, Persistence und Netzwerk weiter ausgebaut.

Wenn dieser kleine Pfad nicht sauber funktioniert, ist die Foundation nicht bewiesen.

Die Engine-Spine muss sich an echtem Gameplay bewähren, nicht nur an Interfaces und Unit-Tests.

---

# 16. Endzustand von Primus impetus

Primus impetus ist erfolgreich, wenn OmniGrid eine gemeinsame, bewiesene Spine besitzt:

- deterministischer Worldgen;
- stabile Prototypes/IDs;
- sparse Sidecars;
- eine Unified World State API;
- echte Player-Blockinteraktion über Commands im gemeinsamen CommunicationEnvelope;
- adressierbare Kommunikation mit Sender/Receiver/Context/Action/Target/Payload;
- Message-ID + Reply-/Correlation-Semantik ohne transportgebundene Funktionspointer;
- Commands/Queries/Events/Replies unter einem gemeinsamen Vertrag;
- zentrale Dirty-/Invalidation-Hooks;
- bounded Communication/Event Queues;
- Lua Callback Cache;
- enTT nur für Hot State;
- RocksDB Base+Delta Persistence;
- Embedded Server auch im Singleplayer;
- gemeinsame Construction-Geometrie;
- Blueprints ohne zweite Architektur;
- aktuelle Agent-/Architektur-/Statusdokumentation;
- aktueller Graphify Knowledge Graph;
- reproduzierbare grüne Git-Milestones;
- nachvollziehbare GitHub-Historie mit Issue ↔ Commit ↔ Milestone-Zuordnung.

> **Small patches. Hard boundaries. Real proof cases. Green tests. Updated docs. Updated graph. Commit every milestone. Document it on GitHub. No second architecture.**

---

# 17. Verbindlicher P01-Streaming-/Residency-Vertrag für die erste Welle (#4 / #20)

Diese Regeln konkretisieren P01 und sind für Implementierungen in der ersten Primus-Welle verbindlich. Die Details in GitHub #4 und #20 sind Teil der Arbeitsquelle und müssen vor P01-Arbeit vollständig gelesen werden.

## 17.1 Vier getrennte Achsen

Der Agent darf folgende Zustände nicht zu einem einzigen `loaded`/`unloaded`-Flag verschmelzen:

1. **Materialization:** Welche registrierten Worldgen-/Content-Pässe sind für den Bereich bereits materialisiert?
2. **World/Data Residency:** Welche logischen Chunk-/World-Daten müssen im RAM verfügbar bleiben?
3. **Simulation Residency:** Welche Bereiche/Systeme müssen weiter simuliert werden?
4. **Render Residency / LOD:** Welche visuelle Repräsentation muss CPU-/GPU-seitig existieren?

Harte Regel:

> **Nicht gerendert bedeutet nicht nicht simuliert.**

Eine entfernte Fabrik/Maschine darf weiterlaufen, obwohl Ogre-Meshes, SceneNodes, LOD0 und GPU-Vertices bereits freigegeben wurden. Umgekehrt darf Far-LOD sichtbar sein, ohne dort vollständige Gameplay-Simulation zu aktivieren.

Simulation kann erforderliche World/Data Residency nach sich ziehen. Sie erzwingt aber nicht automatisch Render Residency.

## 17.2 Jeder Worldgen-/Materialization-Pass besitzt eigene data-driven Streaming-Metadaten

Der C++-Core kennt keine fachlichen Passnamen wie `flowers`, `trees`, `villages`, `terrain` oder Mod-spezifische Inhalte.

Ein Pass wird über stabile namespaced ID und registrierte Metadaten beschrieben.

Jeder Pass muss konzeptionell mindestens separat konfigurieren können:

- `load_radius`;
- `keep_radius`;
- Scheduling-/Materialization-Priority;
- Retention-/Eviction-Policy;
- optional eine namespaced Profile-Referenz.

Grundinvariante:

```text
load_radius <= keep_radius
```

`load_radius` bestimmt, wann noch fehlende Pass-Arbeit eligible wird.

`keep_radius` ist im Normalfall eine **Schutzgrenze**, kein Löschbefehl. Verlässt ein bereits materialisierter Pass seinen Keep-Radius, wird er nur für normale Eviction freigegeben.

## 17.3 Explizite Hard-Eviction ist data-driven erlaubt

Ein Pass/Profil darf ausdrücklich verlangen, dass State/Repräsentation außerhalb des Keep-Radius sofort entfernt wird, wenn dies ein bewusstes Gameplay-/Visual-Feature ist, z. B. ein Fog-/Horror-Mod.

Das ist eine explizite Policy, nicht das Default-Verhalten.

Der Core interpretiert nur generische Policy-Werte. Verboten sind Content-Sonderfälle wie:

```text
if pass == flowers
if mod == fog_mod
```

## 17.4 Profile und Settings sind offen registrierbar

Streaming-/Performance-Profile dürfen **kein geschlossenes C++-Enum** sein.

Default Content darf Profile wie balanced/performance/quality mitbringen. Mods dürfen neue namespaced Profile hinzufügen.

Spieler/Server dürfen relevante Werte über den normalen Settings-/Config-Vertrag individuell überschreiben.

Daraus folgt für die Settings-Architektur:

- keine neuen festen C++-Member für jeden neuen Worldgen-Pass;
- generische namespaced Overrides für registrierte Pass-/Profil-Werte vorsehen;
- Content Defaults + Profile + User/Runtime Overrides deterministisch zu einem Effective Value auflösen;
- Schema/Validierung beibehalten.

Die exakte Merge-Syntax ist Implementierungsdetail, aber **User Override gewinnt am Ende** für erlaubte Performance-/Streaming-Einstellungen.

## 17.5 SLA-/Resource-Pressure ist data-driven

Normale Degradation/Eviction darf durch konfigurierbare Ziele/Budgets gesteuert werden, soweit die Plattform die Metrik verlässlich bereitstellt.

Mindestens als Architektur vorsehen:

- RAM / resident World-Data Budget;
- VRAM Budget;
- FPS bzw. Frame-Time-Ziel;
- GPU Utilization;
- CPU Utilization;
- Render-/Vertex-/Mesh-/GPU-Representation Budget.

Diese Werte gehören in registrierbare Profile und dürfen individuell überschrieben werden.

Ein einzelner schlechter Frame darf keine destruktive Eviction auslösen.

FPS/CPU/GPU-basierte Pressure-Entscheidungen brauchen:

- geglättete Messung / Zeitfenster;
- Grace Period;
- Trigger-Schwelle;
- getrennte Recovery-Schwelle;
- Recovery-Zeitfenster.

Verboten ist ein Oszillator nach dem Muster:

```text
14.9 FPS -> evict
16 FPS   -> reload
14.9 FPS -> evict
```

Ein generischer interner Pressure-State wie NORMAL/ELEVATED/HIGH/CRITICAL ist erlaubt; exakte Namen sind Implementierungsdetail.

## 17.6 Generation-, Residency- und Render-Budget sind getrennt

Der Agent darf nicht einen einzigen Budget-Regler für alle Ressourcen verwenden.

Mindestens unterscheiden:

- **Generation/Materialization Budget:** Wie viel neue Pass-Arbeit darf gestartet werden?
- **World/Data Residency Budget:** Wie viel bereits erzeugter logischer State darf resident bleiben?
- **Render/GPU Budget:** Wie viel Mesh/LOD/Vertex/GPU-Repräsentation darf resident bleiben?

Unter Druck soll eine sinnvolle Degradation möglich sein:

```text
neue niedrige Priorität drosseln
-> billigere Render-Repräsentation
-> evictable Low-Retention-State entfernen
-> weitere Stufen nach Policy
```

Keine hartcodierte Reihenfolge nach Contentnamen. Pass/Profile liefern generische Priorität, Kosten und Retention-Metadaten.

## 17.7 Hierarchische Sidecars sind die sparse World-Metadatenebene

Für Chunk-/ChunkGroup-/Section-/Region-/Sector-Metadaten gilt #20.

Wenn ein Zustand als sparse registrierte Property auf einer vorhandenen World-Hierarchieadresse ausdrückbar ist, darf P01 **nicht automatisch einen parallelen Spezialcontainer** erfinden.

Insbesondere keine neuen Strukturen nur aus Bequemlichkeit wie:

- `FactoryChunkState`;
- `PinnedChunkTable`;
- subsystem-spezifische Chunk-/Region-Flagstores;
- separate Residency-Metadatenbank.

Die generische Sidecar-Familie speichert typed Facts/Constraints. Scheduler/Services interpretieren sie.

**Sidecar ist State, nicht Scheduler.**

SLA-Berechnung, Eviction-Algorithmus, Renderplanung und Simulation Scheduling gehören nicht in den generischen Sidecar-Core.

## 17.8 Chunk ist expliziter Sidecar-Ziellevel

Neben bestehenden block-lokalen Sidecars müssen registrierte hierarchy-object Properties direkt auf `ChunkAddress` sowie auf ChunkGroup/Section/Region/Sector möglich sein.

Chunk-Level-State darf nicht über einen künstlich reservierten lokalen Blockindex emuliert werden.

Die gleichen Regeln gelten:

- sparse/lazy;
- Default-Schreiben entfernt expliziten State;
- typed Registry Validation;
- namespaced IDs;
- `persist`/Versioning;
- canonical hierarchy address, niemals global flatten.

## 17.9 Residency Constraints / Pins

Ein generischer Residency-Constraint kann z. B. World/Data oder Simulation als preferred/required markieren.

`required` bzw. ein äquivalenter Hard-Pin bedeutet:

> Normale Radius-/SLA-/Memory-/FPS-Eviction darf den dafür erforderlichen logischen State nicht entfernen, solange die Anforderung aktiv ist.

Render Residency bleibt separat degradierbar, sofern sie nicht selbst explizit constrained ist.

Mehrere Systeme können denselben Bereich gleichzeitig benötigen. Deshalb darf ein Hard-Pin **kein fragiles einzelnes Boolean** sein, das durch den ersten Release aller anderen Owner verloren geht.

Der konsumierende Residency-/Simulation-Service muss unabhängige Owner/Reason/Reference-Anforderungen sicher abbilden können. Die konkrete Token-/Refcount-/Owner-Repräsentation bleibt Implementierungsdetail.

Persistente Gameplay-Objekte sollen abgeleitete Runtime-Constraints bei Restore wiederherstellen, statt verwaiste ewige Pins zu hinterlassen.

## 17.10 Pflichtbeweise P01/#20

Mindestens folgende Fälle müssen bei Umsetzung testbar/diagnostizierbar werden:

- zwei registrierte Pässe besitzen unterschiedliche Load- und Keep-Radien ohne Contentwissen im C++-Scheduler;
- Verlassen des Keep-Radius macht normalen State evictable, löscht ihn bei gesunden Budgets aber nicht sofort;
- explizite Hard-Eviction-Policy funktioniert data-driven;
- Custom-Mod-Profil kann ohne neuen C++-Settings-Member registriert werden;
- User Override verändert Effective Pass-/Profil-Werte deterministisch;
- anhaltender FPS-/Resource-Pressure degradiert erst nach Hysterese/Grace und flattet nicht frameweise;
- Recovery besitzt getrennte Schwelle/Zeit und erzeugt kein Reload-Thrashing;
- Render Residency kann verschwinden, während required Simulation aktiv bleibt;
- Chunk-/höhere Residency-Constraints laufen über #20 Sidecars statt parallelem Metadatensilo;
- Chunk-Level-Property und block-lokale Property koexistieren eindeutig;
- zwei unabhängige Residency-Owner verhindern, dass das Freigeben nur eines Owners den Bereich evictable macht.

## 17.11 Zusätzliche STOP-Bedingungen für P01/#20

**STOP und Architektur korrigieren**, wenn:

- P01 einen fachlichen Passnamen im generischen C++-Scheduler special-cased;
- Load-/Keep-Radien global hart verdrahtet werden, obwohl Pass-Metadaten vorgesehen sind;
- Keep-Radius wieder als zwingender Unload-Befehl implementiert wird, ohne explizite Hard-Eviction-Policy;
- FPS/CPU/GPU-Metrik ohne Hysterese/Grace direkt Evict/Reload triggert;
- Profile als geschlossenes C++-Enum implementiert werden;
- Settings für jeden Mod-Pass neue feste C++-Felder benötigen;
- `not rendered` mit `not simulated` gleichgesetzt wird;
- Simulation Residency automatisch volle Ogre-/LOD0-Residency erzwingt;
- ein neuer per-Chunk/per-Group Metadata Store entsteht, obwohl #20 Sidecars denselben sparse State tragen können;
- Sidecar-Core SLA-, Factory-, Portal-, Render- oder Simulation-Semantik verstehen soll;
- ein einzelnes Boolean einen Multi-Owner-Hard-Pin repräsentiert und ein Owner fremde Requirements löschen kann.

Dann gilt auch hier:

> **Nicht den Sonderfall passend machen. Die generische Grenze passend machen.**
