#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <fcntl.h>
#include <cjson/cJSON.h>

// my headers
#include "./includes/db.h"
#include "./includes/operator.h"
//#include "./includes/register.h"

// communication channels
// tcp
#include "./includes/tcp/beacon_tcp.h"
#include "includes/tcp/tcp_listener.h"
// tcp ssl
#include "includes/tcp_ssl/tcp_ssl.h"
#include "includes/tcp_ssl/beacon_tcp_ssl.h"
// http

// https

#define BUFFER_SIZE 256

struct database_configs_t {
    char database_server[BUFFER_SIZE];
    char username[BUFFER_SIZE];
    char password[BUFFER_SIZE];
    char database[BUFFER_SIZE];
};


struct operator_console_t {
    int tcp_port;
    int tcp_ssl_port;
};

struct communication_channels_t {
    bool tcp;
    bool https;
    bool tcp_ssl;
    // ports
    int tcp_port;
    int https_port;
    int tcp_ssl_port;
    // ssl certificates
    char ssl_cert[0x100];
    char ssl_key[0x100];
};



char *server_config();
struct database_configs_t *database_config(cJSON *configs);
struct communication_channels_t *channels_config(cJSON *configs);
struct operator_console_t *operator_console(cJSON *configs);

int main() {
    // for log file
    lopen();
    // get 

    char *buffer = server_config();
    PARSE:
    cJSON *config = cJSON_Parse(buffer);
    if (!config) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Failed to parse JSON near: %s\n", error_ptr);
        } else {
            fprintf(stderr, "Failed to parse JSON.\n");
        }
        free(buffer);  // Avoid memory leak
        sleep(30);
        goto PARSE;
    }
    free(buffer);

    struct database_configs_t *database = database_config(config);
    if (database == NULL) puts("Database NULL");

    struct communication_channels_t *channels = channels_config(config);
    if (channels == NULL) puts("Channles NULL");
    
    struct operator_console_t *operator = operator_console(config); 
    if (operator == NULL) puts("Operator NULL");

    cJSON_Delete(config);
    // open logs
    if (db_conn(database->database_server, database->username, database->password, database->database)) {
        perror("Database Failed to connect");
        sleep(120);
    }

    do {
        
        pthread_t operator_thread, tcp_thread, tcp_ssl_thread;
        if (operator->tcp_port != NULL) {
            if (pthread_create(&operator_thread, NULL, Operator_conn, (void*)&operator->tcp_port) != 0) {
                perror("Failed to start Operator thread");
                sleep(30);
            }
        } else {
            fprintf(stderr, "port is NULL!\n");
            puts("ERROR FOR PORT");
        }

        if (channels->tcp == true) {
            
            if (pthread_create(&tcp_thread, NULL, tcp_agent_conn, (void*)&channels->tcp_port) != 0) {
                perror("Failed to start TCP thread");
                sleep(30);
            }
        }
        
        if (channels->tcp_ssl == true) {
            struct Args_t {
                char cert[BUFFER_SIZE];
                char key[BUFFER_SIZE];
                int port;
            };

            struct Args_t *args = malloc(sizeof(*args));;
            strncpy(args->cert, channels->ssl_cert, BUFFER_SIZE);
            strncpy(args->key, channels->ssl_key, BUFFER_SIZE);
            args->port = channels->tcp_ssl_port;

            if (pthread_create(&tcp_ssl_thread, NULL, tcp_ssl_listener, (void*)args) != 0) {
                perror("Failed to start TCP SSL thread");
                sleep(30);
            }
        }

        // Wait for threads to finish (if they ever do)
        pthread_join(operator_thread, NULL);
        pthread_join(tcp_thread, NULL);
        pthread_join(tcp_ssl_thread, NULL);
    } while (1);

    free(operator);
    free(channels);
    free(database);
    
    db_close();
    lclose();
    return 0;
}

char *server_config() {
    char *buffer = (char*)malloc(0x400);
    size_t bytesRead;
    START:
    int conf = open("../config/server_conf.json", O_RDONLY);
    if (conf == -1) {
        //write(1, "Failed to Configuration file\n", 20);
        perror( "Failed to Configuration file\n");
        // logfile
        sleep(30);
        goto START;
    }
    if ((bytesRead = read(conf, buffer, 0x400 - 1)) <= 0) {
        perror("Read Error");
        sleep(30);
        goto START;
    }
    close(conf);
    return buffer;
}



struct database_configs_t *database_config(cJSON *configs) {
    struct database_configs_t *database = malloc(sizeof(*database));
    if (!database) return NULL;

    cJSON *database_array = cJSON_GetObjectItem(configs, "Database");
    if (!database_array || !cJSON_IsArray(database_array)) {
        fprintf(stderr, "Missing or invalid 'Database' array\n");
        free(database);
        return NULL;
    }

    cJSON *db_object = cJSON_GetArrayItem(database_array, 0);
    if (!db_object || !cJSON_IsObject(db_object)) {
        fprintf(stderr, "First item in 'Database' is not a valid object\n");
        free(database);
        return NULL;
    }

    char *keys[] = {"database_server", "username", "password", "database"};
    char *targets[] = {
        database->database_server,
        database->username,
        database->password,
        database->database
    };

    for (int i = 0; i < 4; i++) {
        cJSON *database_item = cJSON_GetObjectItem(db_object, keys[i]);
        const char *value = (database_item && cJSON_IsString(database_item)) ? database_item->valuestring : "NULL";
        strncpy(targets[i], value, BUFFER_SIZE - 1);
        targets[i][BUFFER_SIZE - 1] = '\0';
    }

    return database;
}




struct communication_channels_t *channels_config(cJSON *configs) {
    struct communication_channels_t *channels = malloc(sizeof(*channels));
    if (!channels) return NULL;

    cJSON *comm_array = cJSON_GetObjectItem(configs, "CommunicationChannels");
    if (!comm_array || !cJSON_IsArray(comm_array)) {
        fprintf(stderr, "'CommunicationChannels' is not an array\n");
        free(channels);
        return NULL;
    }

    cJSON *comm = cJSON_GetArrayItem(comm_array, 0);
    if (!comm || !cJSON_IsObject(comm)) {
        fprintf(stderr, "First item in 'CommunicationChannels' is not an object\n");
        free(channels);
        return NULL;
    }

    // Default values (optional)
    channels->tcp = channels->tcp_ssl = channels->https = 0;
    channels->tcp_port = channels->tcp_ssl_port = channels->https_port = 0;
    memset(channels->ssl_cert, 0, sizeof(channels->ssl_cert));
    memset(channels->ssl_key, 0, sizeof(channels->ssl_key));

    cJSON *item;

    if ((item = cJSON_GetObjectItem(comm, "tcp")) && cJSON_IsBool(item))
        channels->tcp = cJSON_IsTrue(item);

    if ((item = cJSON_GetObjectItem(comm, "tcp_ssl")) && cJSON_IsBool(item))
        channels->tcp_ssl = cJSON_IsTrue(item);

    if ((item = cJSON_GetObjectItem(comm, "https")) && cJSON_IsBool(item))
        channels->https = cJSON_IsTrue(item);

    if ((item = cJSON_GetObjectItem(comm, "tcp_port")) && cJSON_IsNumber(item))
        channels->tcp_port = item->valueint;

    if ((item = cJSON_GetObjectItem(comm, "tcp_ssl_port")) && cJSON_IsNumber(item))
        channels->tcp_ssl_port = item->valueint;

    if ((item = cJSON_GetObjectItem(comm, "https_port")) && cJSON_IsNumber(item))
        channels->https_port = item->valueint;

    if ((item = cJSON_GetObjectItem(comm, "ssl_cert")) && cJSON_IsString(item))
        strncpy(channels->ssl_cert, item->valuestring, sizeof(channels->ssl_cert) - 1);

    if ((item = cJSON_GetObjectItem(comm, "ssl_key")) && cJSON_IsString(item))
        strncpy(channels->ssl_key, item->valuestring, sizeof(channels->ssl_key) - 1);

    return channels;
}




struct operator_console_t *operator_console(cJSON *configs) {
    struct operator_console_t *operator = malloc(sizeof(*operator));
    if (!operator) return NULL;

    cJSON *op_array = cJSON_GetObjectItem(configs, "OperatorConsole");
    if (!op_array || !cJSON_IsArray(op_array)) {
        fprintf(stderr, "'OperatorConsole' is not an array\n");
        free(operator);
        return NULL;
    }

    cJSON *op = cJSON_GetArrayItem(op_array, 0);
    if (!op || !cJSON_IsObject(op)) {
        fprintf(stderr, "First item in 'OperatorConsole' is not an object\n");
        free(operator);
        return NULL;
    }

    cJSON *item = cJSON_GetObjectItem(op, "tcp_port");
    if (!item || !cJSON_IsNumber(item)) {
        fprintf(stderr, "'tcp_port' is missing or not a number\n");
        free(operator);
        return NULL;
    }

    operator->tcp_port = item->valueint;
    return operator;
}
