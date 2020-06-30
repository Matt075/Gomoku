/*
   Program:        Gomoku Server
   Group:          Matt Burns, Hoang Ho, Kevin McDonald
   Date:           November 1, 2019
   File name:      goms.c
   Compile:        cc -lpthread -o goms goms.c
   Run:            ./goms
   Description:    This C program designs a game server to which
                   clients connect and play Gomoku. Ctrl + C to
                   kill the server.
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netdb.h>

#define HOST "freebsd2.cs.scranton.edu"                   //the hostname of the HTTP server
#define HTTPPORT "17307"                                  //the HTTP port client will be connecting to
#define BACKLOG 10                                        //how many pending connections queue will hold
#define DIM 8                                             //size of board

typedef struct OLD_GAME{
   int numMoves;
   int win;                                               //0 = keep playing, 1 = p1 win, 2 = p2 win
   char board[8][8];
}OldGame;

typedef struct PLAYER{                                    //maximum of 10 players each server
   int wins;
   int losses;
   int ties;
   int index;
   char name[21];                                              
}Player;

typedef struct NEW_GAME{ 
   int sockP1, sockP2;
   int gameNum;
   Player *P1, *P2, *scoreboard;
   OldGame* gamePtr;
}NewGame;

typedef struct RESET{                                     //an empty struct used to reset NG and OG
   /* This space is intentionally left blank. */    
}Reset;

/* Function Declarations */
int get_server_socket(char *host, char *port);            //get a server socket
int start_server(int serv_socket, int backlog);           //start server's listening
int accept_client(int serv_sock);                         //accept a connection from client
void *start_subserver(void *ptr);                         //start subserver
void print_ip(struct addrinfo *ai);                       //print server IP
void *get_in_addr(struct sockaddr *sa);                   //get internet address
void *horizontalCheck(void *ptr);                         //check 5 in a row horizontally 
void *verticalCheck(void *ptr);                           //check 5 in a row vertically
void checkGameOver(NewGame *ng, int msg[3], int i1, int i2);              //check if someone has won, or if tie
void endGame(NewGame *ng, int msg[3]);                    //end the game
void initBoard(void *ptr);                                //initialize an empty board
void printBoard(OldGame *og);                             //print board
void updateScoreboard(NewGame *ng);                       //update scoreboard after winner found
void initScoreboard(Player scoreboard[10]);               //initalizes scoreboard
void copyScoreboard(Player scoreboard[10], NewGame *ng);  //save scoreboard before erasing it
void getIndex(NewGame *ng);                               //get index of p1 and p2

int main(){
   Player scoreboard[10];
   NewGame* ng = (NewGame*)malloc(sizeof(NewGame));
   OldGame* og = (OldGame*)malloc(sizeof(OldGame));
   Reset* reset = (Reset*)malloc(sizeof(Reset));
  
   //Initialize the scoreboard.
   initScoreboard(scoreboard);
   ng->scoreboard = scoreboard;

   //Variable declaration
   int serverSocket = get_server_socket(HOST, HTTPPORT);
   int gameNum = 0;
   pthread_t subserver;

   //Server IP Information
   struct addrinfo hints, *servinfo;
   memset(&hints, 0, sizeof hints);
   hints.ai_family = PF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_PASSIVE;

   if (start_server(serverSocket, BACKLOG) == -1) {
      printf("ERROR: Server start.\n");
      exit(EXIT_FAILURE);
   }

   //Print Server IP.
   getaddrinfo(HOST, HTTPPORT, &hints, &servinfo);   
   print_ip(servinfo);

   while(1){
      printf("\nWaiting for two connections...\n");
      if ((ng->sockP1 = accept_client(serverSocket)) == -1) continue;
      if ((ng->sockP2 = accept_client(serverSocket)) == -1) continue;
      //Increment the game number.
      gameNum++;
      ng->gameNum = gameNum;

      if((pthread_create(&subserver, NULL, start_subserver, ng)) != 0)
         printf("The game server failed to start.\n");

      printf("Game #%d has started.\n", gameNum);
     
      //Reset the values in ng and og for next iteration.
      copyScoreboard(scoreboard, ng);
      ng = (void *)reset;
      og = (void *)reset;
      ng->scoreboard = scoreboard;

   }
}

int get_server_socket(char *host, char *port){
   struct addrinfo hints, *servinfo, *p;
   int status, server_socket, yes = 1;

   memset(&hints, 0, sizeof hints);
   hints.ai_family = PF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_PASSIVE;

   if ((status = getaddrinfo(host, port, &hints, &servinfo)) != 0){
      printf("getaddrinfo: %s\n", gai_strerror(status));
      exit(1);
   }

   for (p = servinfo; p != NULL; p = p ->ai_next){
      if ((server_socket = socket(p->ai_family, p->ai_socktype,
                           p->ai_protocol)) == -1){
         printf("ERROR: Socket socket.\n");
         continue;
      }

      if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
         printf("ERROR: Socket option.\n");
         continue;
      }

      if (bind(server_socket, p->ai_addr, p->ai_addrlen) == -1){
         printf("ERROR: Socket bind.\n");
         continue;
      }
      break;
   }
   freeaddrinfo(servinfo);
   return server_socket;
}

int start_server(int serv_socket, int backlog){
   int status = 0;
   if ((status = listen(serv_socket, backlog)) == -1){
      printf("ERROR: Socket listen.\n");
   }
   return status;
}

int accept_client(int serv_sock){
   int reply_sock_fd = -1;
   socklen_t sin_size = sizeof(struct sockaddr_storage);
   struct sockaddr_storage client_addr;
   char client_printable_addr[INET6_ADDRSTRLEN];

   if ((reply_sock_fd = accept(serv_sock,
      (struct sockaddr *)&client_addr, &sin_size)) == -1) 
          printf("ERROR: Socket accept.\n");
   else{
      inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr),
                client_printable_addr, sizeof client_printable_addr);
      printf("connection from %s @ %d\n", client_printable_addr, 
              ((struct sockaddr_in*)&client_addr)->sin_port);
   }
   return reply_sock_fd;
}

void *start_subserver(void *ptr){
   NewGame* ng = (NewGame*)ptr;
   OldGame* og = (OldGame*)malloc(sizeof(OldGame));
   Player* P1 = (Player*)malloc(sizeof(Player));
   Player* P2 = (Player*)malloc(sizeof(Player));
 
   //Variable declaration
   pthread_t hThread, vThread;
   int msg[3], i, j;
   int playerOne = 1, playerTwo = 2;
   int currentPlayer = 1;
   char name[21];
   //Initialize our struct values.
   initBoard(og);
   P1->wins = 0;    P2->wins = 0;
   P1->losses = 0;  P2->losses = 0;
   P1->ties =0;     P2->ties = 0;
    printf("\n how bout here\n");
   //updateScoreboard(ng);
    printf("\n how bout he2re\n");
   ng->P1 = P1;
   ng->P2 = P2;
   ng->gamePtr = og;
   ng->gamePtr->numMoves = 0;
   int player[2] = {ng->sockP1, ng->sockP2};

   //Send each player their respective player number and the board.
   send(player[0], &playerOne, sizeof(int), 0);
   send(player[0], ng->gamePtr, sizeof(OldGame), 0);
   send(player[1], &playerTwo, sizeof(int), 0);
   send(player[1], ng->gamePtr, sizeof(OldGame), 0);

   //Get and print player names.
   recv(player[0], ng->P1->name, sizeof(char[21]), 0);
   recv(player[1], ng->P2->name, sizeof(char[21]), 0);
   printf("Game #%d: Player 1's name is %s!\n", ng->gameNum, ng->P1->name);
   printf("Game #%d: Player 2's name is %s!\n", ng->gameNum, ng->P2->name);
   
   //Send the opponent's name.
   send(player[0], ng->P2->name, sizeof(char[21]), 0);
   send(player[1], ng->P1->name, sizeof(char[21]), 0);
   
   
    updateScoreboard(ng);
   //Play the game.
   while(1){ 
      //Send the current player to both players.
      send(player[0], &currentPlayer, sizeof(int), 0); 
      send(player[1], &currentPlayer, sizeof(int), 0);

      //Place stone in our board.
      recv(player[currentPlayer - 1], msg, sizeof(msg), 0);
      if(currentPlayer == 1) ng->gamePtr->board[msg[1]][msg[2]] = 'B'; 
      else ng->gamePtr->board[msg[1]][msg[2]] = 'W'; 
      ng->gamePtr->numMoves++;

      //Check for win.
      pthread_create(&hThread, NULL, horizontalCheck, ng->gamePtr);
      pthread_create(&vThread, NULL, verticalCheck, ng->gamePtr);
      pthread_join(hThread, NULL);
      pthread_join(vThread, NULL);

      //End the game if someone got 5 in a row, or if a tie occurs.
      checkGameOver(ng, msg, ng->P1->index, ng->P2->index);

      //Otherwise, keep playing.
      msg[0] = 0;
      send(player[0], msg, sizeof(&msg), 0);
      send(player[0], ng->gamePtr,sizeof(OldGame), 0);
      send(player[1], msg, sizeof(&msg), 0);
      send(player[1], ng->gamePtr,sizeof(OldGame), 0);
      currentPlayer = (ng->gamePtr->numMoves % 2) + 1;   
   }      
}

void print_ip(struct addrinfo *ai){ 
   struct addrinfo *p; 
   void *addr; 
   char *ipver; 
   char ipstr[INET6_ADDRSTRLEN]; 
   struct sockaddr_in *ipv4; struct sockaddr_in6 *ipv6; 
   short port = 0;

   for (p = ai; p !=  NULL; p = p->ai_next) {
      if (p->ai_family == AF_INET) {
         ipv4 = (struct sockaddr_in *)p->ai_addr;
         addr = &(ipv4->sin_addr);
         port = ipv4->sin_port;
         ipver = "IPV4";
      }
      else {
         ipv6= (struct sockaddr_in6 *)p->ai_addr;
         addr = &(ipv6->sin6_addr);
         port = ipv4->sin_port;
         ipver = "IPV6";
      }
      inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
      printf("Server IP: %s - %s @%d\n", ipstr, ipver, ntohs(port));
   }
}

void *get_in_addr(struct sockaddr * sa){
   if (sa->sa_family == AF_INET) {
      printf("IPV4 ");
      return &(((struct sockaddr_in *)sa)->sin_addr);
   }
   else {
      printf("IPV6 ");
      return &(((struct sockaddr_in6 *)sa)->sin6_addr);
   }
}

void *horizontalCheck(void *ptr){
   OldGame *g = (OldGame *)ptr;
   int consecP1, consecP2, x, y;
   
   //Check first 4 columns for match.
   for(x = 0; x < DIM; x++){
      for(y = 0; y < DIM; y++){
         if (g->board[x][y] == 'B' && 
             g->board[x][y] == g->board[x][y+1] && 
             g->board[x][y] == g->board[x][y+2] && 
             g->board[x][y] == g->board[x][y+3] &&
             g->board[x][y] == g->board[x][y+4]){
                consecP1 = 1;
         }
  
         else if (g->board[x][y] == 'W' &&
             g->board[x][y] == g->board[x][y+1] &&
             g->board[x][y] == g->board[x][y+2] &&
             g->board[x][y] == g->board[x][y+3] &&
             g->board[x][y] == g->board[x][y+4]){
               consecP2 = 1;
         }

         if(consecP1 == 1){ g->win = 1; break; }
         if(consecP2 == 1){ g->win = 2; break; }
      }
   }
   return NULL;
}

void *verticalCheck(void *ptr){
   OldGame *g = (OldGame *)ptr;
   int consecP1, consecP2, x, y;

   //Check first 4 rows for match.
   for(x = 0; x < DIM; x++){
      for(y = 0; y < DIM; y++){
         if (g->board[x][y] == 'B' &&
             g->board[x][y] == g->board[x+1][y] &&
             g->board[x][y] == g->board[x+2][y] &&
             g->board[x][y] == g->board[x+3][y] &&
             g->board[x][y] == g->board[x+4][y]){
               consecP1 = 1;
         }

         else if (g->board[x][y] == 'W' &&
             g->board[x][y] == g->board[x+1][y] &&
             g->board[x][y] == g->board[x+2][y] &&
             g->board[x][y] == g->board[x+3][y] &&
             g->board[x][y] == g->board[x+4][y]){
               consecP2 = 1;
         }

         if(consecP1 == 1){ g->win = 1; break; }
         if(consecP2 == 1){ g->win = 2; break; }
      }
   }
   return NULL;
}

void checkGameOver(NewGame *ng, int msg[3], int i1, int i2){
   //updateScoreboard(ng);
   int numberOfMoves = ng->gamePtr->numMoves;
   
//printf("p1 = %d, p2 = %d\n", i1, i2);
   //player 1 has won
   if (ng->gamePtr->win == 1){
      msg[0] = 1;
      ng->scoreboard[i1].wins++;
      ng->scoreboard[i2].losses++;
      endGame(ng, msg);
   }
      
   //player 2 has won
   else if (ng->gamePtr->win == 2){
      msg[0] = 2;
      ng->scoreboard[i2].wins++;
      ng->scoreboard[i1].losses++;
      endGame(ng, msg);
   }

   //tie
   else if (ng->gamePtr->numMoves == DIM * DIM - 1){
      msg[0] = 3;
      ng->scoreboard[i1].ties++;
      ng->scoreboard[i2].ties++;
      endGame(ng, msg);
   }
}

void endGame(NewGame *ng, int msg[3]){
   //updateScoreboard(ng);
   int i1 = ng->P1->index;
   int i2 = ng->P2->index;

for (int i = 0; i< 10; i++) printf("name = %s wins = %d losses = %d ties = %d index = %d\n",ng->scoreboard[i].name, ng->scoreboard[i].wins, ng->scoreboard[i].losses, ng->scoreboard[i].ties, ng->scoreboard[i].index);
   send(ng->sockP1, msg, sizeof(&msg), 0);
   send(ng->sockP1, ng->gamePtr, sizeof(OldGame), 0);
   send(ng->sockP1, &i1, sizeof(int), 0);
   send(ng->sockP1, &i2, sizeof(int), 0);
   send(ng->sockP1, &(ng->scoreboard[i1]), sizeof(Player), 0);
   send(ng->sockP1, &(ng->scoreboard[i2]), sizeof(Player), 0);  

   send(ng->sockP2, msg, sizeof(&msg), 0);
   send(ng->sockP2, ng->gamePtr, sizeof(OldGame), 0);
   send(ng->sockP2, &i1, sizeof(int), 0);
   send(ng->sockP2, &i2, sizeof(int), 0);
   send(ng->sockP2, &(ng->scoreboard[i1]), sizeof(Player), 0);
   send(ng->sockP2, &(ng->scoreboard[i2]), sizeof(Player), 0);

   if (msg[0] == 1) printf("Game #%d: %s won after %d moves.\n", ng->gameNum, ng->P1->name, ng->gamePtr->numMoves);
   else if (msg[0] == 2) printf("Game #%d: %s won after %d moves.\n", ng->gameNum, ng->P2->name, ng->gamePtr->numMoves);
   //shutdown(ng->sockP1, SHUT_WR);
   //shutdown(ng->sockP2, SHUT_WR);
   pthread_exit(NULL);   
}

void initBoard(void *ptr){
   OldGame *game = (OldGame*)ptr;
   int i, j;
   for (i = 0; i < 8; i++){
      for(j = 0; j < 8; j++){
         game->board[i][j] = '-';
      }
   }
}

void printBoard(OldGame *og){
   int i, j;
   for (i = 0; i < 8; i++){
      for (j = 0; j < 8; j++){
         printf("[ %c ]", og->board[i][j]);
      }
      printf("\n");
   }
   printf("\n");
}

void updateScoreboard(NewGame *ng){
   int i, j;
   
   //iterate through scoreboard
   for (i = 0; i < 10; i++){
      //check if player 1 has a match history
      if(strcmp(ng->scoreboard[i].name, ng->P1->name) == 0){
	       ng->P1->wins = ng->scoreboard[i].wins;
		   ng->P1->losses = ng->scoreboard[i].losses;
		   ng->P1->ties = ng->scoreboard[i].ties;
        // ng->scoreboard[i].wins = ng->P1->wins;
         //ng->scoreboard[i].losses = ng->P1->losses;
         //ng->scoreboard[i].ties = ng->P1->ties;
           ng->P1->index = i;
         break;
      }
      
      //if not, make one at the first free index
      else{
         for (j = 0; j < 10; j++){
            if (strcmp(ng->scoreboard[j].name, "") == 0){
               strcpy(ng->scoreboard[j].name, ng->P1->name);
			      ng->P1->wins = ng->scoreboard[j].wins;
				  ng->P1->losses = ng->scoreboard[j].losses;
				  ng->P1->ties = ng->scoreboard[j].losses;
          //     ng->scoreboard[j].wins = ng->P1->wins;
          //     ng->scoreboard[j].losses = ng->P1->losses;
          //     ng->scoreboard[j].ties = ng->P1->ties;
                  ng->P1->index = j;
               break;
            }
         }
      }
   
   
      //check if player 2 has a match history
      if(strcmp(ng->scoreboard[i].name, ng->P2->name) == 0){
	       ng->P2->wins = ng->scoreboard[i].wins;
		   ng->P2->losses = ng->scoreboard[i].losses;
		   ng->P2->ties = ng->scoreboard[i].ties;
       //  ng->scoreboard[k].wins = ng->P2->wins;
       //  ng->scoreboard[k].losses = ng->P2->losses;
       //  ng->scoreboard[k].ties = ng->P2->ties;
           ng->P2->index = i;
         break;
      }

      //if not, make one at the first free index
      else{
         for (j = 0; j < 10; j++){
            if (strcmp(ng->scoreboard[j].name, "") == 0){
               strcpy(ng->scoreboard[j].name, ng->P2->name);
			      ng->P1->wins = ng->scoreboard[j].wins;
				  ng->P1->losses = ng->scoreboard[j].losses;
				  ng->P1->ties = ng->scoreboard[j].losses;
          //      ng->scoreboard[j].wins = ng->P2->wins;
          //      ng->scoreboard[j].losses = ng->P2->losses;
          //      ng->scoreboard[j].ties = ng->P2->ties;
                 ng->P2->index = j;
               break;
            }
         }
         break;
      }
  
}  
}

void initScoreboard(Player scoreboard[10]){
   int i;
   for (i = 0; i < 10; i++) {
      strcpy(scoreboard[i].name, "");
      scoreboard[i].wins = 0;
      scoreboard[i].losses = 0;
      scoreboard[i].ties = 0;
      scoreboard[i].index = i;
   }
}

void copyScoreboard(Player scoreboard[10], NewGame *ng){
   int i;
   for (i = 0; i < 10; i++){
      strcpy(scoreboard[i].name, ng->scoreboard[i].name);
      scoreboard[i].wins = ng->scoreboard[i].wins;
      scoreboard[i].losses = ng->scoreboard[i].losses;
      scoreboard[i].ties = ng->scoreboard[i].ties;
      scoreboard[i].index = ng->scoreboard[i].index;
   }
}

void getIndex(NewGame *ng){
   int i, j;

   //iterate through scoreboard
   for (i = 0; i < 10; i++){
      //check if player 1 has a match history
      if(strcmp(ng->scoreboard[i].name, ng->P1->name) == 0){
         ng->P1->index = i;
         break;
      }

      //if not, make one at the first free index
      else{
         for (j = 0; j < 10; j++){
            if (strcmp(ng->scoreboard[j].name, "") == 0){
               ng->P1->index = j;
               break;
            }
         }
      }

      //check if player 2 has a match history
      if(strcmp(ng->scoreboard[i].name, ng->P2->name) == 0){
         ng->P2->index = i;
         break;
      }

      //if not, make one at the first free index
      else{
         for (j = 0; j < 10; j++){
            if (strcmp(ng->scoreboard[j].name, "") == 0){
               ng->P2->index = j;
               break;
            }
         }
         break;
      }
   }
}
