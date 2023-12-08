#include "download.h"

int createSocket(char *ip, int port){
    int sock;
    struct sockaddr_in address;
    bzero((char*) &address, sizeof(address));
    address.sin_family= AF_INET;
    address.sin_addr.s_addr= inet_addr(ip);
    address.sin_port= htons(port);

    if ((sock = socket(AF_INET, SOCK_STREAM,0 ))< 0 ){
        exit(-1);
    }

    if (connect(sock, (struct sockaddr*)& address, sizeof(address)) < 0){
        exit(-1);
    };
    return sock;
}

int parseFTP(char *input, struct URL *url) {
    regex_t regex_compiled, generic_regex_compiled;
    char regex[] = "ftp://([^:]+):([^@]+)@([^/]+)/(.+)";
    char generic_regex[] = "ftp://([^/]+)/(.+)";
    regcomp(&regex_compiled, regex, REG_EXTENDED);
    regcomp(&generic_regex_compiled, generic_regex, REG_EXTENDED);
    regmatch_t helper[6];
    if (regexec(&regex_compiled, input, 6, helper, 0) == 0) {
        if (helper[1].rm_so != -1) {
            strncpy(url->user, input + helper[1].rm_so, helper[1].rm_eo - helper[1].rm_so);
            url->user[helper[1].rm_eo - helper[1].rm_so] = '\0';
        }
        if (helper[2].rm_so != -1) {
            strncpy(url->password, input + helper[2].rm_so, helper[2].rm_eo - helper[2].rm_so);
            url->password[helper[2].rm_eo - helper[2].rm_so] = '\0';
        }
        strncpy(url->host, input + helper[3].rm_so, helper[3].rm_eo - helper[3].rm_so);
        url->host[helper[3].rm_eo - helper[3].rm_so] = '\0';
        strncpy(url->resource, input + helper[4].rm_so, helper[4].rm_eo - helper[4].rm_so);
        url->resource[helper[4].rm_eo - helper[4].rm_so] = '\0';
    } else if (regexec(&generic_regex_compiled, input, 4, helper, 0) == 0) {
        strcpy(url->user, "anonymous");
        strcpy(url->password, "password");
        strncpy(url->host, input + helper[1].rm_so, helper[1].rm_eo - helper[1].rm_so);
        url->host[helper[1].rm_eo - helper[1].rm_so] = '\0';
        strncpy(url->resource, input + helper[2].rm_so, helper[2].rm_eo - helper[2].rm_so);
        url->resource[helper[2].rm_eo - helper[2].rm_so] = '\0';
    }
    else {
        regfree(&regex_compiled);
        regfree(&generic_regex_compiled);
        return -1;  
    }
    struct hostent *host;
    printf("Host: %s\n", url->host);
    printf("User: %s\n", url->user);
    printf("Resource: %s\n", url->resource);
    printf("Password: %s\n", url->password);
    if ((host = gethostbyname(url->host)) == NULL) {
        herror("gethostbyname()");
        return -1;
    }
    strcpy(url->ip, inet_ntoa(*((struct in_addr *) host->h_addr)));
    regfree(&regex_compiled);
    regfree(&generic_regex_compiled);
    return 0;
}

int passive_mode(const int socket ,char *ip, int *port){
    int ip1, ip2, ip3, ip4, port1, port2;
    write(socket, "pasv\n", 5);
    char result[MAX_LENGTH];
    if (readResponse(socket, result) != RESPONSE_CODE_PASSIVE) return -1;
    sscanf(result, PASSIVE_REGEX, &ip1, &ip2, &ip3, &ip4, &port1, &port2);
    *port = port1 * 256 + port2;
    sprintf(ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
    return RESPONSE_CODE_PASSIVE;
}

int readResponse(int socket, char *buf){
    char helper;
    int i = 0;
    int code;
    int count = 0;
    ResponseState response = START;
    memset(buf, 0, MAX_LENGTH);
    printf("Reading response\n");
    while (count != 3) {
        printf("Reading byte\n");
        read(socket, &helper, 1);
        printf("Byte: %c\n", helper);
        if (count == 0) { 
            if (helper == ' ') {
                printf("Space\n");
                count = 1; 
            } else if (helper == '-') {
                printf("Dash\n");
                count = 2; 
            } else if (helper == '\n') {
                printf("Newline\n");
                count = 3; 
            } else {
                printf("Other\n");
                buf[i++] = helper;
            }
        } else if (count == 1) { 
            if (helper == '\n') {
                count = 3; 
            } else {
                buf[i++] = helper;
            }
        } else if (count == 2) { 
            if (helper == '\n') {
                count = 0; 
                i = 0;
                memset(buf, 0, MAX_LENGTH);
            } else {
                buf[i++] = helper;
            }
        }
        sscanf(buf, "%d", &code);
        printf("Buf: %s\n", buf);
        return code;
    }
}

int authenticate(int socket, char *user, char *password){
    char helper[MAX_LENGTH];
    char password[5+strlen(password)+1];
    char user[5+strlen(user)+1];
    printf("Socket: %d\n", socket);
    sprintf(password, "pass %s\n", password);
    sprintf(user, "user %s\n", user);
    printf("User command: %s\n", user);
    write(socket, user, strlen(user));
    if(readResponse(socket,helper)!=331) exit(-1); 
    memset(helper, 0, MAX_LENGTH);
    write(socket, password, strlen(password));
    return readResponse(socket,helper); 
}

int requestResource(int socket, char *resource){
    char command[5+strlen(resource)+1];
    char helper[MAX_LENGTH];
    sprintf(command, "retr %s\n", resource);
    write(socket, command, strlen(command));
    return readResponse(socket,helper);
}

int getResource(int socketA, int socketB, char *resource){
    int reader;
    char helper[MAX_LENGTH];
    FILE *file = fopen(resource, "wb");
    while((reader=read(socketB, helper, MAX_LENGTH))>0){
        if(fwrite(helper, 1, reader, file)==0){return -1;}
    }
    fclose(file);
    return readResponse(socketA,helper);
}

int close_connection(const int socketA, const int socketB){
    write(socketA, "quit\n", 5);
    char response[MAX_LENGTH];
    if(readResponse(socketA,response)!=221) return -1;
    write(socketB, "quit\n", 5);
    memset(response, 0, MAX_LENGTH);
    if(readResponse(socketB,response)!=221) return -1; 
    return 0;
}


int main(int argc, char *argv[]){
    struct URL url;
    memset(&url, 0, sizeof(url));
    if(argc!=2){
        printf("Usage: ./download ftp://[<user>:<password>@]<host>/<url-path>\n");
        exit(-1);
    }

    if (parseFTP(argv[1], &url)!= 0) exit (-1);
    
    char response[MAX_LENGTH];
    int firstSocket = createSocket(url.ip, 21); 
    printf("First Socket: %d\n", firstSocket);
    if (readResponse(firstSocket, response) != 220){
        printf("Error creating socket\n"); 
        exit(-1);
    }
    printf("Print going to authenticate\n");
    printf("User: %s\n", url.user);
    printf("Password: %s\n", url.password);
    if (authenticate(firstSocket, url.user, url.password) != 230){ 
        printf("Authentication failed, user and password not matching\n");
        exit(-1);
    }
    printf("Print going to passive mode\n");
    int port;
    char ip[16];
    if (passive_mode(firstSocket, ip, &port) != RESPONSE_CODE_PASSIVE){
        exit(-1);
    }
    printf("Ip: %s\n", ip);
    printf("Port: %d\n", port);
    int secondSocket = createSocket(ip, port);
    printf("Second Socket: %d\n", secondSocket);
    if (secondSocket < 0 ){ 
        exit(-1);
    }

    if(requestResource(firstSocket, url.resource)!=RESPONSE_CODE_READY_FOR_TRANSFER){
        printf("Error requesting resource\n");
        exit(-1);
    }

    if(getResource(firstSocket, secondSocket, url.resource)!=RESPONSE_CODE_TRANSFER_COMPLETE){
        printf("Error getting resource\n");
        exit(-1);
    }
    printf("Resource downloaded successfully\n");

    if (close(firstSocket)<0) {
        perror("close()");
        exit(-1);
    }

    if (close(secondSocket)<0) {
        perror("close()");
        exit(-1);
    }
    printf("Connection closed\n");
    return 0;
}