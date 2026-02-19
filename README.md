# ADESTATS — Pipeline ETL de statistiques d'enseignement

Pipeline de statistiques d'enseignement pour un établissement d'enseignement supérieur (~15 000 étudiants, ~90 000 événements/an).  
Extraction des données de planification, croisement avec les référentiels de scolarité (Apogée) et de ressources humaines (Cocktail), alimentation des tableaux de bord décisionnels.

**Criticité :** ce pipeline est vital pour le pilotage institutionnel :

- Suivi des heures d'enseignement (CM, TD, TP)
- Analyse des taux d'occupation des salles
- Ventilation des charges par enseignant
- **Génération des rapports pour le calcul des fiches de paye des vacataires et des enseignants**

> Un arrêt de production impacte directement la Direction du Pilotage et les composantes de l'université.

---

## Architecture

Le système repose sur trois sources de données, un programme C++ d'extraction (OCCI / fork / mémoire partagée), une base Oracle 19c avec 8 procédures PL/SQL séquentielles, et une couche de reporting.

```mermaid
graph TD
    subgraph SOURCES["Sources de Données"]
        ADE["🗓️ <b>ADE</b><br/>Emploi du temps<br/>TBLADEACTIVITIES"]
        APO["🎓 <b>APOGEE</b><br/>Scolarité<br/>ETAPE @APO6"]
        CKT["👤 <b>COCKTAIL</b><br/>RH / Prévisionnel<br/>@GRHUM"]
    end

    subgraph ETL["Serveur ETL (Linux RHEL)"]
        CRON["⏰ CRON"]
        WRAP["🔧 run_stats.sh<br/>ulimit -n 65536"]
        CONF["📄 adestats.conf"]
        CPP["⬡ <b>Programme C++</b><br/>OCCI 19c<br/>───<br/>Jointures :<br/>• ACTIVITY_ID (ADE)<br/>• COD_ETP (APOGEE)<br/>• COD_ETP (COCKTAIL)"]
    end

    subgraph ORACLE["Base Oracle 19c"]
        LISTENER["🔌 Listener<br/>Handler statique (SID)"]
        subgraph INSTANCE["Instance & Stockage"]
            IMPORT["📥 Tables<br/>d'importation"]
            PLSQL["⚙️ Procédures<br/>PL/SQL (×8)"]
            REDO["💾 Redo Logs<br/>4 × 1 Go"]
            MODEL["🏛️ <b>Modèle relationnel<br/>final</b>"]
        end
    end

    subgraph REPORT["Reporting & Sorties"]
        OR["📊 OpenReport<br/>(legacy)"]
        RS["📊 <b>ReportServer</b><br/>(actuel)"]
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

> Détails : [Composants techniques](architecture/composants.md) · [Programme C++](architecture/programme-cpp.md)

---

## Chaîne de traitement PL/SQL

Le traitement est orchestré par une procédure maître qui appelle 8 étapes séquentielles. Chaque étape est journalisée dans une table de logs dédiée.

```mermaid
graph LR
    M["<b>PROC_MAITRE</b><br/>Orchestrateur"]
    P1["P001<br/>Purge _W"]
    P2["P002<br/>Ventilation"]
    P3["P003<br/>Enrichissement"]
    P4["P004<br/>Agrégation<br/>heures"]
    P5["P005<br/>Codes étape"]
    P6["P006<br/>Croisement RH"]
    P7["P007<br/>Assemblage<br/>rapport"]
    P8["P008<br/>Bascule<br/>production"]

    M --> P1 --> P2 --> P3 --> P4 --> P5 --> P6 --> P7 --> P8

    style M fill:#FF9800,stroke:#E65100,color:#fff
    style P8 fill:#009688,stroke:#004D40,color:#fff
```

| Étape | Procédure | Rôle |
|:-----:|-----------|------|
| 1 | `PROC_001` | Purge des tables de travail (_W) et gestion des contraintes FK |
| 2 | `PROC_002` | Ventilation des données brutes : activités, enseignants, groupes, salles |
| 3 | `PROC_003` | Enrichissement : effectifs groupes, mapping codes salles ABYLA |
| 4 | `PROC_004` | Agrégation des volumes horaires par type (CM, TD, TP, CI, CONF, PROJET) |
| 5 | `PROC_005` | Construction des codes étape : effectifs et listage (LISTAGG) |
| 6 | `PROC_006` | Croisement RH (corps, contrat), coefficients équivalent TD (CM×1.5, TD×1.0, TP÷1.5) |
| 7 | `PROC_007` | Assemblage du rapport dénormalisé final (salles, ABYLA, effectifs ventilés) |
| 8 | `PROC_008` | Bascule tables de travail (_W) → tables de production |

> Détails : [Chaîne de traitement](architecture/chaine-traitement.md) · [Procédures PL/SQL](plsql/)

---

## Schémas annualisés

Les données sont historisées dans des schémas Oracle annuels. Un schéma commun porte les tables de référence partagées.

```mermaid
graph TD
    COMMON["<b>ADESTATS</b><br/>(schéma commun)<br/>UHA_ADEPROJECTS<br/>UHA_ABYLA"]
    S06["ADESTATS_06<br/>(année N-1)"]
    S07["ADESTATS_07<br/>(année N)"]
    S08["ADESTATS_08<br/>(à créer)"]

    COMMON --> S06
    COMMON --> S07
    COMMON -.-> S08

    style COMMON fill:#00BCD4,stroke:#00838F,color:#fff
    style S07 fill:#4CAF50,stroke:#2E7D32,color:#fff
    style S08 fill:#E0E0E0,stroke:#9E9E9E,color:#666
```

Chaque schéma annuel contient **12 tables de production**. La création d'un nouveau schéma suit la procédure documentée dans [exploitation](exploitation/).

---

## Volumétrie

| Indicateur | Valeur |
|------------|--------|
| Étudiants | ~15 000 |
| Événements planifiés | ~90 000 |
| Enseignants (RH) | ~1 500 |
| Salles référencées | 466 |
| Tables par schéma annuel | 12 |
| Fréquence d'exécution | Quotidienne (J+1) |

---

## Points techniques notables

- **Multi-processus C++** — `fork()` pour séparer extraction et affichage de progression, communication via mémoire partagée (`shmget`/`shmat`), concurrence via `flock`. Voir [programme-cpp.md](architecture/programme-cpp.md).
- **Pattern tables de travail** — Tables intermédiaires `_W` sécurisant les transformations avant bascule en production.
- **Jointures hétérogènes** — Croisement de 3 sources distinctes (ADE, Apogée, Cocktail) via DB links Oracle.
- **Corrections héritées** — Ventilation `IS_COURSEMEMBER`, optimisation REGEX, alignement types Oracle (BYTE vs CHAR).

---

## Incidents documentés

| Incident | Description |
|----------|-------------|
| [ORA-12516](incidents/ora-12516-occi.md) | Saturation du listener — trop de sessions OCCI simultanées |
| [ORA-12899](incidents/ora-12899-varchar.md) | Troncature VARCHAR2 — BYTE vs CHAR sur colonnes multi-octets |

---

## Contenu du dépôt

```
├── architecture/
│   ├── chaine-traitement.md    # Détail des 8 procédures PL/SQL
│   ├── composants.md           # VMs, schémas Oracle, binaire C++
│   └── programme-cpp.md        # Connexion OCCI, fork, mémoire partagée
├── incidents/
│   ├── ora-12516-occi.md       # Post-mortem saturation listener
│   └── ora-12899-varchar.md    # Post-mortem BYTE vs CHAR
├── plsql/                      # Procédures PL/SQL anonymisées
├── exploitation/               # Scripts cron, shell, configuration
├── vues/                       # Vues SQL pour la couche reporting
└── snippet_occi_fork.cpp       # Extrait C++ (OCCI + fork + shm)
```

---

## Contexte de maintenance

Ce système est en **production quotidienne**. La maintenance couvre le code PL/SQL, le binaire C++, le serveur Oracle 19c (listener, redo logs, dimensionnement), le système RHEL et l'intégration reporting (ReportServer, OpenReport).

Reprise de maintenance dans un contexte de documentation technique lacunaire — ce dépôt centralise l'effort de rétro-ingénierie réalisé pour documenter le fonctionnement interne et assurer la traçabilité des correctifs.
