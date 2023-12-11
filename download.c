#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>

// Protocol Responses
#define SERVICE_READY      220
#define USERNAME_OK        331
#define LOGIN_SUCCESSFUL   230
#define ENTER_PASSIVE_MODE 227
#define FILE_STATUS_OK     150
#define TRANSFER_COMPLETE  226

typedef struct URL {
    char *user;
    char *password;
    char *host;
    char *path;
    char *filename;
    char ip[128];
} URL;

int openSocket(char *ip, int port){
    int sockfd;
    struct sockaddr_in server_addr;

    /*server address handling*/
    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);    /*32 bit Internet address network byte ordered*/
    server_addr.sin_port = htons(port);        /*server TCP port must be network byte ordered */

    /*open a TCP socket*/
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        exit(-1);
    }
    /*connect to the server*/
    if (connect(sockfd,
                (struct sockaddr *) &server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect()");
        exit(-1);
    }

    return sockfd;
}

int serverReply(FILE *readSocket){
    char* buffer;
    size_t len = 0;
    
    long code;

    while (getline(&buffer, &len, readSocket) != -1) { // lê diretamente do socket para o buffer
        printf("%s", buffer);

        if (buffer[3]==' '){
            code = atol(buffer);
            break;
        }
    }
    return code; // retornar o replyCode
}

int sendCommand(int socket, char *command){

    if (write(socket, command, strlen(command)) <= 0){
        printf("Error sending command\n");
        exit(-1);
    }

    return 0;
}

int transferFile(int socket, char *fileName){
    int fd;

    if ((fd = open(fileName, O_WRONLY | O_CREAT)) < 0){
        printf("Error opening file\n");
        exit(-1);    
    }

    char buffer[1];
    int bytesRead;

    while((bytesRead = read(socket, buffer, 1)) > 0) {
        if(write(fd,buffer,bytesRead) < 0){
            exit(-1);
        }
    }

    if(close(fd) < 0){
        exit(-1);
    }

    return 0;
}

int download(struct URL url){

    int socketA = openSocket(url.ip, 21);

    if(socketA < 0){
        printf("Error opening socket\n");
        exit(-1);
    }

    FILE *readSocket = fdopen(socketA, "r");

    if (serverReply(readSocket) != SERVICE_READY){ // se não responder com 220 retorna com erro
        printf("Error connecting to server\n");
        exit(-1);
    }

    char command[256];
    sprintf(command, "USER %s\n", url.user);

    sendCommand(socketA, command);

    if (serverReply(readSocket) != USERNAME_OK){ // se não responder com 331 (a dizer que o username está correto) retorna com erro
        printf("Error sending username\n");
        exit(-1);
    }

    sprintf(command, "PASS %s\n", url.password);
    printf("Password sent: %s\n", command);

    sendCommand(socketA, command);

    if (serverReply(readSocket) != LOGIN_SUCCESSFUL){ // se não responder com 230 (password correta) retorna com erro
        printf("Error sending password, probably wrong credentials\n");
        exit(-1);
    }

    sprintf(command, "PASV\n");
    sendCommand(socketA, command);

    char* buffer;
    size_t len = 0;
    
    long code;

    while (getline(&buffer, &len, readSocket) != -1) { // lê diretamente do socket para o buffer
        printf("%s", buffer);

        if (buffer[3]==' '){
            code = atol(buffer);
            break;
        }
    }

    if (code != ENTER_PASSIVE_MODE){
        printf("Error entering passive mode\n");
        exit(-1);
    }

    int ip[4];
    int port[2];

    sscanf(buffer, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).\n", &ip[0], &ip[1], &ip[2], &ip[3], &port[0], &port[1]);

    printf("IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
    printf("Port: %d\n", port[0]*256 + port[1]);

    char aux_ip[32];

    sprintf(aux_ip, "%d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

    int socketB = openSocket(aux_ip, port[0]*256 + port[1]);
    
    if(socketB < 0){
        printf("Error opening socket\n");
        exit(-1);
    }

    sprintf(command, "retr %s\n", url.path);
    sendCommand(socketA, command);

    if (serverReply(readSocket) != FILE_STATUS_OK){ // se não responder com 150 (a dizer que o ficheiro está pronto para ser transferido) retorna com erro
        printf("Error opening file, probably wrong path given\n");
        exit(-1);
    }

    if (transferFile(socketB, url.filename) != 0){
        printf("Error transfering file\n");
        exit(-1);
    }

    sprintf(command, "quit \n");
    sendCommand(socketA, command);

    return 0;
}

int getIP(char *hostname, struct URL *url) {
    struct hostent *h;

    if ((h = gethostbyname(hostname)) == NULL) {
        herror("gethostbyname()");
        return -1;
    }

    strcpy(url->ip, inet_ntoa(*((struct in_addr *) h->h_addr)));

    printf("IP Address: %s\n", inet_ntoa(*((struct in_addr *) h->h_addr)));

    return 0;
}

int main(int argc, char **argv){
    if(argc != 2){
        printf("Usage: download ftp://<user>:<password>@<host>/<url>\n");
        exit(-1);
    }

    struct URL url;

    char* ftp = strtok(argv[1], "://");
    if(strcmp(ftp, "ftp") != 0){
        printf("Usage: download ftp://<user>:<password>@<host>/<url>\n");
        exit(-1);
    }

    char* credentials = strtok(NULL, "/"); // [<user>:<password>@]<host>
    url.path = strtok(NULL, ""); // <url>
    url.user = strtok(credentials, ":");
    url.password = strtok(NULL, "@");

    if(url.password == NULL){
        url.user = "anonymous";
        url.password = "anonymous";
        url.host = strtok(credentials, "/");
    } else {
        url.host = strtok(NULL, "/");
    }

    //debug
    printf("user: %s\n", url.user);
    printf("password: %s\n", url.password);
    printf("host: %s\n", url.host);
    printf("path: %s\n", url.path);
    
    if(getIP(url.host, &url) < 0){
        printf("Error getting IP\n");
        exit(-1);
    }

    char *filename = strrchr(url.path, '/');

    url.filename = url.path; // se não houver / no path, filename = path

    char *aux = strtok(filename, "/");
    while(aux != NULL){
        url.filename = aux;
        aux = strtok(NULL, "/");
    }

    printf("Filename: %s\n", url.filename);

    if(download(url) < 0){
        printf("Error downloading file\n");
        exit(-1);
    }

    return 0;
}   