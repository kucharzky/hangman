#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define MCAST_PORT 5001
#define MCAST_GROUP "224.1.1.1"

// Funkcja do odbierania wiadomości z serwera
void* receive_messages(void* socket_desc) {
    int sock = *(int*)socket_desc;
    char buffer[BUFFER_SIZE];

    while(1) {
        memset(buffer, 0, BUFFER_SIZE);
        if(recv(sock, buffer, BUFFER_SIZE, 0) <= 0) {
            printf("Połączenie z serwerem zostało przerwane\n");
            exit(1);
        }
        printf("%s", buffer);
    }
    return NULL;
}

int main() {
    int sock = 0, mcast_sock = 0;
    struct sockaddr_in serv_addr, mcast_addr;
    pthread_t thread_id;
    char buffer[BUFFER_SIZE];
    char nickname[50];
    char server_ip[16];

    // Tworzenie gniazda do multicastu
    mcast_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(mcast_sock < 0) {
        printf("Nie można utworzyć socketu multicast\n");
        return -1;
    }

    // Konfiguracja adresu i portu do multicastu
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_addr.s_addr = inet_addr(MCAST_GROUP);
    mcast_addr.sin_port = htons(MCAST_PORT);

    // Wysłanie zapytania na adres multicastowy w celu otrzymania adresu
    char* mcast_request = "HANGMAN_DISCOVER";
    if(sendto(mcast_sock, mcast_request, strlen(mcast_request), 0, 
              (struct sockaddr*)&mcast_addr, sizeof(mcast_addr)) < 0) {
        printf("Błąd wysyłania żądania multicast\n");
        return -1;
    }

    // Receive unicast response with server IP
    memset(buffer, 0, BUFFER_SIZE);
    struct sockaddr_in resp_addr;
    socklen_t resp_len = sizeof(resp_addr);
    if(recvfrom(mcast_sock, buffer, BUFFER_SIZE, 0, 
                (struct sockaddr*)&resp_addr, &resp_len) < 0) {
        printf("Błąd odbierania odpowiedzi multicast\n");
        return -1;
    }
    close(mcast_sock);

    strcpy(server_ip, inet_ntoa(resp_addr.sin_addr));
    printf("Znaleziono serwer pod adresem: %s\n", server_ip);

    // Tworzenie gniazda TCP
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        printf("Nie można utworzyć socketu\n");
        return -1;
    }

    // Pobranie nicku od uzytkownika
    printf("Podaj nick: ");
    fgets(nickname, sizeof(nickname), stdin);
    nickname[strcspn(nickname, "\n")] = 0;

    // Konfiguracja adresu i portu
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(5000);

    if(inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("Nieprawidłowy adres IP\n");
        return -1;
    }

    // Połączenie do serwera na gnieździe TCP
    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Nie można połączyć z serwerem\n");
        return -1;
    }

    // Wysłanie nicku do serwera
    send(sock, nickname, strlen(nickname), 0);

    // Tworzenie wątku do odbierania informacji
    if(pthread_create(&thread_id, NULL, receive_messages, (void*)&sock) < 0) {
        printf("Nie można utworzyć wątku\n");
        return -1;
    }

    // Pętla główna
    while(1) {
        // Pobieranie i wysyłania wiadomości użytkownika
        memset(buffer, 0, BUFFER_SIZE);
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        if(send(sock, buffer, strlen(buffer), 0) < 0) {
            printf("Nie można wysłać wiadomości\n");
            return -1;
        }

        // Obsługa komendy quit
        if(strcmp(buffer, "quit") == 0) {
            break;
        }
    }

    close(sock); // Zamknięcie gniazda
    pthread_cancel(thread_id); // Zatrzymanie wątku odbierania wiadomości
    pthread_join(thread_id, NULL); // Oczekiwanie na zatrzymanie wątku przed zatrzymaniem wątku głównego

    return 0;
}