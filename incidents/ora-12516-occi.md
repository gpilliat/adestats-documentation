-- ============================================================
-- INCIDENT : ORA-12516 intermittent (client OCCI)
-- Serveur  : Serveur ETL statistiques (Linux RHEL)
-- Période  : Janvier 2026
-- Statut   : Résolu (durcissement + observabilité)
-- ============================================================

# Post-mortem — ORA-12516 intermittent (client OCCI)

## Résumé

Le binaire C++ (compilé avec OCCI 19c) crashait de façon intermittente
avec `ORA-12516: listener could not find available handler` + core dump,
alors que `sqlplus` se connectait normalement au même instant.

**Cause racine :** mélange historique de librairies Oracle 19c et 21c
dans le `LD_LIBRARY_PATH`, provoquant un comportement erratique du
client OCCI selon le contexte d'exécution (cron vs shell interactif).

**Impact :** 3 échecs consécutifs du job cron nocturne, pas de mise
à jour des tableaux de bord pendant 24h.

---

## Symptôme

```
terminate called after throwing an instance of 'oracle::occi::SQLException'
  what(): ORA-12516: TNS:listener could not find available handler
          with matching protocol stack
Abandon (core dumped)
```

- 3 exécutions via cron → 3 crashes
- `sqlplus` fonctionne normalement au même moment
- Coredump non conservé (COREFILE=none dans la config système)

---

## Diagnostic différentiel

| Hypothèse | Vérifié | Résultat |
|---|---|---|
| Saturation processes/sessions Oracle | `V$RESOURCE_LIMIT` | OK à froid |
| Listener en erreur | `lsnrctl services` | READY, refused:0 |
| Entrée SID statique parasite | `listener.ora` | Bloc commenté (corrigé 08/01) |
| Shared server / dispatchers | `shared_servers=0` | Déjà désactivé |
| SERVICE_NAME incohérent | Vérifié | Cohérent (CDB) |
| **Runtime client : mélange 19c/21c** | `LD_DEBUG=libs` + `ldd` | **Traces de 21c dans les backups d'environnement** |

**Élément décisif :** `sqlplus` OK alors que le binaire OCCI échoue →
le problème n'est pas côté listener/instance, mais côté client/runtime.

---

## Cause racine détaillée

### Mélange de librairies Oracle

Des backups de `.bashrc` et de crontab contenaient des
`LD_LIBRARY_PATH` référençant à la fois :
- `/opt/oracle/product/19c/lib` (attendu)
- `/opt/oracle/product/21c/dbhome_1/lib` (parasite)

Pour un binaire OCCI compilé contre Oracle 19c, charger des
dépendances transversales depuis 21c provoque des comportements
erratiques — pas un crash systématique, mais des erreurs
intermittentes selon le contexte d'exécution.

### Pourquoi "intermittent" ?

Le chargeur dynamique Linux (`ld.so`) est **déterministe** : pour un
même environnement, il charge toujours les mêmes librairies.
L'"intermittence" venait des **variations de contexte** :

- Cron vs shell interactif → variables d'environnement différentes
- Utilisateur root vs oracle → profils différents
- Exports dans `.bashrc` vs `.bash_profile` → chargés ou non selon le type de session

---

## Remédiation

### 1. Nettoyage des sources d'environnement

```bash
# Vérifier et éliminer toute référence 21c
grep -R "21c/dbhome_1/lib" /home/oracle/.bash* /home/oracle/.profile
crontab -l -u oracle | grep -i LD_LIBRARY
grep -R "21c/dbhome_1/lib" /etc/ld.so.conf /etc/ld.so.conf.d
```

### 2. Script wrapper "safe-by-default"

Nouveau `run_stats.sh` avec :
- **Verrouillage strict** du `LD_LIBRARY_PATH` sur 19c uniquement
- **Retries** configurables (`MAX_TRIES=3`, `SLEEP_BETWEEN=30`)
- **Diagnostic opt-in** (désactivé par défaut, activable sur incident)
- **Mail de compte-rendu enrichi** incluant :
  - Contexte système (UID/GID, kernel)
  - `ulimit -a`
  - Variables Oracle + `LD_LIBRARY_PATH`
  - Checksum du script et du binaire
  - Extrait `ldd` (librairies Oracle chargées)
  - Extrait `lsnrctl services`

### 3. Prévention

- Isolation stricte des homes Oracle (pas de multi-versions dans PATH)
- Runbook incident ORA-12516 documenté avec checklist
- Activation du stockage des coredumps pour investigation post-mortem

---

## Validation

```bash
# 1. Vérifier l'environnement cron
crontab -l -u oracle | egrep "ORACLE_HOME|LD_LIBRARY_PATH|ORACLE_SID"

# 2. Vérifier les dépendances du binaire
ldd /home/oracle/bin/ade | egrep "occi|clntsh|nnz|clntshcore"
# Attendu : chemins 19c uniquement

# 3. Preuve forte : trace du chargeur dynamique
LD_DEBUG=libs /home/oracle/bin/ade stats 6 2> /tmp/ade.lddebug.log
grep "trying file=.*lib(clntsh|occi|nnz)" /tmp/ade.lddebug.log
# Attendu : uniquement /opt/oracle/product/19c/...

# 4. Test dry-run avec mail complet
DRY_RUN=1 /home/oracle/bin/run_stats.sh 6
# Attendu : mail de CR sans exécution réelle
```

---

## Chronologie

| Date | Action | Effet |
|---|---|---|
| Nov. 2025 | Installation Oracle 21c, ajout 21c dans env | Facteur de risque introduit |
| 08/01/2026 | Neutralisation SID statique dans listener.ora | Supprime entrées UNKNOWN |
| 15/01/2026 | Désactivation Shared Server | Sans effet sur le problème |
| 20/01/2026 | 3 échecs ORA-12516 via cron | Incident confirmé |
| 21/01/2026 | Validation libs = 19c + script safe-by-default | **Résolution + durcissement** |

---

## Enseignements

1. **Un `LD_LIBRARY_PATH` pollué peut provoquer des erreurs Oracle trompeuses** — l'ORA-12516 pointait vers le listener alors que le problème était côté client.
2. **Les jobs cron ont un environnement différent du shell interactif** — toujours forcer explicitement les variables d'environnement dans le wrapper.
3. **Documenter les diagnostics négatifs** (ce qui a été vérifié et écarté) est aussi important que documenter la cause trouvée.
4. **Sans coredump stocké, l'investigation post-mortem est très limitée** — activer le stockage dès la mise en production.
