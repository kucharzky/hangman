# C Multiplayer Hangman Game with Sockets

This is a simple command-line multiplayer Hangman game implemented in C using network sockets (TCP and UDP Multicast) and POSIX threads (pthreads).

## Description

The application consists of a server and multiple clients that can connect to play Hangman together. The server manages the game state, handles client connections, and facilitates communication. Clients connect to the server, discover its IP address using UDP multicast, and interact with the game through a simple command-line interface.

## Features

*   **Multiplayer:** Supports up to 4 players simultaneously.
*   **Server Discovery:** Clients automatically discover the server's IP address on the local network using UDP multicast (Group: `224.1.1.1`, Port: `5001`).
*   **TCP Communication:** Main gameplay communication between server and clients uses TCP sockets (Port: `5000`).
*   **Turn-Based Gameplay:** Players take turns guessing letters or the entire word.
*   **Player Word Setting:** Players rotate the role of setting the secret word for others to guess.
*   **Game Logic:** Includes tracking lives (starts with 7), displaying the partially guessed word, and checking letter/word guesses.
*   **Concurrency:** The server uses pthreads to handle multiple client connections concurrently. The client uses a separate thread to receive messages non-blockingly.
*   **Game State Protection:** Uses a mutex (`pthread_mutex_t`) on the server to protect shared game state data from race conditions.
*   **Game History:** Logs game results (winner, word, lives left) to a file (`historia.txt`).
*   **History Command:** Players can type `HISTORIA` to view the game history log.
*   **Basic Commands:** Includes commands like `start` to initiate a game and `quit` for clients to leave.

## Technologies Used

*   **Language:** C
*   **Networking:**
    *   Berkeley Sockets API (`sys/socket.h`, `netinet/in.h`, `arpa/inet.h`)
    *   TCP/IP for game communication
    *   UDP Multicast for server discovery
*   **Concurrency:** POSIX Threads (pthreads - `pthread.h`)
*   **Standard Libraries:** `stdio.h`, `stdlib.h`, `string.h`, `unistd.h`, `ctype.h`, `time.h`

## Setup and Compilation

You need a C compiler (like GCC) and the pthreads library installed.

1.  **Clone the repository (if applicable):**
    ```bash
    git clone <your-repository-url>
    cd <repository-directory>
    ```
2.  **Compile the server:**
    ```bash
    gcc serwer.c -o serwer -pthread
    ```
3.  **Compile the client:**
    ```bash
    gcc klient.c -o klient -pthread
    ```
    *Note: The `-pthread` flag is crucial for linking the POSIX threads library.*

## Usage

1.  **Run the Server:**
    Open a terminal and start the server executable:
    ```bash
    ./serwer
    ```
    The server will print "Serwer uruchomiony. Oczekiwanie na graczy..." and wait for connections.

2.  **Run the Client(s):**
    Open one or more separate terminals (up to 4) and run the client executable in each:
    ```bash
    ./klient
    ```
    *   The client will first attempt to discover the server via multicast.
    *   Once found, it will print the server's IP address.
    *   It will then prompt you to enter a nickname: `Podaj nick: `
    *   Enter your desired nickname and press Enter.

## Gameplay

1.  **Joining:** As clients join, the server broadcasts join messages to all connected clients.
2.  **Starting:** When enough players have joined, one player types `start` and presses Enter.
3.  **Setting the Word:** The server designates one player (the `word_setter`) to provide the secret word. Only that player will see the "Podaj hasło:" prompt. They type the word and press Enter.
4.  **Guessing:** The game begins. The server shows the hidden word (e.g., `_ _ _ _`) and indicates whose turn it is.
5.  **Taking a Turn:** The player whose turn it is can either:
    *   Type a single letter (e.g., `a`) and press Enter.
    *   Type the full word guess (e.g., `hangman`) and press Enter.
6.  **Feedback:** The server updates the game state based on the guess (revealing letters, decreasing lives on incorrect letter guesses) and broadcasts the current state (`Stan gry: ... (Pozostało żyć: ...)`).
7.  **Winning/Losing:**
    *   The game ends if a player guesses the full word correctly.
    *   The game ends if the players run out of lives (0 lives).
    *   The game ends immediately if a player attempts to guess the full word and gets it wrong.
8.  **Next Round:** After a game ends, the server logs the result, announces the start of a new round, and the role of `word_setter` rotates to the next player. The new word setter is prompted for a word.
9.  **Viewing History:** Any player can type `HISTORIA` at any time to see the contents of the `historia.txt` log file.
10. **Quitting:** A player can type `quit` to disconnect from the server.

## How it Works (Briefly)

*   **Server:** Listens for TCP connections on port 5000. Creates a new thread for each accepted client (`handle_client`). Listens for UDP multicast packets on port 5001 (`multicast_listener`) and responds with its IP via unicast UDP. Manages the central `GameState` struct, using a mutex for safe concurrent access.
*   **Client:** Sends a UDP multicast message ("HANGMAN\_DISCOVER") to find the server. Receives the server IP via unicast UDP. Connects to the server via TCP. Uses the main thread to read user input and send it to the server. Creates a separate thread (`receive_messages`) to continuously listen for and display messages received from the server.
