# Chaîne de traitement PL/SQL — Détail des 8 procédures

## Vue d'ensemble

La procédure maître orchestre 8 étapes séquentielles. Chaque étape est
journalisée dans `UHA_ADESTATS_LOGS` (date + libellé d'opération).

```
PROC_MAITRE(PROJECTID, ADEPROJECTID)
│
├── Marque le début dans UHA_ADEPROJECTS
│
├── P001 — Purge des tables de travail
├── P002 — Ventilation extraction → entités
├── P003 — Enrichissement (effectifs, ABYLA)
├── P004 — Agrégation heures par type
├── P005 — Construction codes étape
├── P006 — Croisement RH + assemblage
├── P007 — Rapport final dénormalisé
├── P008 — Bascule _W → production
│
└── Marque la fin dans UHA_ADEPROJECTS
```

---

## P001 — Purge des tables de travail

**Rôle :** Remettre à zéro toutes les tables suffixées `_W` avant traitement.

**Pattern :**
1. Désactiver les 17 contraintes FK (EXECUTE IMMEDIATE)
2. TRUNCATE de 12 tables _W
3. Réactiver les contraintes FK
4. Configurer le SORT_AREA_SIZE

**Tables purgées :** ACTIVITIES_W, ACTIVITIES_CLASSROOM_W,
ACTIVITIES_CODES_W, ACTIVITIES_STUDENTS_GROUPS_W,
ACTIVITIES_TEACHERS_W, CLASSROOM_W, STUDENTS_GROUPS_W,
TEACHERS_W, TEACHERS_ACTIVITIES_LIST_W, TEACHERS_ACTIVITIES_SUM_W,
TEACHERS_CHARACTERISTICS_W, TEACHERS_DEP_LIST_W

---

## P002 — Ventilation extraction → entités

**Rôle :** Dispatcher les données brutes d'extraction (une table plate)
vers les tables relationnelles par type d'entité.

**Types d'entité** (ENTITY_TYPE_ID) :
- 1 → Groupes étudiants
- 2 → Enseignants
- 3 → Salles
- 5 → Étudiants

**Opérations :**
- INSERT DISTINCT des activités
- INSERT DISTINCT des enseignants (ENTITY_TYPE_ID = 2)
- INSERT DISTINCT des liens activités ↔ enseignants (IS_COURSEMEMBER > 0)
- INSERT DISTINCT des groupes étudiants (type 1 ou 5)
- INSERT DISTINCT des salles (type 3)
- INSERT DISTINCT des caractéristiques enseignants

**Correction notable (bug hérité) :**
Le MERGE d'origine pour les `IS_COURSEMEMBER` reposait sur le comptage
des types d'entités distincts (HAVING COUNT(DISTINCT ENTITY_TYPE_ID) = 2).
Cette logique échouait quand les groupes étaient regroupés dans un seul
dossier → les enseignants étaient ignorés.

Correction : détection des enseignants marqués course member, puis
propagation aux groupes associés au même événement.

---

## P003 — Enrichissement

**Rôle :** Compléter les données avec des comptages et des codes externes.

**3 curseurs :**
- Effectifs et comptage des groupes étudiants par événement
- Comptage des enseignants par événement
- Correspondance salles ADE ↔ codes ABYLA (patrimoine immobilier)

---

## P004 — Agrégation heures par type

**Rôle :** Calculer le total d'heures par enseignant et par type d'activité.

**6 curseurs** identiques (un par type) : CI, PROJET, CM, TD, TP, CONF

Chaque curseur fait un SUM(EVENT_DURATION) groupé par TEACHER_ID
pour le type d'activité correspondant.

---

## P005 — Construction des codes étape

**Rôle :** Construire les listes de codes étape (APOGEE) associés à
chaque activité, avec comptage et effectifs.

**Tables temporaires utilisées :**
- `P004_TEMP__CODES_COUNT` — nombre de codes par activité
- `P004_TEMP__CODE_SIZE` — effectif par code et activité
- `P004_TEMP__CODES_SIZES` — effectif total par activité
- `P004_TEMP__CODES_LIST` — liste concaténée (LISTAGG)
- `P004_TEMP__GROUPS_LIST` — liste des groupes (PATH)
- `P004_TEMP__GROUPS_FULL_NAMES_LIST` — noms complets

---

## P006 — Croisement RH + assemblage

**Rôle :** Enrichir les données enseignants avec le référentiel RH
(corps, type de contrat) et construire le rapport de base.

**Opérations :**
1. Chargement des infos RH (Cocktail) dans table temporaire
2. Assemblage du rapport : activités + codes + enseignants
3. Construction de la liste des départements par enseignant
4. Application des coefficients équivalent TD :
   - CM × 1.5
   - CI × 1.25
   - TD × 1.0
   - TP / 1.5
5. MERGE des caractéristiques (nommé, solde MEP, encadrement)
6. MERGE des infos corps depuis Cocktail

---

## P007 — Rapport final dénormalisé

**Rôle :** Assembler le rapport final (`REPORT_01_W`) en joignant
toutes les données transformées.

**Opérations :**
1. Construction de la table temporaire des activités (avec découpage nom/prénom par REGEX)
2. Construction de la table temporaire des départements
3. INSERT final dans REPORT_01_W (jointure des deux)
4. Calcul des heures ventilées par effectif :
   `ROUND(STUDENT_COUNT * HEURES_TYPE / STUDENT_COUNT_SUM, 2)`
5. MERGE des noms de salles et codes ABYLA (LISTAGG)

---

## P008 — Bascule _W → production

**Rôle :** Copier les tables de travail vers les tables de production.

**Pattern :**
1. Désactiver les 17 contraintes FK
2. Pour chaque table : DELETE du projet + INSERT SELECT depuis _W
3. COMMIT
4. Réactiver les contraintes FK

**12 tables basculées** : ACTIVITIES, ACTIVITIES_CLASSROOM,
ACTIVITIES_CODES, ACTIVITIES_STUDENTS_GROUPS, ACTIVITIES_TEACHERS,
CLASSROOM, STUDENTS_GROUPS, TEACHERS, TEACHERS_ACTIVITIES_LIST,
TEACHERS_ACTIVITIES_SUM, TEACHERS_CHARACTERISTICS, TEACHERS_DEP_LIST,
REPORT_01
