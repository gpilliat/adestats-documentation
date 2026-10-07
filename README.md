# Migration JasperReports — OpenReport vers ReportServer

> Retour d'expérience sur la migration de rapports JasperReports complexes entre deux plateformes de reporting. Ce dépôt documente les incompatibilités découvertes, les solutions techniques mises en œuvre, et les pièges à éviter.

## Pourquoi ce dépôt

Dans le cadre du remplacement d'**OpenReport** (OR) par **ReportServer** (RS) à l'Université de Haute-Alsace, la migration des rapports SQL standards s'est faite sans difficulté — un simple copier-coller des requêtes suffit.

Les rapports **JasperReports** (.jrxml), en revanche, ont révélé une série d'incompatibilités profondes entre les deux plateformes. Ce qui fonctionnait parfaitement sous OpenReport depuis des années a nécessité une réécriture complète de l'architecture : nouveau code PL/SQL, modification des JRXML, et adaptation aux comportements spécifiques de ReportServer.

Ce dépôt capitalise sur ce travail pour servir de référence à toute équipe confrontée à une migration similaire.

## Le cas d'étude

Les rapports migrés sont des **états d'heures d'enseignement** (permanents et vacataires) qui cumulent toutes les difficultés possibles :

- **Schémas Oracle dynamiques** — les données sont partitionnées par année universitaire (`ADESTATS_07`, `ADESTATS_08`…), le nom du schéma est injecté à l'exécution
- **Cascade de paramètres à 6 niveaux** — Année → Schéma → UFR → Département → Dates → Enseignants
- **Fonctions PL/SQL pipelinées** — les listes de valeurs sont générées par des fonctions `TABLE(...)`
- **Multi-sélection** — passage de collections Java vers des clauses SQL `IN (...)`
- **Calculs croisés** — heures par composante, heures "autres composantes", équivalents TD

## Le problème fondamental

OpenReport et ReportServer ne gèrent pas le cycle de vie des paramètres de la même façon.

```
OpenReport
══════════
  Paramètres ──→ Résolution unique ──→ Injection JRXML ──→ Exécution Oracle
                  (tout en un pass)

ReportServer
════════════
  Phase 1 — Chargement interface
  Paramètres RS ──→ Moteur JUEL ${...} ──→ Requêtes Oracle (cascade OK)

  Phase 2 — Exécution du rapport
  RS ré-évalue les requêtes ──→ Moteur JUEL ${...} ──→ Scope vide
  ──→ PropertyNotFoundException ❌

  Phase 3 — Moteur JasperReports
  JRXML $P{...} / $P!{...} ──→ SQL Oracle
  (syntaxe différente, types différents)
```

OpenReport résolvait tout en un seul passage. ReportServer sépare le cycle en plusieurs phases avec **deux moteurs d'expression distincts** (JUEL pour RS, syntaxe Jasper pour le JRXML), et les paramètres en cascade ne survivent pas toujours à la transition.

Conséquence : il a fallu **déporter la logique métier** des requêtes JRXML vers des **fonctions Oracle dédiées**, réécrire les JRXML, et adapter la configuration des paramètres.

## Structure du dépôt

```
docs/
├── 01-pourquoi-ca-casse.md        Analyse des incompatibilités OR → RS
├── 02-guide-migration-jrxml.md    Procédure de migration d'un rapport Jasper
├── 03-solutions-techniques.md     Chaque problème rencontré et sa solution
├── 04-configuration-rs.md         Config des paramètres dans ReportServer
└── 05-suivi.md                    État d'avancement des rapports

sql/
├── types/                         Types Oracle (TYP_LOV_ROW, TYP_LOV_TAB)
├── functions/                     Fonctions PL/SQL créées pour la migration
└── utils/                         Debug et utilitaires

jrxml/
├── permanents/                    JRXML du rapport permanents
├── vacataires/                    JRXML du rapport vacataires
└── templates/                     JRXML de test minimal

config/
└── reportserver/
    ├── parametres-rs.md           Documentation des paramètres RS
    └── exports/                   Exports XML ReportServer (référence)
```

## Technologies

| Couche | Technologie | Rôle |
|---|---|---|
| Base de données | Oracle 19c, PL/SQL | Données, fonctions pipelinées, schémas dynamiques |
| Serveur de rapports | ReportServer 4.x | Gestion des paramètres, exécution, diffusion |
| Moteur de rendu | JasperReports | Compilation JRXML, génération PDF |
| Expressions RS | JUEL (Java EL) | Résolution des `${...}` dans les paramètres RS |
| Ancien serveur | OpenReport | Plateforme d'origine (en cours de décommission) |

## État du projet

| Rapport | JRXML | Paramètres RS | Fonctions Oracle | Exécution PDF |
|---|---|---|---|---|
| Permanents | ✅ Réécrit | ✅ Cascade OK | ✅ Déployées | ✅ Validé |
| Vacataires | ✅ Réécrit | ✅ Cascade OK | ✅ Déployées | ✅ Validé |

Les deux rapports sont **fonctionnels en production**. Ce dépôt documente l'ensemble des difficultés rencontrées et les solutions mises en œuvre pour y parvenir.

## Licence

Publié à des fins de démonstration et de partage d'expérience.
