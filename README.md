# ADESTATS — Documentation d'un pipeline ETL universitaire

## Contexte

**ADESTATS** est un pipeline de statistiques d'enseignement pour un établissement d'enseignement supérieur. Il extrait les données de planification (emplois du temps), les croise avec les référentiels de scolarité et de ressources humaines, puis alimente des tableaux de bord pour le pilotage.

J'ai repris la maintenance complète de ce système (code C++, PL/SQL, serveur Oracle, exploitation) sans aucune documentation existante. Ce dépôt rassemble la documentation que j'ai construite depuis la reprise, ainsi que des exemples de code anonymisés.

---

## Architecture

```
┌─────────────────────────────────┐
│       Sources de données        │
│                                 │
│  ┌───────────┐  ┌───────────┐   │
│  │    ADE    │  │  APOGEE   │   │
│  │ (emploi   │  │(scolarité)│   │
│  │ du temps) │  │           │   │
│  └─────┬─────┘  └─────┬─────┘   │
│        │              │         │
│  ┌─────┴──────────────┘         │
│  │  ┌───────────┐               │
│  │  │ COCKTAIL  │               │
│  │  │   (RH)    │               │
│  │  └─────┬─────┘               │
└──┼────────┼─────────────────────┘
   │        │
   ▼        ▼
┌─────────────────────────────────┐
│     Serveur ETL (Linux RHEL)    │
│                                 │
│  CRON → run_stats.sh            │
│           │                     │
│           ▼                     │
│  ┌─────────────────┐            │
│  │ Programme C++   │            │
│  │ (OCCI 19c)      │            │
│  │                 │            │
│  │ Jointures :     │            │
│  │ • ACTIVITY_ID   │            │
│  │ • COD_ETP       │            │
│  └────────┬────────┘            │
└───────────┼─────────────────────┘
            │
            ▼  Chargement via listener (SID statique)
┌─────────────────────────────────┐
│     Base Oracle 19c             │
│                                 │
│  ┌──────────────────────┐       │
│  │ Tables d'importation │       │
│  └──────────┬───────────┘       │
│             ▼                   │
│  ┌──────────────────────┐       │
│  │ 8 procédures PL/SQL  │       │
│  │ (chaîne séquentielle)│       │
│  └──────────┬───────────┘       │
│             ▼                   │
│  ┌──────────────────────┐       │
│  │ Modèle relationnel   │       │
│  │ final (reporting)    │       │
│  └──────────┬───────────┘       │
│             │                   │
│  Redo Logs (4 x 1 Go)          │
│  Schémas annualisés (_06, _07)  │
└─────────────┬───────────────────┘
              │
              ▼
┌─────────────────────────────────┐
│     Reporting                   │
│  ┌──────────┐ ┌──────────────┐  │
│  │OpenReport│ │ ReportServer │  │
│  │(legacy)  │ │  (actuel)    │  │
│  └──────────┘ └──────────────┘  │
└─────────────────────────────────┘
```

Voir le [diagramme détaillé en Mermaid →](diagrammes/architecture.md)

---

## Chaîne de traitement PL/SQL

Le pipeline PL/SQL s'exécute en 8 étapes séquentielles, orchestrées par une procédure maître. Chaque étape est journalisée dans une table de logs.

| Étape | Procédure | Rôle |
|---|---|---|
| 1 | `PROC_001` | Purge des tables de travail (_W), désactivation/réactivation des contraintes FK |
| 2 | `PROC_002` | Ventilation des données extraites → activités, enseignants, groupes, salles |
| 3 | `PROC_003` | Enrichissement : effectifs groupes, comptage enseignants, codes ABYLA (salles) |
| 4 | `PROC_004` | Agrégation des heures par type d'activité et par enseignant (CM, TD, TP, CI, CONF, PROJET) |
| 5 | `PROC_005` | Construction des codes étape : comptage, effectifs, listes (LISTAGG) |
| 6 | `PROC_006` | Croisement avec le référentiel RH (corps, type contrat), construction du rapport final |
| 7 | `PROC_007` | Assemblage final : rapport dénormalisé avec salles, codes ABYLA, effectifs ventilés |
| 8 | `PROC_008` | Bascule des tables de travail (_W) vers les tables de production |

---

## Contenu du dépôt

```
adestats-documentation/
├── README.md                          ← Ce fichier
├── architecture/
│   ├── composants.md                  ← VMs, Oracle, schémas, flux
│   ├── programme-cpp.md               ← Programme C++ ETL (OCCI, fork, mémoire partagée)
│   └── chaine-traitement.md           ← Détail des 8 procédures PL/SQL
├── diagrammes/
│   └── architecture.md                ← Diagrammes Mermaid (flux, chaîne, schémas)
├── plsql/
│   ├── proc_maitre.sql                ← Procédure d'orchestration
│   ├── proc_001_purge.sql             ← Purge tables de travail
│   ├── proc_002_ventilation.sql       ← Ventilation extraction → entités
│   ├── proc_004_agregation.sql        ← Agrégation heures par type
│   └── vue_report_enrichi.sql         ← Vue de reporting enrichie
├── exploitation/
│   ├── pilotage-cron.md               ← Script wrapper, planification
│   └── procedure-annuelle.md          ← Création des schémas ADESTATS_0x
├── incidents/
│   ├── ora-12516-occi.md              ← Post-mortem : conflit librairies 19c/21c
│   ├── ora-12899-varchar.md           ← Post-mortem : désalignement VARCHAR2
│   └── runbook-diagnostic.md          ← Arbre de décision
└── vues/
    └── vue_report_salles.sql          ← Vue enrichie avec statut qualité salles
```

---

## Points techniques notables

**Programme C++ avec multi-processus** — Le binaire ETL utilise `fork()` pour séparer l'extraction (processus enfant) de l'affichage de progression (processus parent), avec communication par mémoire partagée (`shmget`/`shmat`). Un verrouillage par `flock` empêche les exécutions concurrentes. Voir la [documentation détaillée](architecture/programme-cpp.md).

**Reprise sans documentation** — Le système a été développé par un prédécesseur, sans documentation technique ni fonctionnelle. J'ai reconstruit la compréhension du système par rétro-ingénierie du code C++ et PL/SQL, et créé l'intégralité de la documentation présente dans ce dépôt.

**Pipeline à 8 étapes avec pattern "tables de travail"** — Les procédures utilisent un pattern classique d'ETL : extraction dans des tables suffixées `_W` (work), transformation en place, puis bascule vers les tables de production. Les contraintes FK sont désactivées pendant le traitement pour la performance.

**Croisement de 3 sources hétérogènes** — Le programme C++ (OCCI) effectue des jointures entre les emplois du temps (ADE), la scolarité (APOGEE) et les ressources humaines (Cocktail) via des DB links Oracle.

**Correction de bugs hérités** — Plusieurs corrections documentées : logique de ventilation des `IS_COURSEMEMBER` (MERGE défaillant sur les cas limites), découpage nom/prénom par REGEX, désalignement VARCHAR2 BYTE vs CHAR.

**Schémas annualisés** — Chaque année universitaire dispose de son propre schéma Oracle (`ADESTATS_06`, `_07`...) avec ses procédures et tables. Un schéma commun (`ADESTATS`) contient le référentiel des projets.

---

## Contexte de maintenance

Ce système est en production et utilisé quotidiennement pour le pilotage des charges d'enseignement. La maintenance couvre :
- Le code PL/SQL (corrections, évolutions fonctionnelles)
- Le binaire C++ (compilation OCCI 19c, dépendances)
- Le serveur Oracle 19c (listener, redo logs, PDB, dimensionnement)
- Le système d'exploitation (RHEL, cron, ulimit, SELinux)
- L'intégration avec les outils de reporting (ReportServer, OpenReport)
