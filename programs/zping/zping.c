/*
Copyright 2019-2020 NetFoundry, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <ziti/errors.h>
#include <ziti/ziti.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <signal.h>
#include <argp.h>
#include <stdio.h>

#define DIE(v) do { \
int code = (v);\
if (code != ZITI_OK) {\
fprintf(stderr, "ERROR: " #v " => %s\n", ziti_errorstr(code));\
exit(code);\
}} while(0)

void INThandler(int);
static void print_stats(void);
static void getStddev(void);
static void on_client_connect(ziti_connection clt, int status);
static void on_client_write(ziti_connection clt, ssize_t status, void *ctx);
static ssize_t on_client_data(ziti_connection clt, uint8_t *data, ssize_t len);
static void on_client(ziti_connection serv, ziti_connection client, int status, ziti_client_ctx *clt_ctx);
static void listen_cb(ziti_connection serv, int status);
static void on_write(ziti_connection conn, ssize_t status, void *ctx);
static void on_connect(ziti_connection conn, int status);
static ssize_t on_data(ziti_connection c, uint8_t *buf, ssize_t len);
static void on_signal(uv_signal_t *h, int signal);
static void on_ziti_init(ziti_context ztx, const ziti_event_t *ev);
static void on_ziti_enroll(ziti_config *cfg, int status, char *err_message, void *ctx);
static int write_identity_file(ziti_config *cfg);



static ziti_context ziti;
static bool server = false;
static const char *service;
static const char *config;
static const char *identity;
static const char *output_file;
static const char *ekey;
static const char *ecert;
static const char *jwt;
static int count = 31536000;
static int pmax = 31536000;
static int bytes = 100;
static double interval = 1.0;
struct timeval st, en, cur;
struct timeval is[1000000];
static double rt[1000000];
static bool isActive = false;
static bool enroll = false;
uv_idle_t idler;
uv_loop_t *loop;
ziti_connection zconn;
static uv_signal_t sig;

struct pingSession{
    struct timeval istart[31536000];
    double rt[31536000];
    int sent;
    int recv;
    double avgrt;
    double maxrt;
    double minrt;
    double stddv;
    ziti_connection c;
};

struct enroll_cert {
    const char *key;
    const char *cert;
};

static struct pingSession ps;

static int parse_opt (int key, char *arg, struct argp_state *state){
    switch(key){
        case 'm':{
            if (strcmp("client", arg) == 0) {
                printf("Running as client\n");
            }
            else if (strcmp("server", arg) == 0) {
                printf("Running as server\n");
                server = true;
            }
            else if (strcmp("enroll", arg) == 0) {
                printf("enrolling\n");
                enroll = true;
            }
            else{
                printf("Invalid mode for option -m,--mode <server|client>\n");
                printf("zping --help for more info\n");
                exit(1);
            }
            break;
        }
        case 'c':{
            config = arg;
            break;
        }
        case 's':{
            service = arg;
            break;
        }  
        case 'l':{
            bytes = atoi(arg);
            break;
        }
        case 't':{
            interval = (double)atoi(arg);
            break;
        }
        case 'n':{
            count = atoi(arg);
            pmax = atoi(arg);
            break;
        }
        case 'i':{
            identity = arg;
            break;
        }
        case 'k':{
            ekey = arg;
            break;
        }
        case 'e':{
            ecert = arg;
            break;
        }
        case 'o':{
            output_file = arg;
            break;
        }
         case 'j':{
            jwt = arg;
            break;
        }

    }
  return 0;
}

void  INThandler(int sig){
    signal(sig, SIG_IGN);
    print_stats();
}

static void on_client_write(ziti_connection clt, ssize_t status, void *ctx) {
    free(ctx);
}

static ssize_t on_client_data(ziti_connection clt, uint8_t *data, ssize_t len) {
    if (len > 0) {
        uint8_t *reply = malloc(128);
        size_t l = sprintf(reply, "%.*s", (int)len, data);
        ziti_write(clt, reply, l, on_client_write, reply);
    }
    else if (len == ZITI_EOF) {
        printf("client disconnected\n");
        ziti_close(clt, NULL);
    }
    else {
        fprintf(stderr, "error: %zd(%s)", len, ziti_errorstr(len));
        ziti_close(clt, NULL);
    }
    return len;
}

static void getStddev() {
        double sum = 0.0;
        double sqavg = 0.0;
        for (int x=0; x < ps.recv; x++ ) {
                double elem = ps.rt[x];
                sum += pow(elem-ps.avgrt, 2.0);
                if (x == (ps.recv -1)) { 
                    sqavg = sum/(double)ps.recv;
                }
        }
        ps.stddv = sqrt(sqavg);
        
}

static void getMinMaxAvg() {
        double total = 0.0;
        double avg = 0.0;
        double max = 0.0;
        double min = 0.0;
        for (int x=0; x < ps.recv; x++ ){
                double elem = ps.rt[x];
                total += elem;
                if (x == 0) {
                        min = elem;
                        max = elem;
                }
                if (elem < min) {
                        min = elem;
                }
                if (elem > max) {
                        max = elem;
                }
                if (x == (ps.recv -1)) {
                    avg = total / (double)ps.recv;
                }

        }
        ps.avgrt = avg;
        ps.maxrt = max;
        ps.minrt = min;
}

static void on_client_connect(ziti_connection clt, int status) {
    if (status == ZITI_OK) {
        char *base = malloc(128);
        size_t l = sprintf(base, "%s\n", "Ping Server Connected!");
        ziti_write(clt, base, l, on_client_write, base);
    }
}

static void on_client(ziti_connection serv, ziti_connection client, int status, ziti_client_ctx *clt_ctx) {
    if (status == ZITI_OK) {
        const char *source_identity = clt_ctx->caller_id;
        if (source_identity != NULL) {
            fprintf(stderr, "incoming connection from '%s'\n", source_identity);
        }
        else {
            fprintf(stderr, "incoming connection from unidentified client\n");
        }
        if (clt_ctx->app_data != NULL) {
            fprintf(stderr, "Client initiated: '%.*s'!\n", (int) clt_ctx->app_data_sz, clt_ctx->app_data);
        }
        ziti_accept(client, on_client_connect, on_client_data);
    } else {
        fprintf(stderr, "failed to accept client: %s(%d)\n", ziti_errorstr(status), status);
    }
}

static void listen_cb(ziti_connection serv, int status) {
    if (status == ZITI_OK) {
        printf("Ping Server is ready! %d(%s)\n", status, ziti_errorstr(status));
    }
    else {
        printf("ERROR The ping server could not be started: %d(%s)\n", status, ziti_errorstr(status));
        ziti_close(serv, NULL);
    }
}

static void on_write(ziti_connection conn, ssize_t status, void *ctx) {
    if (status < 0) {
        fprintf(stderr, "request failed to submit status[%zd]: %s\n", status, ziti_errorstr((int) status));
    }
    if (ctx) {
        free(ctx);
    }
}

static void print_stats(){
        isActive = false;
        getMinMaxAvg();
        getStddev();
        printf("\n--- %s ping statistics ---\n", identity);
        printf("%d packets sent and %d packets received, %.2lf%% packet loss\n",ps.sent,ps.recv,100*(1.0-(((double)ps.recv)/(double)ps.sent)));
        printf("round-trip min/max/avg/stddev %.3lf/%.3lf/%.3lf/%.3lf ms\n\n",ps.minrt,ps.maxrt,ps.avgrt,ps.stddv);
        //ziti_close(ps.c, NULL);
        ziti_shutdown(ziti);
}

static void ping_idle_cb(uv_idle_t* handle) {
    gettimeofday(&st, NULL);
    int seq = pmax - count;
    int y = bytes;
    char pattern[bytes + 1];
    for (int x = 0; x < bytes; x++){
        pattern[x] = 'A';
    }   
    pattern[bytes] = '\0';
    if (count < 0){
        uv_idle_stop(handle);
    }
    else if ((seq == 0) && isActive){  
       ps.istart[seq] = st;
       char *base = malloc(bytes + 1);
       size_t l = sprintf(base, "%s%d", pattern, seq);
       DIE(ziti_write(zconn, base, l, on_write, base));
       ps.sent = seq + 1;
       count--;
    }
    else if (isActive){
       double diff_taken; 
       diff_taken = (st.tv_sec - ps.istart[seq-1].tv_sec) * 1e6;
       diff_taken = (diff_taken + (st.tv_usec - ps.istart[seq-1].tv_usec)) * 1e-6;
       if (diff_taken >= interval){
           ps.istart[seq] = st;
           char *base = malloc(128);
           size_t l = sprintf(base, "%s%d", pattern, seq);
           DIE(ziti_write(zconn, base, l, on_write, base));
           ps.sent = seq + 1;
           count--;
       }
    }

}

static void on_connect(ziti_connection conn, int status) {
    zconn = conn;
    ps.c = zconn;
    DIE(status);
    uv_idle_start(&idler, ping_idle_cb);
}

static ssize_t on_data(ziti_connection c, uint8_t *buf, ssize_t len) {
    size_t total;
    int seq;
    if (len == ZITI_EOF) {

        printf("request completed: %s\n", ziti_errorstr(len));
        ziti_close(c, NULL);
        ziti_shutdown(ziti);

    }
    else if (len < 0) {
        fprintf(stderr, "unexpected error: %s\n", ziti_errorstr(len));
        ziti_close(c, NULL);
        ziti_shutdown(ziti);
    }
    else if(len == 23){
        printf("%.*s\n", (int)len, buf);
        isActive = true;
    }
    else {
        total += len;
        char *reply = malloc(128);
        char *sequence = malloc(bytes);
        size_t l = sprintf(reply, "%.*s", (int)len, buf);
        size_t slen = sprintf(sequence, "%s", reply + bytes);
        seq = atoi(sequence);
        ps.recv = seq + 1;
        double time_taken;
        gettimeofday(&en, NULL);
        time_taken = (en.tv_sec - ps.istart[seq].tv_sec) * 1e6;
        time_taken = (time_taken + (en.tv_usec - ps.istart[seq].tv_usec)) * 1e-6;
        ps.rt[seq] = time_taken*1000;
        printf("%ld bytes from server ziti_seq=%s time=%.3lfms\n",(long)l-(long)slen,sequence,ps.rt[seq]);
        free(reply);    
    }
    if (((ps.recv) == pmax) && isActive){
        print_stats();
    }
    return len;
}

static void on_ziti_init(ziti_context ztx, const ziti_event_t *ev) {
    if (ev->type != ZitiContextEvent) return;

    if (ev->event.ctx.ctrl_status == ZITI_PARTIALLY_AUTHENTICATED) return;

    if (ev->event.ctx.ctrl_status != ZITI_OK) {
        DIE(ev->event.ctx.ctrl_status);
        return;
    }
    ziti = ztx;
    ziti_connection conn;
    ziti_conn_init(ziti, &conn, NULL);
    if (server) {
        ziti_listen_opts listen_opts = {
                //.identity = "winserver03",
                .bind_using_edge_identity = true,
//              .terminator_precedence = PRECEDENCE_REQUIRED,
//              .terminator_cost = 10,
        };
        ziti_listen_with_options(conn, service, &listen_opts, listen_cb, on_client);
    }
    else {
        char app_data[100];
        snprintf ( app_data, 100, "Requesting %d pings", count);
        ziti_dial_opts dial_opts = {
                .identity = identity,
                .app_data = app_data,
                .app_data_sz = strlen(app_data) + 1,
        };
        DIE(ziti_dial_with_options(conn, service, &dial_opts, on_connect, on_data));
    }
}

static int write_identity_file(ziti_config *cfg) {
    FILE *f;

    size_t len;
    char *output_buf = ziti_config_to_json(cfg, 0, &len);

    if ((f = fopen(output_file, "wb")) == NULL) {
        return (-1);
    }

    int rc = ZITI_OK;
    if (fwrite(output_buf, 1, len, f) != len) {
        rc = -1;
    }

    free(output_buf);
    fclose( f );

    return rc;
}

static void on_ziti_enroll(ziti_config *cfg, int status, char *err_message, void *ctx) {

    if (status != ZITI_OK) {
        fprintf(stderr, "ERROR: => %d => %s\n", status, err_message);
        exit(status);
    }


    int rc = write_identity_file(cfg);

    DIE(rc);
}

int main(int argc, char **argv) {
    signal(SIGINT, INThandler);
    loop = uv_default_loop();
    
    struct argp_option options[] = {
        { "mode", 'm',"<mode>", 0, "set server|client mode|enroll"},
        { "config", 'c',"<path to config-file>",0, "set config file <mandatory>"},
        { "service", 's',"<service name>",0, "set ziti service <mandatory>"},
        { "bytes", 'l',"<integer bytes>",0, "set size of payload <default 100>"},
        { "time-out", 't',"<integer seconds>",0, "set time between pings <default 1 sec>"},
        { "number", 'n',"<integer>",0, "set number of pings <default 31536000> "},
        { "identity", 'i',"<identity>",0, "set identity to dial <mandatory>"},
        { "output-file", 'o',"<path to output file>",0, "set enrollment output-file <mandatory>"},
        { "key", 'k',"<path to key file>",0, "set enrollment key <optional>"},
        { "cert", 'e',"<path to cert file>",0, "set enrollment cert <optional>"},
        { "jwt", 'j',"<path to jwt file>",0, "set jwt <optional>"},
        {0}
    };
    struct argp argp = { options, parse_opt, 0, 0 };
    argp_parse (&argp, argc, argv, 0, 0, 0);
    if (!enroll){
        if (config != NULL){
            printf("Connecting using credentials in: %s\n",config);
        } 
        else{
            printf("Missing mandatory option -c,--config <path to config-file>\n");
            printf("zping --help for more info\n");
            exit(1);
        }
        if (identity != NULL){
            printf("Connecting to identity: %s\n",identity);
        }
        else if ((identity == NULL) && (!server)){
            printf("Missing mandatory option -i,--identity <identity name>\n");
            printf("zping --help for more info\n");
            exit(1);
        }
        
        uv_idle_init(loop, &idler);
    
        DIE(ziti_init(config, loop, on_ziti_init, ZitiContextEvent, loop));

        uv_signal_init(loop, &sig);
    }
    else{
        if(output_file == NULL){
            printf("Missing mandatory option -o,--output-file <path to output file>\n");
            printf("zping --help for more info\n");
            exit(1);
        }

        ziti_enroll_opts opts = {0};
        struct enroll_cert cert;
        if((jwt == NULL) && ((ekey == NULL) || (ecert == NULL))){
            printf("Must specify either -j or both (-k and -e)\n");
            printf("zping --help for more info\n");
            exit(1);
        }
        if(jwt != NULL){
           opts.jwt = realpath(jwt, NULL);
        }

        if (ekey != NULL) {
            opts.enroll_key = realpath(ekey, NULL);
        }

        if (ecert != NULL) {
            opts.enroll_cert = realpath(ecert, NULL);
        }

        DIE(ziti_enroll(&opts, loop, on_ziti_enroll, NULL));
    }
    uv_run(loop, UV_RUN_DEFAULT);
    if (enroll){
        printf("\nSuccessfully registered and output id to: %s\n", output_file); 
    }
    return 0;
}

