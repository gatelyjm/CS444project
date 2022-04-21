#include "net_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

#define NUM_VARIABLES 26
#define NUM_SESSIONS 128
#define NUM_BROWSER 128
#define DATA_DIR "./sessions"
#define SESSION_PATH_LEN 128

typedef struct browser_struct {
    bool in_use;
    int socket_fd;
    int session_id;
} browser_t;

typedef struct session_struct {
    bool variables[NUM_VARIABLES];
    double values[NUM_VARIABLES];
} session_t;

typedef struct node {
  int key;
  session_t* session;
  struct node* next;
} node_t;

typedef struct head {
  int size;
  node_t* list;
} head_t;

static browser_t browser_list[NUM_BROWSER];                             // Stores the information of all browsers.
// TODO: For Part 3.2, convert the session_list to a simple hashmap/dictionary.
//static session_t session_list[NUM_SESSIONS];                            // Stores the information of all sessions.
static head_t session_list;
static pthread_mutex_t browser_list_mutex = PTHREAD_MUTEX_INITIALIZER;  // A mutex lock for the browser list.
static pthread_mutex_t session_list_mutex = PTHREAD_MUTEX_INITIALIZER;  // A mutex lock for the session list.

//Inserts an element to the list found at head
//Returns false if insert was not completed
bool map_insert(head_t* list_head, const int key);

//Returns bool informing if session was found at key
//Stores the session if found in session
bool map_find(const head_t* list_head, const int key, session_t** session);

//Deletes element in list at key
//Returns true if deletion was performed
bool map_delete(head_t* list_head, const int key);

// Returns the string format of the given session.
// There will be always 9 digits in the output string.
void session_to_str(int session_id, char result[]);

// Determines if the given string represents a number.
bool is_str_numeric(const char str[]);

// Process the given message and update the given session if it is valid.
bool process_message(int session_id, const char message[]);

// Broadcasts the given message to all browsers with the same session ID.
void broadcast(int session_id, const char message[]);

// Gets the path for the given session.
void get_session_file_path(int session_id, char path[]);

// Loads every session from the disk one by one if it exists.
void load_all_sessions();

// Saves the given sessions to the disk.
void save_session(int session_id);

// Assigns a browser ID to the new browser.
// Determines the correct session ID for the new browser
// through the interaction with it.
int register_browser(int browser_socket_fd);

// Handles the given browser by listening to it,
// processing the message received,
// broadcasting the update to all browsers with the same session ID,
// and backing up the session on the disk.
void browser_handler(int browser_socket_fd);

// Starts the server.
// Sets up the connection,
// keeps accepting new browsers,
// and creates handlers for them.
void start_server(int port);

/**
 * Returns bool indicating successful insertion of element
 * Assumes user has already verified that ID is not in list already
 *
 * @param list_head head of the list of session_struct
 * @param key to be inserted in list
 */
bool map_insert(head_t* list_head, const int key)
{
  session_t* session_ptr;

  if(list_head->size >= NUM_SESSIONS)
  {
    //If there are already enough sessions, do not insert
    return false;
  }

  if(list_head->list == NULL)
  {
    node_t* new_list = malloc(sizeof(node_t));
    new_list->key = key;
    new_list->next = NULL;
    session_ptr = malloc(sizeof(session_t));
    new_list->session = session_ptr;
    list_head->list = new_list;
    (list_head->size)++;
    return true;
  }
  node_t* element = list_head->list;
  while((element->next) != NULL)
  {
    element = element->next;
  }

  node_t* new_node = malloc(sizeof(node_t));
  new_node->key = key;
  new_node->next = NULL;
  session_ptr = malloc(sizeof(session_t));
  new_node->session = session_ptr;


  element->next = new_node;
  (list_head->size)++;
  return true;
}

/**
 * Returns bool indicating successful finding of element
 * Stores found session in param session
 *
 * @param list_head head of the list of session_struct
 * @param key of element to be found
 * @param session to store the session_t if found
 */
bool map_find(const head_t* list_head, const int key, session_t** session)
{
  node_t* element = list_head->list;
  while(element != NULL)
  {
    if(element->key == key)
    {
      *session = element->session;
      return true;
    }
    element = element->next;
  }
  return false;
}

/**
 * Returns bool indicating successful deletion of element
 *
 * @param list_head head of the list of session_struct
 * @param key to be deleted in list
 */
bool map_delete(head_t* list_head, const int key)
{
  node_t* element = list_head->list;
  if(element->key == key)
  {
    list_head->list = element->next;
    (list_head->size)--;
    free(element->session);
    free(element);
    return true;
  }

  node_t* deletion;
  while(element->next != NULL)
  {
    if(element->next->key == key)
    {
      deletion = element->next;
      element->next = element->next->next;
      free(deletion->session);
      free(deletion);
      (list_head->size)--;
      return true;
    }
    element = element->next;
  }

  return false;
}

/**
 * Returns the string format of the given session.
 * There will be always 9 digits in the output string.
 *
 * @param session_id the session ID
 * @param result an array to store the string format of the given session;
 *               any data already in the array will be erased
 */
void session_to_str(int session_id, char result[]) {
    memset(result, 0, BUFFER_LEN);
    session_t* session;
    map_find(&session_list, session_id, &session);

    for (int i = 0; i < NUM_VARIABLES; ++i) {
        if (session->variables[i]) {
            char line[32];

            if (session->values[i] < 1000) {
                sprintf(line, "%c = %.6f\n", 'a' + i, session->values[i]);
            } else {
                sprintf(line, "%c = %.8e\n", 'a' + i, session->values[i]);
            }

            strcat(result, line);
        }
    }
}

/**
 * Determines if the given string represents a number.
 *
 * @param str the string to determine if it represents a number
 * @return a boolean that determines if the given string represents a number
 */
bool is_str_numeric(const char str[]) {
    if (str == NULL) {
        return false;
    }

    if (!(isdigit(str[0]) || (str[0] == '-') || (str[0] == '.'))) {
        return false;
    }

    int i = 1;
    while (str[i] != '\0') {
        if (!(isdigit(str[i]) || str[i] == '.')) {
            return false;
        }
        i++;
    }

    return true;
}

/**
 * Process the given message and update the given session if it is valid.
 *
 * @param session_id the session ID
 * @param message the message to be processed
 * @return a boolean that determines if the given message is valid
 */
bool process_message(int session_id, const char message[]) {
    char *token;
    int result_idx;
    double first_value;
    char symbol;
    double second_value;
    session_t* session;

    // TODO: For Part 3.1, write code to determine if the input is invalid and return false if it is.
    // Hint: Also need to check if the given variable does exist (i.e., it has been assigned with some value)
    // for the first variable and the second variable, respectively.

    // Makes a copy of the string since strtok() will modify the string that it is processing.
    char data[BUFFER_LEN];
    strcpy(data, message);

    map_find(&session_list, session_id, &session);

    // Processes the result variable.
    token = strtok(data, " ");
    if(strlen(token) <= 0)
    {
      //no entry
      return false;
    }
    result_idx = token[0] - 'a';

    if(strlen(token) > 1 || result_idx < 0 || result_idx > 25)
    {
      //variable is not a single character
      //or character is not a lower case letter
      return false;
    }

    // Processes "=".
    token = strtok(NULL, " ");

    if(token == NULL || strlen(token) > 1 || *token != '=')
    {
      //character must be exactly one = sign
      return false;
    }

    // Processes the first variable/value.
    token = strtok(NULL, " ");
    if(token == NULL){
      //missing variables/values
      return false;
    }
    else if (is_str_numeric(token)) {
        first_value = strtod(token, NULL);
    } else {
        int first_idx = token[0] - 'a';
        if(strlen(token) > 1 || first_idx < 0 || first_idx > 25 || !(session->variables[first_idx]))
        {
          //variable is not a single character
          //or character is not a lower case letter
          //or variable is not in use yet
          return false;
        }
        first_value = session->values[first_idx];
    }

    // Processes the operation symbol.
    token = strtok(NULL, " ");
    if (token == NULL) {
        session->variables[result_idx] = true;
        session->values[result_idx] = first_value;
        return true;
    }
    else if(strlen(token) > 1) {
      //operator must be one character
      return false;
    }
    else {
      symbol = token[0];
    }

    // Processes the second variable/value.
    token = strtok(NULL, " ");
    if(token == NULL) {
      //missing last value
      return false;
    }
    else if (is_str_numeric(token)) {
        second_value = strtod(token, NULL);
    } else {
        int second_idx = token[0] - 'a';
        if(strlen(token) > 1 || second_idx < 0 || second_idx > 25 || !(session->variables[second_idx]))
        {
          //variable is not a single character
          //or character is not a lower case letter
          //or variable is not in use yet
          return false;
        }
        second_value = session->values[second_idx];
    }

    // No data should be left over thereafter.
    token = strtok(NULL, " ");
    if(token != NULL)
    {
      //extra data
      return false;
    }

    session->variables[result_idx] = true;

    if (symbol == '+') {
        session->values[result_idx] = first_value + second_value;
    } else if (symbol == '-') {
        session->values[result_idx] = first_value - second_value;
    } else if (symbol == '*') {
        session->values[result_idx] = first_value * second_value;
    } else if (symbol == '/') {
        session->values[result_idx] = first_value / second_value;
    } else {
      //invalid operation
      return false;
    }

    return true;
}

/**
 * Broadcasts the given message to all browsers with the same session ID.
 *
 * @param session_id the session ID
 * @param message the message to be broadcasted
 */
void broadcast(int session_id, const char message[]) {
    for (int i = 0; i < NUM_BROWSER; ++i) {
        if (browser_list[i].in_use && browser_list[i].session_id == session_id) {
            send_message(browser_list[i].socket_fd, message);
        }
    }
}

/**
 * Gets the path for the given session.
 *
 * @param session_id the session ID
 * @param path the path to the session file associated with the given session ID
 */
void get_session_file_path(int session_id, char path[]) {
    sprintf(path, "%s/session%d.dat", DATA_DIR, session_id);
}

/**
 * Loads every session from the disk one by one if it exists.
 */
void load_all_sessions() {
    for(int i=0; i<NUM_SESSIONS; ++i){
        char path[128];
        get_session_file_path(i, path);

        FILE *fin=fopen(path,"r");
        if (fin==NULL){
            continue;
        }
        fread(&session_list[i],sizeof(session_t),1, fin);
        fclose(fin);
        
    }
    
    // TODO: For Part 1.1, write your file operation code here.
    // Hint: Use get_session_file_path() to get the file path for each session.
    //       Don't forget to load all of sessions on the disk.
}

/**
 * Saves the given sessions to the disk.
 *
 * @param session_id the session ID
 */
void save_session(int session_id) {
    
    char path[128];
    get_session_file_path(session_id, path);
    FILE *fin=fopen(path,"w");
    fwrite(&session_list[session_id],sizeof(session_t),1, fin);
    fclose(fin);
    // TODO: For Part 1.1, write your file operation code here.
    // Hint: Use get_session_file_path() to get the file path for each session.
}

/**
 * Assigns a browser ID to the new browser.
 * Determines the correct session ID for the new browser through the interaction with it.
 *
 * @param browser_socket_fd the socket file descriptor of the browser connected
 * @return the ID for the browser
 */
int register_browser(int browser_socket_fd) {
    int browser_id;
    session_t* session;


    // TODO: For Part 2.2, identify the critical sections where different threads may read from/write to
    //  the same shared static array browser_list and session_list. Place the lock and unlock
    //  code around the critical sections identified.

    for (int i = 0; i < NUM_BROWSER; ++i) {
        if (!browser_list[i].in_use) {
            browser_id = i;
            browser_list[browser_id].in_use = true;
            browser_list[browser_id].socket_fd = browser_socket_fd;
            break;
        }
    }

    char message[BUFFER_LEN];
    receive_message(browser_socket_fd, message);

    int session_id = strtol(message, NULL, 10);
    if (session_id == -1)
    {
        while(true)
        {
            session_id = rand() % 10000; //Generates a random number from 0 to 99999
            if (!map_find(&session_list, session_id, &session))
            {
                map_insert(&session_list, session_id);
                break;
            }
        }
    }
    browser_list[browser_id].session_id = session_id;

    sprintf(message, "%d", session_id);
    send_message(browser_socket_fd, message);

    return browser_id;
}

/**
 * Handles the given browser by listening to it, processing the message received,
 * broadcasting the update to all browsers with the same session ID, and backing up
 * the session on the disk.
 *
 * @param browser_socket_fd the socket file descriptor of the browser connected
 */
void browser_handler(int browser_socket_fd) {
    int browser_id;

    browser_id = register_browser(browser_socket_fd);

    int socket_fd = browser_list[browser_id].socket_fd;
    int session_id = browser_list[browser_id].session_id;

    printf("Successfully accepted Browser #%d for Session #%d.\n", browser_id, session_id);

    while (true) {
        char message[BUFFER_LEN];
        char response[BUFFER_LEN];

        receive_message(socket_fd, message);
        printf("Received message from Browser #%d for Session #%d: %s\n", browser_id, session_id, message);

        if ((strcmp(message, "EXIT") == 0) || (strcmp(message, "exit") == 0)) {
            close(socket_fd);
            pthread_mutex_lock(&browser_list_mutex);
            browser_list[browser_id].in_use = false;
            pthread_mutex_unlock(&browser_list_mutex);
            printf("Browser #%d exited.\n", browser_id);
            return;
        }

        if (message[0] == '\0') {
            continue;
        }

        bool data_valid = process_message(session_id, message);
        if (!data_valid) {
            // TODO: For Part 3.1, add code here to send the error message to the browser.
            broadcast(session_id, "ERROR");
            continue;
        }

        session_to_str(session_id, response);
        printf("finished session_to_str\nresponse: %s\n", response);
        broadcast(session_id, response);

        save_session(session_id);
    }
}

/**
 * Starts the server. Sets up the connection, keeps accepting new browsers,
 * and creates handlers for them.
 *
 * @param port the port that the server is running on
 */
void start_server(int port) {
    // Loads every session if there exists one on the disk.
    load_all_sessions();

    // Creates the socket.
    int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Binds the socket.
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);
    if (bind(server_socket_fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        perror("Socket bind failed");
        exit(EXIT_FAILURE);
    }

    // Listens to the socket.
    if (listen(server_socket_fd, SOMAXCONN) < 0) {
        perror("Socket listen failed");
        exit(EXIT_FAILURE);
    }
    printf("The server is now listening on port %d.\n", port);

    // Main loop to accept new browsers and creates handlers for them.
    while (true) {
        struct sockaddr_in browser_address;
        socklen_t browser_address_len = sizeof(browser_address);
        int browser_socket_fd = accept(server_socket_fd, (struct sockaddr *) &browser_address, &browser_address_len);
        if ((browser_socket_fd) < 0) {
            perror("Socket accept failed");
            continue;
        }

        // Starts the handler thread for the new browser.
        // TODO: For Part 2.1, creat a thread to run browser_handler() here.
        browser_handler(browser_socket_fd);
    }

    // Closes the socket.
    close(server_socket_fd);
}

/**
 * The main function for the server.
 *
 * @param argc the number of command-line arguments passed by the user
 * @param argv the array that contains all the arguments
 * @return exit code
 */
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    if (argc == 1) {
    } else if ((argc == 3)
               && ((strcmp(argv[1], "--port") == 0) || (strcmp(argv[1], "-p") == 0))) {
        port = strtol(argv[2], NULL, 10);

    } else {
        puts("Invalid arguments.");
        exit(EXIT_FAILURE);
    }

    if (port < 1024) {
        puts("Invalid port.");
        exit(EXIT_FAILURE);
    }

    start_server(port);

    exit(EXIT_SUCCESS);
}
