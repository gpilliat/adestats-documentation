# Architecture ADESTATS

## Flux de données

```mermaid
graph TD
    subgraph SOURCES["Sources de Données"]
        ADE["🗓️ <b>Emploi du temps</b>"]
        APO["🎓 <b>Scolarité</b>"]
        CKT["👤 <b>RH / Prévisionnel</b>"]
    end

    subgraph ETL["Serveur ETL (Linux)"]
        CRON["⏰ Ordonnanceur"]
        WRAP["🔧 Script d'orchestration"]
        CPP["⬡ <b>Programme C++</b><br/>OCCI 19c<br/>───<br/>Jointures multi-sources"]
    end

    subgraph ORACLE["Base Oracle 19c"]
        LISTENER["🔌 Listener"]

        subgraph INSTANCE["Instance & Stockage"]
            IMPORT["📥 Tables<br/>d'importation"]
            PLSQL["⚙️ Procédures<br/>PL/SQL (×8)"]
            REDO["💾 Redo Logs"]
            MODEL["🏛️ <b>Modèle relationnel<br/>final</b>"]
        end
    end

    subgraph REPORT["Reporting & Sorties"]
        OR["📊 OpenReport<br/>(legacy)"]
        RS["📊 <b>ReportServer</b><br/>(actuel)"]
    end

    ADE --> CPP
    APO --> CPP
    CKT --> CPP

    CRON --> WRAP
    WRAP --> CPP

    CPP --> LISTENER
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

## Chaîne PL/SQL

```mermaid
graph LR
    M["<b>Procédure maître</b><br/>Orchestrateur"]
    P1["P001<br/>Purge"]
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

## Schémas annualisés

```mermaid
graph TD
    COMMON["<b>Schéma commun</b><br/>Tables de référence partagées"]

    S06["Schéma<br/>(année N-1)"]
    S07["Schéma<br/>(année N)"]
    S08["Schéma<br/>(à créer)"]

    COMMON --> S06
    COMMON --> S07
    COMMON -.-> S08

    style COMMON fill:#00BCD4,stroke:#00838F,color:#fff
    style S07 fill:#4CAF50,stroke:#2E7D32,color:#fff
    style S08 fill:#E0E0E0,stroke:#9E9E9E,color:#666
```
