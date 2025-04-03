#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ctype.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <time.h>

#define MAX_CLIENTS 4
#define BUFFER_SIZE 1024
#define MAX_WORD_LENGTH 50
#define MCAST_PORT 5001
#define MCAST_GROUP "224.1.1.1"
#define LOG_FILE "historia.txt"

typedef struct {
    int socket;
    char nickname[50];
    int id;
    struct sockaddr_in addr;
} Client;

// Struktura stanu gry
typedef struct {
    Client* clients[MAX_CLIENTS];
    int client_count;
    char current_word[MAX_WORD_LENGTH];
    char displayed_word[MAX_WORD_LENGTH];
    int lives;
    int current_player;
    int word_setter;
    int game_in_progress;
    pthread_mutex_t mutex;
} GameState;

GameState game_state = {
    .client_count = 0,
    .lives = 7,
    .current_player = 0,
    .word_setter = 0,
    .game_in_progress = 0
};

// Funckja wysyłająca historie gier
void send_ranking(int client_socket) {
    FILE* log_file = fopen(LOG_FILE, "r");
    if(log_file == NULL) {
        char* no_ranking = "Brak historii gier.\n";
        send(client_socket, no_ranking, strlen(no_ranking), 0);
        return;
    }

    char buffer[BUFFER_SIZE];
    while(fgets(buffer, sizeof(buffer), log_file)) {
        send(client_socket, buffer, strlen(buffer), 0);
    }
    
    fclose(log_file);
}

// Funkcja Zapisująca rezultat gry do pliku
void log_game_result(const char* word, const char* displayed_word, const char* winner, int lives_left, const char* incorrect_word) {
    FILE* log_file = fopen(LOG_FILE, "a");
    if(log_file == NULL) {
        perror("Brak pliku");
        return;
    }

    // Pobieranie aktualnego czasu
    time_t now;
    time(&now);
    char* timestamp = ctime(&now);
    timestamp[strcspn(timestamp, "\n")] = 0;

    char* outcome;
    if(strcmp(word, displayed_word) == 0) {
        outcome = "WYGRANA";
    } else if(lives_left == 0 || incorrect_word) {
        outcome = "PRZEGRANA";
    } else {
        outcome = "NIEROZSTRZYGNIĘTA";
    }

    // Wpisywanie logu
    if(incorrect_word) {
        fprintf(log_file, "[%s] Hasło: %s, Stan: %s, Odgadnięte: %s, Niepoprawne hasło: %s, Zwycięzca: %s, Pozostałe życia: %d\n", 
                timestamp, word, outcome, displayed_word, incorrect_word, 
                (winner ? winner : "BRAK"), lives_left);
    } else {
        fprintf(log_file, "[%s] Hasło: %s, Stan: %s, Odgadnięte: %s, Zwycięzca: %s, Pozostałe życia: %d\n", 
                timestamp, word, outcome, displayed_word, 
                (winner ? winner : "BRAK"), lives_left);
    }

    fclose(log_file);
}

// Funkcja wysyłająca wiadomość w pętli do połączonych klientów
void broadcast_message(char* message) {
    pthread_mutex_lock(&game_state.mutex);
    for(int i = 0; i < game_state.client_count; i++) {
        sendto(game_state.clients[i]->socket, message, strlen(message), 0, (struct sockaddr*)&game_state.clients[i]->addr, sizeof(game_state.clients[i]->addr));
    }
    pthread_mutex_unlock(&game_state.mutex);
}

// Inicjalizacja gry i resetowanie poprzedniego stanu
void init_game() {
    game_state.lives = 7;
    game_state.game_in_progress = 1;
    memset(game_state.displayed_word, 0, MAX_WORD_LENGTH);
    for(int i = 0; i < strlen(game_state.current_word); i++) {
        game_state.displayed_word[i] = '_';
    }
}

// Sprawdzanie obecności litery w haśle
void check_letter(char letter) {
    int found = 0;
    letter = tolower(letter); // Sprowadzanie liter do lowercase aby uniknąć problemów z wielkością liter

    for(int i = 0; i < strlen(game_state.current_word); i++) {
        if(tolower(game_state.current_word[i]) == letter) {
            game_state.displayed_word[i] = game_state.current_word[i];
            found = 1;
        }
    }

    // Zmniejszanie życia w razie pomyłki
    if(!found) {
        game_state.lives--;
    }
}

// Główna funkcja do obsługi klienta
void* handle_client(void* arg) {
    Client* client = (Client*)arg;
    char buffer[BUFFER_SIZE];
    char message[BUFFER_SIZE];
    char incorrect_word[MAX_WORD_LENGTH] = {0};

    // Pobieranie nicku wpisanego przez gracza przy łączeniu
    recv(client->socket, buffer, BUFFER_SIZE, 0);
    strcpy(client->nickname, buffer);

    // Informacja o dołączeniu gracza
    sprintf(message, "Gracz dołączył: %s (Liczba graczy: %d/%d)\n", client->nickname, game_state.client_count, MAX_CLIENTS);
    broadcast_message(message);

    while(1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client->socket, buffer, BUFFER_SIZE, 0);

        // Odłączanie klienta
        if(bytes_received <= 0) {
            pthread_mutex_lock(&game_state.mutex);
            for(int i = 0; i < game_state.client_count; i++) {
                if(game_state.clients[i] == client) {
                    for(int j = i; j < game_state.client_count - 1; j++) {
                        game_state.clients[j] = game_state.clients[j + 1];
                    }
                    game_state.client_count--; // Zmniejszenie aktualnej liczby klientów
                    break;
                }
            }
            pthread_mutex_unlock(&game_state.mutex);

            sprintf(message, "Gracz %s opuścił grę\n", client->nickname);
            broadcast_message(message);
            close(client->socket);
            free(client);
            return NULL;
        }

        // Obsługa komendy historia
        if(strcmp(buffer, "HISTORIA") == 0) {
            send_ranking(client->socket);
            continue;
        }

        // Oczekiwanie na rozpoczęcie gry i obsługa komendy "start"
        if(strcmp(buffer, "start") == 0 && !game_state.game_in_progress) {
            game_state.word_setter = 0;
            sprintf(message, "Gracz %s rozpoczyna grę. Oczekiwanie na hasło...\n", game_state.clients[game_state.word_setter]->nickname);
            broadcast_message(message);

            // Wysłanie informacji do gracza o podanie hasła
            if(client->id == game_state.word_setter) {
                sendto(client->socket, "Podaj hasło:\n", 13, 0, (struct sockaddr*)&client->addr, sizeof(client->addr));
            }
        }
        else if(client->id == game_state.word_setter && !game_state.game_in_progress) {
            strcpy(game_state.current_word, buffer);
            init_game();
            game_state.current_player = (game_state.word_setter + 1) % game_state.client_count;

            // Informacja o początku rundy
            sprintf(message, "Gra rozpoczęta! Hasło ma %lu liter: %s\n", 
                    strlen(game_state.current_word), game_state.displayed_word);
            broadcast_message(message);

            // Informacja o turze gracza
            sprintf(message, "Tura gracza %s\n", game_state.clients[game_state.current_player]->nickname);
            broadcast_message(message);
        }
        else if(game_state.game_in_progress && client->id == game_state.current_player) {
            if(strlen(buffer) == 1) { 
                check_letter(buffer[0]);
                sprintf(message, "Stan gry: %s (Pozostało żyć: %d)\n", game_state.displayed_word, game_state.lives);
                broadcast_message(message);
            }
            else {
                if(strcasecmp(buffer, game_state.current_word) == 0) {
                    sprintf(message, "Gratulacje! Gracz %s odgadł hasło: %s\n", client->nickname, game_state.current_word);
                    broadcast_message(message);
                    
                    // Zapis stanu gry do pliku
                    log_game_result(game_state.current_word, game_state.current_word, client->nickname, game_state.lives, NULL);
                    
                    // Rozpoczęcie nowej rundy
                    game_state.game_in_progress = 0;
                    game_state.word_setter = (game_state.word_setter + 1) % game_state.client_count; // Zmiana gracza podającego hasło
        
                    sprintf(message, "Nowa runda! Gracz %s podaje hasło.\n", game_state.clients[game_state.word_setter]->nickname);
                    broadcast_message(message);

                    if(game_state.clients[game_state.word_setter]->id == client->id) {
                        sendto(client->socket, "Podaj hasło:\n", 13, 0, (struct sockaddr*)&client->addr, sizeof(client->addr));
                    }
                    continue;
                }
                else {
                    // Zapamiętaj niepoprawnie zgadnięte słowo
                    strcpy(incorrect_word, buffer);
                    
                    sprintf(message, "Nieprawidłowe hasło! Koniec gry.\n");
                    broadcast_message(message);
                    
                    log_game_result(game_state.current_word, game_state.displayed_word, NULL, game_state.lives, incorrect_word);
                    
                    game_state.game_in_progress = 0;
                    game_state.word_setter = (game_state.word_setter + 1) % game_state.client_count;

                    sprintf(message, "Nowa runda! Gracz %s podaje hasło.\n",
                            game_state.clients[game_state.word_setter]->nickname);
                    broadcast_message(message);

                    if(game_state.clients[game_state.word_setter]->id == client->id) {
                        sendto(client->socket, "Podaj hasło:\n", 13, 0, (struct sockaddr*)&client->addr, sizeof(client->addr));
                    }
                    continue;
                }
            }

            // Sprawdzenie wygrania gry
            if(strcmp(game_state.displayed_word, game_state.current_word) == 0) {
                sprintf(message, "Gratulacje! Hasło odgadnięte: %s\n", game_state.current_word);
                broadcast_message(message);
                
                log_game_result(game_state.current_word, game_state.current_word, game_state.clients[game_state.current_player]->nickname, game_state.lives, NULL);
                
                game_state.game_in_progress = 0;
                game_state.word_setter = (game_state.word_setter + 1) % game_state.client_count;

                sprintf(message, "Nowa runda! Gracz %s podaje hasło.\n",
                        game_state.clients[game_state.word_setter]->nickname);
                broadcast_message(message);
                
                if(game_state.clients[game_state.word_setter]->id == client->id) {
                    sendto(client->socket, "Podaj hasło:\n", 13, 0, (struct sockaddr*)&client->addr, sizeof(client->addr));
                }
            }
            // Sprawdzenie przegranej
            else if(game_state.lives == 0) {
                sprintf(message, "Koniec żyć! Przegrana. Hasłem było: %s\n", game_state.current_word);
                broadcast_message(message);
                
                log_game_result(game_state.current_word, game_state.displayed_word, NULL, game_state.lives, NULL);
                
                game_state.game_in_progress = 0;
                game_state.word_setter = (game_state.word_setter + 1) % game_state.client_count;

                sprintf(message, "Nowa runda! Gracz %s podaje hasło.\n",
                        game_state.clients[game_state.word_setter]->nickname);
                broadcast_message(message);

                if(game_state.clients[game_state.word_setter]->id == client->id) {
                    sendto(client->socket, "Podaj hasło:\n", 13, 0, (struct sockaddr*)&client->addr, sizeof(client->addr));
                }
            }
            else {
                game_state.current_player = (game_state.current_player + 1) % game_state.client_count;
                if(game_state.current_player == game_state.word_setter) {
                    game_state.current_player = (game_state.current_player + 1) % game_state.client_count;
                }
                sprintf(message, "Tura gracza %s\n", 
                        game_state.clients[game_state.current_player]->nickname);
                broadcast_message(message);
            }
        }
    }
    return NULL;
}
//Funkcja do obsługi multicastu
void* multicast_listener(void* arg) {
    int mcast_sock;
    struct sockaddr_in mcast_addr;
    struct ip_mreq mreq;

    // Stworzenie gniazda do multicastu
    mcast_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(mcast_sock < 0) {
        perror("Nie można utworzyć socketu multicast");
        exit(1);
    }

    // Ustawienie opcji gniazda pozwalających na wiele gniazd na tym samym porcie
    int reuse = 1;
    if(setsockopt(mcast_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse)) < 0) {
        perror("Błąd ustawiania SO_REUSEADDR");
        close(mcast_sock);
        exit(1);
    }

    // Konfiguracja adresu i portu do multicastu
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    mcast_addr.sin_port = htons(MCAST_PORT);

    // Powiązanie gniazda z adresem
    if(bind(mcast_sock, (struct sockaddr*)&mcast_addr, sizeof(mcast_addr)) < 0) {
        perror("Błąd bindowania multicast");
        close(mcast_sock);
        exit(1);
    }

    // Dołączenie do grupy multicastowej
    mreq.imr_multiaddr.s_addr = inet_addr(MCAST_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if(setsockopt(mcast_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) < 0) {
        perror("Błąd dołączania do grupy multicast");
        close(mcast_sock);
        exit(1);
    }

    // Nasłuchiwanie zapytań multicastowych
    while(1) {
        char buffer[BUFFER_SIZE];
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int n = recvfrom(mcast_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&cli_addr, &cli_len);
        if(n < 0) {
            perror("Błąd odbierania multicast");
            continue;
        }

        buffer[n] = 0;
        if(strcmp(buffer, "HANGMAN_DISCOVER") == 0) {
            // Odpowiedź na zapytania multicastowe
            char response[16];
            strcpy(response, inet_ntoa(cli_addr.sin_addr));
            if(sendto(mcast_sock, response, strlen(response), 0, (struct sockaddr*)&cli_addr, cli_len) < 0) {
                perror("Błąd wysyłania odpowiedzi multicast");
            }
        }
    }
    return NULL;
}

int main() {
    int server_socket;
    struct sockaddr_in server_addr;
    pthread_t thread_id, mcast_thread_id;

    pthread_mutex_init(&game_state.mutex, NULL); // Incjalizacja mutexa w celu kontroli dostępu do zasobów

    // Tworzenie wątku do obsługi multicastu
    if(pthread_create(&mcast_thread_id, NULL, multicast_listener, NULL) < 0) { 
        perror("Nie można utworzyć wątku multicast");
        exit(1);
    }

    // Tworzenie gniazda
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket == -1) {
        perror("Nie można utworzyć socketu");
        exit(1);
    }

    // Konfiguracja portu i adresu
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5000);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Łączenie adresu z gniazdem
    if(bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Błąd przy bindowaniu");
        exit(1);
    }

    // Stan LISTEN
    listen(server_socket, 5);
    printf("Serwer uruchomiony. Oczekiwanie na graczy...\n");

    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

        // Informacje o połączeniach od klienta
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Nowe połączenie od klienta %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        // Odrzucanie nadmiarowych klientów
        if(game_state.client_count >= MAX_CLIENTS) {
            char* message = "Serwer jest pełny!\n";
            send(client_socket, message, strlen(message), 0);
            close(client_socket);
            continue;
        }

        Client* new_client = (Client*)malloc(sizeof(Client));
        new_client->socket = client_socket;
        new_client->id = game_state.client_count;
        new_client->addr = client_addr;

        pthread_mutex_lock(&game_state.mutex); // Zarezerwowanie dostępnu do zasobów
        game_state.clients[game_state.client_count++] = new_client;
        pthread_mutex_unlock(&game_state.mutex); // Odblokowanie dostępu

        pthread_create(&thread_id, NULL, handle_client, (void*)new_client); // Nowy wątek dla każdego klienta
        pthread_detach(thread_id); // Wątek może działać niezależnie
    }

    pthread_mutex_destroy(&game_state.mutex); // Usunięcie mutexa
    close(server_socket);
    return 0;
}