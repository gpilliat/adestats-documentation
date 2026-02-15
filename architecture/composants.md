# Architecture technique

## 1. Composants

### Serveur ETL

| | |
|---|---|
| OS | RHEL (Red Hat Enterprise Linux) |
| RAM | 16 Go |
| Oracle | 19c Enterprise Edition |
| SID | ADESTATS |
| Binaire métier | `/home/oracle/bin/ade` (C++, OCCI 19c) |
| Scripts | `run_stats.sh`, `import-sympa.sh` |

### Base Oracle

| | |
|---|---|
| Instance | ADESTATS (CDB) |
| Listener | TCP 1521, handler statique (SID) |
| Redo Logs | 4 × 1 Go |
| Services | ADESTATS (principal), PDB auxiliaire |

### Schémas annualisés

Chaque année universitaire dispose de son propre schéma :

- **`ADESTATS`** — schéma commun (table de référence `UHA_ADEPROJECTS`, table `UHA_ABYLA`)
- **`ADESTATS_06`** — année N-1
- **`ADESTATS_07`** — année N (en cours)

Chaque schéma contient :
- Tables d'extraction (`UHA_EXTRACTION_ADE`, `UHA_EXTRACTION_ADE_CHARACTERISTICS`, `UHA_EXTRACTION_COCKTAIL`)
- Tables de travail (suffixe `_W`)
- Tables de production (sans suffixe)
- Procédures PL/SQL (`UHA_ADESTATS_001` à `_008`)
- Tables temporaires (`UHA_P00x_TEMP__*`)

### Connexion aux sources

| Source | DB Link | Table/Vue principale | Clé de jointure |
|---|---|---|---|
| ADE (emploi du temps) | @ADEPROD6 | TBLADEACTIVITIES | ACTIVITY_ID |
| APOGEE (scolarité) | @APO6 | ETAPE | COD_ETP |
| COCKTAIL (RH) | @GRHUM | UHA_PREVISIONNEL | COD_ETP |

---

## 2. Flux applicatifs

### Extraction (C++)

Le binaire `ade` (C++/OCCI) :
1. Se connecte aux 3 sources via DB links Oracle
2. Effectue les jointures entre ADE, APOGEE et COCKTAIL
3. Charge les résultats dans les tables d'extraction du schéma cible

### Transformation (PL/SQL)

La procédure maître `UHA_ADESTATS(PROJECTID, ADEPROJECTID)` appelle
séquentiellement les 8 sous-procédures qui transforment les données
d'extraction en modèle relationnel final exploitable par le reporting.

### Consommation (Reporting)

Les outils de reporting se connectent en JDBC :
```
jdbc:oracle:thin:@serveur:1521:ADESTATS
```

Outils connectés :
- **ReportServer** (actuel) — rapports interactifs
- **OpenReport** (legacy) — anciens rapports encore utilisés

---

## 3. Table de référence des projets

La table `ADESTATS.UHA_ADEPROJECTS` contrôle l'exécution :

| Colonne | Rôle |
|---|---|
| `PROJECTID` | Identifiant du projet |
| `ADEPROJECTID` | Identifiant ADE associé |
| `SCHEMA` | Schéma cible (ex: ADESTATS_07) |
| `EXTRACT_ENABLE` | 0/1 — active le traitement |
| `EXTRATION_DATE_START` | Horodatage début extraction |
| `EXTRATION_DATE_END` | Horodatage fin extraction |

Les traitements se basent sur le dernier projet actif (`EXTRACT_ENABLE = 1`).
