# Chaîne de traitement PL/SQL — Détail des 8 procédures

## Vue d'ensemble

La procédure maître orchestre 8 étapes séquentielles. Chaque étape est journalisée (date + libellé d'opération).

```
PROCÉDURE MAÎTRE
│
├── Marque le début du traitement
│
├── P001 — Purge des tables de travail
├── P002 — Ventilation extraction → entités
├── P003 — Enrichissement
├── P004 — Agrégation heures par type
├── P005 — Construction codes étape
├── P006 — Croisement RH + assemblage
├── P007 — Rapport final dénormalisé
├── P008 — Bascule tables de travail → production
│
└── Marque la fin du traitement
```

---

## P001 — Purge des tables de travail

**Rôle :** Remettre à zéro toutes les tables de travail avant traitement.

**Pattern :**
1. Désactiver temporairement les contraintes d'intégrité référentielle
2. Vider les tables de travail (une par type d'entité : activités, groupes, enseignants, salles)
3. Réactiver les contraintes
4. Ajuster les paramètres de tri pour la session

---

## P002 — Ventilation extraction → entités

**Rôle :** Dispatcher les données brutes d'extraction (une table plate) vers les tables relationnelles par type d'entité (groupes étudiants, enseignants, salles, étudiants).

**Opérations :**
- Insertion distincte des activités
- Insertion distincte des enseignants
- Insertion distincte des liens activités ↔ enseignants
- Insertion distincte des groupes étudiants
- Insertion distincte des salles
- Insertion distincte des caractéristiques enseignants

**Correction notable (bug hérité) :**
La logique d'origine pour rattacher les enseignants à une activité reposait sur un comptage de types d'entités distincts, qui échouait dans certains cas de regroupement. Correction : détection explicite des enseignants marqués comme intervenants, puis propagation aux groupes associés au même événement.

---

## P003 — Enrichissement

**Rôle :** Compléter les données avec des comptages et des codes de correspondance externes.

**Traitements :**
- Effectifs et comptage des groupes étudiants par événement
- Comptage des enseignants par événement
- Correspondance entre les salles et un référentiel patrimonial externe

---

## P004 — Agrégation heures par type

**Rôle :** Calculer le total d'heures par enseignant et par type d'activité pédagogique (cours magistraux, travaux dirigés, travaux pratiques, conférences, projets).

Chaque type fait l'objet d'une agrégation groupée par enseignant.

---

## P005 — Construction des codes étape

**Rôle :** Construire les listes de codes de formation associés à chaque activité, avec comptage et effectifs.

Utilise des tables temporaires de calcul pour le comptage, les effectifs et les listes concaténées.

---

## P006 — Croisement RH + assemblage

**Rôle :** Enrichir les données enseignants avec le référentiel RH (corps, type de contrat) et construire le rapport de base.

**Opérations :**
1. Chargement des informations RH en table temporaire
2. Assemblage du rapport : activités + codes + enseignants
3. Construction de la liste des départements par enseignant
4. Application de coefficients d'équivalence entre types d'enseignement (pondération standard utilisée dans le calcul de charge d'enseignement)
5. Rattachement des caractéristiques et informations de corps

---

## P007 — Rapport final dénormalisé

**Rôle :** Assembler le rapport final en joignant toutes les données transformées.

**Opérations :**
1. Construction de la table temporaire des activités
2. Construction de la table temporaire des départements
3. Insertion finale dans la table de rapport (jointure des deux)
4. Calcul des heures ventilées au prorata des effectifs
5. Rattachement des noms de salles et codes de correspondance patrimoniale

---

## P008 — Bascule vers la production

**Rôle :** Copier les tables de travail vers les tables de production.

**Pattern :**
1. Désactiver temporairement les contraintes d'intégrité
2. Pour chaque table : suppression des données du projet concerné, puis insertion depuis la table de travail
3. Validation de la transaction
4. Réactivation des contraintes
