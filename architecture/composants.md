# Architecture technique

## 1. Composants

### Serveur ETL

| | |
|---|---|
| OS | Linux (Red Hat Enterprise Linux) |
| Oracle | 19c Enterprise Edition |
| Binaire métier | Programme C++ (OCCI 19c) |
| Scripts | Script d'orchestration principal + script d'export secondaire |

### Base Oracle

| | |
|---|---|
| Listener | Handler statique (SID) |
| Redo Logs | Dimensionnement adapté au volume transactionnel |
| Services | Instance principale + PDB auxiliaire |

### Schémas annualisés

Chaque année universitaire dispose de son propre schéma :

- **Schéma commun** — porte les tables de référence partagées (table de pilotage des projets, table de correspondance patrimoniale)
- **Schéma N-1** — année précédente, conservée en historique
- **Schéma N** — année en cours

Chaque schéma contient :
- Tables d'extraction (une par source)
- Tables de travail (préfixe dédié, isolées de la production)
- Tables de production (résultat final)
- Chaîne de procédures PL/SQL (8 étapes)
- Tables temporaires de calcul intermédiaire

### Connexion aux sources

Le système consolide trois sources hétérogènes via des liens de base de données Oracle :

| Source | Contenu | Clé de jointure |
|---|---|---|
| Emploi du temps | Activités pédagogiques, salles, créneaux | Identifiant d'activité |
| Scolarité | Étapes et codes de formation | Code étape |
| RH / Prévisionnel | Enseignants, contrats, corps | Code étape |

---

## 2. Flux applicatifs

### Extraction (C++)

Le binaire d'extraction (C++/OCCI) :
1. Se connecte aux 3 sources via liens de base de données Oracle
2. Effectue les jointures entre les trois systèmes sources
3. Charge les résultats dans les tables d'extraction du schéma cible

### Transformation (PL/SQL)

Une procédure maître appelle séquentiellement les 8 sous-procédures qui transforment les données d'extraction en modèle relationnel final exploitable par le reporting.

### Consommation (Reporting)

Les outils de reporting se connectent en JDBC à l'instance Oracle.

Outils connectés :
- **ReportServer** (actuel) — rapports interactifs
- **OpenReport** (legacy) — anciens rapports encore utilisés

---

## 3. Table de référence des projets

Une table de pilotage contrôle l'exécution des traitements :

| Colonne | Rôle |
|---|---|
| Identifiant projet | Identifiant du traitement |
| Identifiant source | Identifiant côté système source |
| Schéma cible | Schéma de destination (ex: schéma de l'année en cours) |
| Indicateur d'activation | Active ou désactive le traitement |
| Horodatage début / fin | Suivi de l'exécution |

Les traitements se basent sur le dernier projet actif.
