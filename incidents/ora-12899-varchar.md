# Post-mortem — ORA-12899 : désalignement VARCHAR2 (BYTE vs CHAR)

## Résumé

Les procédures d'agrégation (`PROC(7,7)`) échouaient avec
`ORA-12899: valeur trop grande pour la colonne` sur des libellés
d'activité de 249 caractères, alors que la limite ADE est de 250.

**Cause racine :** les colonnes de la chaîne statistique étaient
définies en `VARCHAR2(200 BYTE)` alors que la source autorise
jusqu'à 250 caractères (pouvant dépasser 200 bytes en UTF-8).

**Impact :** arrêt de la mise à jour des rapports pendant la
durée de la correction.

---

## Diagnostic

### Requête d'identification

```sql
-- Lister les libellés qui dépassent 200 caractères
SELECT activity_id,
       event_id,
       LENGTH(activity_name) AS len,
       activity_name
FROM EXTRACTION_ADE
WHERE LENGTH(activity_name) > 200
ORDER BY len DESC;

-- Longueur maximale observée
SELECT MAX(LENGTH(activity_name)) AS max_len
FROM EXTRACTION_ADE;
-- Résultat : 249
```

### Vérification de la définition des colonnes

```sql
SELECT table_name,
       column_name,
       data_type,
       data_length,
       char_used,    -- 'B' = BYTE, 'C' = CHAR
       char_length
FROM all_tab_columns
WHERE owner = 'SCHEMA_STATS'
  AND column_name LIKE '%NAME%'
ORDER BY table_name;
```

**Résultat :** colonnes en `VARCHAR2(200 BYTE)` → insuffisant.

---

## Corrections

Passage en `VARCHAR2(300 CHAR)` avec marge de sécurité :

```sql
-- Tables enseignants / activités
ALTER TABLE TEACHERS_ACTIVITIES_LIST
  MODIFY (ACTIVITY_NAME VARCHAR2(300 CHAR));

ALTER TABLE TEACHERS_ACTIVITIES_LIST_W
  MODIFY (ACTIVITY_NAME VARCHAR2(300 CHAR));

-- Table de travail (identifiant quoté → guillemets obligatoires)
ALTER TABLE P007_TEMP__ACTIVITIES_LIST
  MODIFY ("activite_description" VARCHAR2(300 CHAR));

-- Table de reporting
ALTER TABLE REPORT_01_W
  MODIFY (ACTIVITIES_DESCRIPTION VARCHAR2(300 CHAR));
```

### Point technique : BYTE vs CHAR

- `VARCHAR2(200 BYTE)` = 200 octets → avec UTF-8, un caractère accentué
  peut occuper 2-3 bytes → la capacité réelle en caractères est < 200.
- `VARCHAR2(300 CHAR)` = 300 caractères quelle que soit l'encodage.
- **Règle :** toujours utiliser `CHAR` pour les colonnes susceptibles
  de contenir des caractères non-ASCII.

---

## Méthode de résolution itérative

Ce type d'incident se manifeste en cascade : une fois la première
colonne corrigée, la suivante dans la chaîne de traitement peut
déclencher la même erreur. La méthode :

1. Relancer le traitement
2. Si ORA-12899 → identifier la table/colonne dans le message
3. Vérifier la définition (`BYTE` vs `CHAR`, longueur)
4. Corriger en `VARCHAR2(300 CHAR)`
5. Relancer — répéter jusqu'à succès complet

---

## Vérification post-correctif

```sql
-- Confirmer le passage en CHAR
SELECT table_name,
       column_name,
       char_used,
       char_length
FROM all_tab_columns
WHERE owner = 'SCHEMA_STATS'
  AND (LOWER(column_name) LIKE '%name%'
       OR LOWER(column_name) LIKE '%description%')
  AND char_used = 'C'
ORDER BY table_name, column_name;
```

---

## Enseignements

1. **BYTE vs CHAR** est un piège classique en Oracle avec UTF-8 —
   toujours spécifier `CHAR` pour les colonnes textuelles.
2. **Le défaut structurel peut rester invisible pendant des années**
   tant qu'aucune donnée réelle n'atteint la limite.
3. **Les identifiants quotés** (minuscules) dans Oracle nécessitent
   des guillemets dans les DDL — facile à oublier.
