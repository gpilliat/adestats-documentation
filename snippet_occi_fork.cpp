/**
 * snippet_occi_fork.cpp
 * 
 * Extrait anonymisé montrant les patterns C++ utilisés dans le binaire ETL :
 * - Connexion multi-bases Oracle via OCCI 19c
 * - Séparation extraction / affichage par fork()
 * - Communication inter-processus par mémoire partagée (shmget/shmat)
 * - Verrouillage d'instance par flock()
 * - Gestion des exceptions OCCI
 *
 * Ce fichier illustre le style de code et l'architecture,
 * pas le code de production (propriété de l'établissement).
 */

#include <iostream>
#include <fstream>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <occi.h>

using namespace oracle::occi;
using namespace std;

// ============================================================
// Structure de progression (mémoire partagée)
// ============================================================
struct SharedProgress {
    int    current_step;
    int    total_steps;
    char   step_label[128];
    bool   finished;
    bool   error;
};

// ============================================================
// Journalisation horodatée
// ============================================================
void log_to_file(const string& logpath, const string& message) {
    ofstream logfile(logpath, ios::app);
    if (logfile.is_open()) {
        time_t now = time(nullptr);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
        logfile << "[" << timestamp << "] " << message << endl;
    }
}

// ============================================================
// Verrouillage d'instance (empêche l'exécution simultanée)
// ============================================================
int acquire_lock(const string& pidfile) {
    int fd = open(pidfile.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) return -1;

    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        return -1;  // Une autre instance tourne déjà
    }

    // Écrire le PID courant
    string pid_str = to_string(getpid());
    write(fd, pid_str.c_str(), pid_str.size());
    return fd;
}

// ============================================================
// Connexion OCCI à une base Oracle
// ============================================================
Connection* connect_oracle(
    Environment* env,
    const string& user,
    const string& pass,
    const string& connstr,
    const string& logpath
) {
    Connection* conn = nullptr;
    try {
        conn = env->createConnection(user, pass, connstr);
        log_to_file(logpath, "Connexion OK : " + connstr);
    }
    catch (SQLException& ex) {
        log_to_file(logpath,
            "ERREUR connexion " + connstr +
            " [ORA-" + to_string(ex.getErrorCode()) + "] " +
            ex.getMessage()
        );
        throw;
    }
    return conn;
}

// ============================================================
// Exécution d'un script SQL externe avec substitution de variables
// ============================================================
void execute_sql_file(
    Connection* conn,
    const string& filepath,
    const string& project_id,
    const string& schema,
    const string& logpath
) {
    // Charger le fichier SQL
    ifstream file(filepath);
    string sql((istreambuf_iterator<char>(file)),
                istreambuf_iterator<char>());

    // Substitution des variables (pattern {{VAR}})
    size_t pos;
    while ((pos = sql.find("{{PROJECTID}}")) != string::npos)
        sql.replace(pos, 13, project_id);
    while ((pos = sql.find("{{SCHEMA}}")) != string::npos)
        sql.replace(pos, 10, schema);

    try {
        Statement* stmt = conn->createStatement(sql);
        stmt->executeUpdate();
        conn->commit();
        conn->terminateStatement(stmt);
        log_to_file(logpath, "SQL OK : " + filepath);
    }
    catch (SQLException& ex) {
        log_to_file(logpath,
            "ERREUR SQL " + filepath +
            " [ORA-" + to_string(ex.getErrorCode()) + "] " +
            ex.getMessage()
        );
        throw;
    }
}

// ============================================================
// Point d'entrée principal
// ============================================================
int main(int argc, char* argv[]) {

    const string logpath   = "/var/log/etl/extraction.log";
    const string pidfile   = "/var/run/etl_extract.pid";
    const string conf_path = "/usr/local/etc/etl.conf";

    // --- Verrouillage d'instance ---
    int lock_fd = acquire_lock(pidfile);
    if (lock_fd < 0) {
        log_to_file(logpath, "ABANDON : une instance est déjà en cours");
        return 1;
    }

    // --- Mémoire partagée pour la progression ---
    int shm_id = shmget(IPC_PRIVATE, sizeof(SharedProgress), IPC_CREAT | 0600);
    SharedProgress* progress = (SharedProgress*) shmat(shm_id, nullptr, 0);
    memset(progress, 0, sizeof(SharedProgress));
    progress->total_steps = 12;

    // --- Fork : parent = affichage, enfant = extraction ---
    pid_t pid = fork();

    if (pid == 0) {
        // =============================================
        // PROCESSUS ENFANT : extraction
        // =============================================
        Environment* env = nullptr;

        try {
            env = Environment::createEnvironment(Environment::DEFAULT);

            // Connexion aux 3 bases
            Connection* conn_source_1 = connect_oracle(
                env, "reader", "***", "source1_connstr", logpath);
            Connection* conn_source_2 = connect_oracle(
                env, "reader", "***", "source2_connstr", logpath);
            Connection* conn_target   = connect_oracle(
                env, "writer", "***", "target_connstr",  logpath);

            // Étape 1 : extraction source 1
            strncpy(progress->step_label, "Extraction source 1", 127);
            progress->current_step = 1;
            execute_sql_file(conn_target,
                "./sql/EXTRACTION_SOURCE_1.sql", "42", "SCHEMA_07", logpath);

            // Étape 2 : extraction source 2
            strncpy(progress->step_label, "Extraction source 2", 127);
            progress->current_step = 2;
            execute_sql_file(conn_target,
                "./sql/EXTRACTION_SOURCE_2.sql", "42", "SCHEMA_07", logpath);

            // Étape 3 : déclenchement procédure PL/SQL
            strncpy(progress->step_label, "Traitement PL/SQL", 127);
            progress->current_step = 3;
            {
                Statement* stmt = conn_target->createStatement(
                    "BEGIN PROC_MAITRE(:1, :2); END;");
                stmt->setInt(1, 42);
                stmt->setInt(2, 7);
                stmt->executeUpdate();
                conn_target->commit();
                conn_target->terminateStatement(stmt);
            }

            // Nettoyage
            env->terminateConnection(conn_source_1);
            env->terminateConnection(conn_source_2);
            env->terminateConnection(conn_target);
            Environment::terminateEnvironment(env);

            progress->finished = true;
            log_to_file(logpath, "Extraction terminée avec succès");
        }
        catch (SQLException& ex) {
            progress->error = true;
            log_to_file(logpath,
                "ERREUR FATALE [ORA-" + to_string(ex.getErrorCode()) + "] " +
                ex.getMessage());
            if (env) Environment::terminateEnvironment(env);
            _exit(1);
        }
        catch (exception& ex) {
            progress->error = true;
            log_to_file(logpath, string("ERREUR FATALE : ") + ex.what());
            if (env) Environment::terminateEnvironment(env);
            _exit(1);
        }

        _exit(0);

    } else {
        // =============================================
        // PROCESSUS PARENT : affichage de la progression
        // =============================================
        while (!progress->finished && !progress->error) {
            cout << "\r[" << progress->current_step
                 << "/" << progress->total_steps << "] "
                 << progress->step_label << "     " << flush;
            usleep(500000);  // 0.5s
        }

        int status;
        waitpid(pid, &status, 0);

        if (progress->error) {
            cout << "\n*** ERREUR — voir " << logpath << endl;
        } else {
            cout << "\nTerminé." << endl;
        }

        // Libération mémoire partagée
        shmdt(progress);
        shmctl(shm_id, IPC_RMID, nullptr);
    }

    // Libération du verrou
    flock(lock_fd, LOCK_UN);
    close(lock_fd);
    unlink(pidfile.c_str());

    return 0;
}
```
