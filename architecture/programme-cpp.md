# Programme C++ ETL — Extracteur multi-sources

## Objectif

Programme C++ qui automatise l'extraction et le chargement de données entre 3 bases Oracle :
- **ADE** (source) — données de planification / emplois du temps
- **COCKTAIL** (source) — données RH et étudiants
- **STATS** (destination) — schéma de statistiques

Le programme gère le cycle complet : connexion aux sources, extraction, chargement dans les tables d'importation, puis déclenchement des procédures PL/SQL de transformation.

---

## Architecture technique

```mermaid
graph TD
    subgraph SOURCES["Sources de Donnees"]
        ADE["<b>ADE</b><br/>Emploi du temps<br/>TBLADEACTIVITIES"]
        APO["<b>APOGEE</b><br/>Scolarite<br/>ETAPE @APO6"]
        CKT["<b>COCKTAIL</b><br/>RH / Previsionnel<br/>@GRHUM"]
    end

    subgraph ETL["Serveur ETL (Linux RHEL)"]
        CRON["CRON"]
        WRAP["run_stats.sh<br/>ulimit -n 65536"]
        CONF["adestats.conf"]
        CPP["<b>Programme C++</b><br/>OCCI 19c<br/>───<br/>Jointures :<br/>• ACTIVITY_ID (ADE)<br/>• COD_ETP (APOGEE)<br/>• COD_ETP (COCKTAIL)"]
    end

    subgraph ORACLE["Base Oracle 19c"]
        LISTENER["Listener<br/>Handler statique (SID)"]

        subgraph INSTANCE["Instance & Stockage"]
            IMPORT["Tables<br/>d'importation"]
            PLSQL["Procedures<br/>PL/SQL (x8)"]
            REDO["Redo Logs<br/>4 x 1 Go"]
            MODEL["<b>Modele relationnel<br/>final</b>"]
        end
    end

    subgraph REPORT["Reporting & Sorties"]
        OR["OpenReport<br/>(legacy)"]
        RS["<b>ReportServer</b><br/>(actuel)"]
    end

    ADE -->|"ACTIVITY_ID<br/>@ADEPROD6"| CPP
    APO -->|"COD_ETP<br/>@APO6"| CPP
    CKT -->|"COD_ETP<br/>@GRHUM"| CPP

    CRON --> WRAP
    CONF -.->|"Config"| CPP
    WRAP --> CPP

    CPP -->|"Chargement<br/>SID statique"| LISTENER
    LISTENER --> IMPORT
    IMPORT --> PLSQL
    PLSQL --> MODEL
    REDO -.->|"Journalisation"| MODEL

    MODEL --> OR
    MODEL --> RS

    style ADE fill:#4CAF50,stroke:#2E7D32,color:#fff
    style APO fill:#2196F3,stroke:#1565C0,color:#fff
    style CKT fill:#9C27B0,stroke:#6A1B9A,color:#fff
    style CPP fill:#F44336,stroke:#C62828,color:#fff
    style MODEL fill:#009688,stroke:#004D40,color:#fff
    style RS fill:#5C6BC0,stroke:#3949AB,color:#fff
    style OR fill:#3F51B5,stroke:#283593,color:#fff

```

---

## Composants

| Composant | Rôle |
|---|---|
| `ConfigLoader` | Charge la configuration depuis `/usr/local/etc/adestats.conf` (identifiants DB, chemins) |
| `sqlString` | Charge les scripts SQL externes, remplace les variables dynamiques (`{{PROJECTID}}`, etc.) |
| `ProgressBar` / `ade_bin_monitor` | Affichage de la progression dans le terminal (processus parent) |
| `log_to_file` | Journalisation horodatée de chaque étape |

---

## Mécanismes système

### Multi-processus (fork)

Le programme utilise `fork()` pour séparer :
- **Processus enfant** : exécute les extractions (tâche lourde, I/O Oracle)
- **Processus parent** : affiche la barre de progression

La communication entre les deux passe par un **segment de mémoire partagée** (`shmget` / `shmat`) qui transporte l'état d'avancement.

### Verrouillage d'instance

Utilisation de `flock` sur un fichier PID pour empêcher l'exécution simultanée de plusieurs instances. Ce mécanisme est critique car les procédures PL/SQL en aval ne supportent pas les exécutions concurrentes (TRUNCATE / INSERT sur les mêmes tables).

---

## Commandes

### `ade stats <n>` — Extraction et chargement

C'est le cœur du programme. Flux :

```mermaid
graph LR
    C1["Connexion<br/>3 bases"] --> C2["Projet actif<br/>EXTRACT_ENABLE = 1"]
    C2 --> C3["Extraction<br/>ADE"]
    C3 --> C4["Extraction<br/>COCKTAIL"]
    C4 --> C5["PL/SQL<br/>UHA_ADESTATS()"]

    style C1 fill:#42A5F5,stroke:#1565C0,color:#fff
    style C5 fill:#009688,stroke:#004D40,color:#fff
```

1. **Connexion** aux 3 bases (ADE, COCKTAIL, STATS)
2. **Identification du projet actif** dans `UHA_ADEPROJECTS` (`EXTRACT_ENABLE = 1`)
3. **Extractions en cascade** :
   - `EXTRACTION_ADE.sql` → `UHA_EXTRACTION_ADE`
   - Caractéristiques, entités, membres des cours
   - `EXTRACTION_COCKTAIL.sql` → `UHA_EXTRACTION_COCKTAIL`
4. **Déclenchement** de la procédure PL/SQL `UHA_ADESTATS(PROJECTID, ADEPROJECTID)`

### `ade exports` — Export pour listes de diffusion

Fonction secondaire :
1. Extrait les permissions utilisateurs depuis ADE
2. Génère un fichier texte (`import-ade-sympa.txt`) pour alimenter le gestionnaire de listes de diffusion (Sympa)

---

## Fichiers SQL requis

Le binaire s'appuie sur des scripts SQL externes chargés à l'exécution :

```
sql/
├── EXTRACTION_ADE.sql              ← Requête d'extraction ADE
├── EXTRACTION_ADE_import.sql       ← Chargement dans tables STATS
├── EXTRACTION_COCKTAIL.sql         ← Requête d'extraction RH
└── ...

scripts/
└── 04-01_ADESTATS_Create_Temp_Tables.sql  ← Initialisation tables temp
```

Les scripts contiennent des variables de substitution (`{{PROJECTID}}`, `{{ADEPROJECTID}}`, `{{SCHEMA}}`) remplacées à la volée par `sqlString`.

---

## Gestion des erreurs

- **Exceptions OCCI** : capturées (`SQLException`) et consignées dans le fichier de log avec le détail Oracle (code erreur, message)
- **Vérification des privilèges** : messages d'aide spécifiques si les droits sur `V$SESSION` / `V$PROCESS` sont insuffisants
- **Fichier PID** : si le verrou existe déjà, le programme refuse de démarrer et log l'information

---

## Compilation et dépendances

| | |
|---|---|
| Compilateur | g++ (version standard RHEL) |
| Librairie Oracle | OCCI 19c (`libocci.so.19.1`, `libclntsh.so.19.1`) |
| ORACLE_HOME | `/opt/oracle/product/19c` |
| Dépendances système | `libnnz19.so`, `libclntshcore.so.19.1` |

> **⚠️ Point critique :** le binaire DOIT être exécuté avec un `LD_LIBRARY_PATH` pointant exclusivement vers les librairies 19c. Un mélange avec d'autres versions (ex: 21c) provoque des erreurs intermittentes. Voir [incident ORA-12516](../incidents/ora-12516-occi.md).
