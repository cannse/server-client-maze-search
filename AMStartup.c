/* ========================================================================== */
/* File: AMStartup.c
 *
 * Author: Troy Palmer and Sean Cann
 * Date: 5.28.2015
 *
 * Overview: Simulates a group of individuals searching for one another
 * in a perfect maze (e.g. there is only one path from any two points).
 * AMStartup.c initializes avatar clients and avatar threads and carries out
 * communication with server to guide each avatar through the maze 
 * 
 * 
 *
 * Input/Command line options:
 *
 * 1. -n nAvatars: An (int) corresponding to the number of Avatars in the maze
 *
 * 2. -d Difficult: An (int) indicating level of difficulty [0,9]
 *
 * 3. -h Hostname: A (char *) representing server hostname (either stowe.cs.d..
 *or carter.cs.dart..) 
 *
 */
/* ========================================================================== */

// ---------------- System includes

#include <stdio.h>                           
#include <stdlib.h>
#include <errno.h>
#include <string.h>   
#include <strings.h>                       // argument checking
#include <unistd.h>
#include <gtk/gtk.h>
#include <getopt.h> // argument parsing
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>// hostent structure 
#include <time.h> // getting date/time
#include <pthread.h>


// ---------------- Local includes 

#include "amazing.h"

// ---------------- Constant definitions

#define MAX_SIZE 1000
#define MAX_WINDOW_SIZE 800

// ---------------- Structures/Types

typedef struct AvatarInitData {
  int AvatarId;
  int nAvatars;
  int difficulty;
  struct hostent *server;
  AM_Message message;
} AvatarInitData;

typedef struct MazeSquareData {
  int northSide;  // for the sides: -1 = unknown, 0 = blocked, 1 = open
  int eastSide;
  int southSide;
  int westSide;
} MazeSquareData;

// ---------------- Private variables

FILE *logfile;
int mazeSolved = 0;
int mazeWidth, mazeHeight;
MazeSquareData maze[MAX_SIZE][MAX_SIZE];

static GdkPixmap *pixmap = NULL;
int size_multiplier;
static int currently_drawing = 0;


// ---------------- Private prototypes

int SendMoveMessage(int avatarId, int directionToMove, int sockfd);

void *InitiateAvatar(void *data);

int StartThreads(int nAvatars, int difficulty, AM_Message message, struct hostent *server);

char* DetermineLogfile(int nAvatars, int difficulty);

int SetMazeSquareSide(int x, int y, int direction, int mode);

int ConvertDirection(int x, int y, int relativeDirection);

gboolean on_window_configure_event(GtkWidget *da, GdkEventConfigure *event, gpointer user_data);

gboolean on_window_expose_event(GtkWidget *da, GdkEventExpose *event, gpointer user_data);

void *do_draw(void *ptr);

int display_window(int *argc, char ***argv, int w, int h);

gboolean timer_exe(GtkWidget * window);

/* ========================================================================== */


/*
 *
 * SetMazeSquareSide - sets a side (direction aka N/S/E/W) in the maze[x][y] 2D array to
 * be "mode." Mode is an int, either 0 for "blocked" or 1 for "open" (or -1 for "unknown").
 * It also sets the opposite side in the square adjacent to the given square to be "mode,"
 * but only if that square exists.
 *
 * Returns an boolean indicating success
 *
 */
int SetMazeSquareSide(int x, int y, int direction, int mode) {

  if (direction == M_NORTH) {
    maze[x][y].northSide = mode;
    if (y - 1 >= 0) {
      maze[x][y - 1].southSide = mode;
    }
  }

  else if (direction == M_EAST) {
    maze[x][y].eastSide = mode;
    if (x + 1 <= mazeWidth) {
      maze[x + 1][y].westSide = mode;
    }
  }

  else if (direction == M_SOUTH) {
    maze[x][y].southSide = mode;
    if (y + 1 <= mazeHeight) {
      maze[x][y + 1].northSide = mode;
    }
  }

  else { 
    maze[x][y].westSide = mode;
    if (x - 1 >= 0) {
      maze[x - 1][y].eastSide = mode;
    }
  }

  return 1;
}


/*
 *
 * ConvertDirection - takes a relative direction (right/left/etc) as input
 *
 * Returns the status of a maze square direction (northSide/southSide/etc) at (x,y)
 *
 */
int ConvertDirection(int x, int y, int relativeDirection) {

  if (relativeDirection == M_NORTH) {
    return maze[x][y].northSide;
  }

  else if (relativeDirection == M_EAST) {
    return maze[x][y].eastSide;
  }

  else if (relativeDirection == M_SOUTH) {
    return maze[x][y].southSide;
  }

  else {
    return maze[x][y].westSide;
  }

  return -1;
}


/*
 *
 * SendMoveMessage - sends an AM_AVATAR_MOVE message to the server
 *
 * Returns 0 if the execution was unsuccessful (and returns 1 otherwise)
 *
 */
int SendMoveMessage(int avatarId, int directionToMove, int sockfd) {

  /* Create message to send */
  AM_Message *amAvatarMove = calloc(1, sizeof(AM_Message));

  amAvatarMove->type = htonl(AM_AVATAR_MOVE); // Message type (init)
  amAvatarMove->avatar_move.AvatarId = htonl(avatarId); // id of current avatar
  amAvatarMove->avatar_move.Direction = htonl(directionToMove); // direction of movement

  /* Send message */
  if (send(sockfd, amAvatarMove, sizeof(AM_Message), 0) == -1) {
    fprintf(stderr, "Error: Failed to send AM_AVATAR_MOVE message to server.\n");
    free(amAvatarMove);
    return(0);
  }

  return(1);
}


void *OpenFrame(void *data) {


  AM_Message *params = ((AM_Message *) data);

  int mazeHeight = ntohl(params->init_ok.MazeHeight);
  int mazeWidth = ntohl(params->init_ok.MazeWidth);

  printf("Height %d Width %d\n", mazeHeight, mazeWidth);
  size_multiplier = MAX_WINDOW_SIZE / mazeWidth;
  display_window(NULL, NULL, mazeWidth, mazeHeight);

  return NULL;

}


/*
 *
 * InitiateAvatar - begins the execution of each Avatar thread
 *
 * Pseudocode:
 *
 */
void *InitiateAvatar(void *data) {


  /* Parse thread parameters */

  AvatarInitData *params = ((AvatarInitData *) data);

  int avatarId = params->AvatarId;
  // int nAvatars = params->nAvatars;
  // int difficulty = params->difficulty;
  struct hostent *server = params->server;
  AM_Message initMessage = params->message;

  printf("Starting thread for Avatar number %d\n", avatarId);



  /* Create Socket */

  int sockfd2;
  struct sockaddr_in servAddr2;

  if ((sockfd2 = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
     fprintf(stderr, "Error: Unable to create socket for Avatar number %d.\n", avatarId);
     return(0);
  }

  // Initialize buffer to 0s
  bzero((char *) &servAddr2, sizeof(servAddr2));

  servAddr2.sin_family = AF_INET;

  // Convert maze port to correct format
  int mazePort = ntohl(initMessage.init_ok.MazePort);
  servAddr2.sin_port = htons(mazePort);

  // Copy address into sockaddr_in struct
  bcopy((char *)server->h_addr_list[0], (char *)&servAddr2.sin_addr.s_addr, server->h_length);



  /* Connect to server */

  while (connect(sockfd2, (struct sockaddr *) &servAddr2, sizeof(servAddr2)) == -1) {
    fprintf(stderr, "Error: Unable to connect to the server on maze port. Errno: %s\n", strerror(errno));
  }
  fprintf(stdout, "Connection to server on maze port established.\n");



  /* Send AM_AVATAR_READY message to server */

  AM_Message *amAvatarReady = calloc(1, sizeof(AM_Message));

  amAvatarReady->type = htonl(AM_AVATAR_READY); // Message type (AM_AVATAR_READY)
  amAvatarReady->avatar_ready.AvatarId = htonl(avatarId); // Avatar ID

  while (send(sockfd2, amAvatarReady, sizeof(AM_Message), 0) == -1) {
    fprintf(stderr, "Error: Failed to send AM_AVATAR_READY message to server.\n");
  }
  // free(amAvatarReady);



  /* Begin Navigation */

  int straight, right, backward, left;  // relative to avatar
  int orientation, upcomingMove;        // relative to environment
  int prevX, prevY;
  int moveNumber = 1;

  int firstIteration = 1; // true


  /* Avatar struct for graphics updating */

  Avatar *avatar = calloc(1, sizeof(Avatar));
  avatar->id = avatarId;


  orientation = M_NORTH;

  straight = M_NORTH;
  right = M_EAST;
  backward = M_SOUTH;
  left = M_WEST;

  upcomingMove = right;

  printf(" AVATAR ID: %d\n sockfd: %d\n orientation: %d\n upcomingMove: %d\n straight: %d\n right: %d\n backward: %d\n left: %d\n",avatarId,  sockfd2, orientation, upcomingMove, straight, right, backward, left);


  /* Keep first avatar stationary */

  if (avatarId == 0) {
    upcomingMove = M_NULL_MOVE;
  }


  /* Navigate with the remaining avatars until maze is solved */

  while (!mazeSolved) {


    /* Listen for AM_AVATAR_TURN message from server */

    AM_Message amAvatarTurn;
    memset(&amAvatarTurn, 0, sizeof(amAvatarTurn));

    int recvResponse = recv(sockfd2, &amAvatarTurn, sizeof(amAvatarTurn), 0);

    if (recvResponse == 0) {
      fprintf(stderr, "Error: Avatar ID %d: No AM_AVATAR_TURN message available from server.\n", avatarId);
      // free(amAvatarTurn);
      continue;
    }

    else if (recvResponse == -1) {
      fprintf(stderr, "Error: Avatar ID %d Failed to receive AM_AVATAR_TURN message from server.\n", avatarId);
      // free(amAvatarTurn);
      continue;
    }

    else if (IS_AM_ERROR(amAvatarTurn.type)) {
      fprintf(stderr, "Received error message from server. Exiting.\n");
      return NULL;
    }


    /* Continue movement of avatars */

    else if (ntohl(amAvatarTurn.type) == AM_AVATAR_TURN) {

      /* Set initial X,Y values */

      if (firstIteration) {
        prevX = ntohl(amAvatarTurn.avatar_turn.Pos[avatarId].x);
        prevY = ntohl(amAvatarTurn.avatar_turn.Pos[avatarId].y);
      }


      /* Update position if it is this avatar's turn */

      if (ntohl(amAvatarTurn.avatar_turn.TurnId) == avatarId) {


        /* Store updated x and y values */

        int x = ntohl(amAvatarTurn.avatar_turn.Pos[avatarId].x);
        int y = ntohl(amAvatarTurn.avatar_turn.Pos[avatarId].y);

        printf("Avatar %d (X,Y) = (%d,%d)\n", avatarId, x, y);


        /* Keep immobile ones from moving */

        if (upcomingMove == M_NULL_MOVE) {
          SendMoveMessage(avatarId, upcomingMove, sockfd2);
          continue;
        }


        /* If we haven't moved, make a different move */

        if ((x == prevX) && (y == prevY)) { 

          /* Update maze data structure */
          if (!firstIteration) {
            SetMazeSquareSide(prevX, prevY, upcomingMove, 0);
          }
          firstIteration = 0;

          // this if-ladder sets upcomingMove to be the next on the list (right -> straight -> left -> backward)
          if (upcomingMove == right && (ConvertDirection(prevX, prevY, straight) == -1 || ConvertDirection(prevX, prevY, straight) == 1) ) {
            upcomingMove = straight;
          }

          else if (upcomingMove == straight && (ConvertDirection(prevX, prevY, left) == -1 || ConvertDirection(prevX, prevY, left) == 1) ) {
            upcomingMove = left;
          }

          else if (upcomingMove == backward) {
            upcomingMove = right;
          }

          else {
            upcomingMove = backward;
          }
        }


        /* If the move is successful, update orientation */

        else { 

          /* Update maze data structure */
          SetMazeSquareSide(prevX, prevY, upcomingMove, 1);

          /* Freeze avatar if it finds the stationary one */

          if ((x == ntohl(amAvatarTurn.avatar_turn.Pos[0].x)) && (y == ntohl(amAvatarTurn.avatar_turn.Pos[0].y))) { 
            upcomingMove = M_NULL_MOVE;
          }


          else {

            prevX = x;
            prevY = y;

            /* Update Avatar struct for graphics */
            avatar->pos.x = htonl(x);
            avatar->pos.y = htonl(y);


            pthread_t updateGraphics;
            int updateFailed;
            updateFailed = pthread_create(&updateGraphics, NULL, do_draw, avatar);

            if (updateFailed) {
              fprintf(stderr, "Failed to update graphics window.\n");
              exit(0);
            }

            fprintf(logfile, "Avatar ID: %d (x,y) Position: (%d,%d) Move Number: %d\n", avatarId, prevX, prevY, moveNumber++);


            orientation = upcomingMove; // orients the agent in the direction it just moved 

            /* Set relative direction variables */

            // printf("Updating orientation.\n");
            if (orientation == M_NORTH) { //    N                                                                                                                     
              straight = M_NORTH;         //  W   E                                                                                                                     
              right = M_EAST;             //    S                                                                                                                           
              backward = M_SOUTH;
              left = M_WEST;
            }

            else if (orientation == M_EAST) {
              straight = M_EAST;
              right = M_SOUTH;
              backward = M_WEST;
              left = M_NORTH;
            }

            else if (orientation == M_SOUTH) {
              straight = M_SOUTH;
              right = M_WEST;
              backward = M_NORTH;
              left = M_EAST;
            }

            else {
              straight = M_WEST;
              right = M_NORTH;
              backward = M_EAST;
              left = M_SOUTH;
            }

            /* Determine relative direction */
            
            if ((ConvertDirection(prevX, prevY, right) == -1 || ConvertDirection(prevX, prevY, right) == 1) ) { // NOTE: also need to do this instead of just resetting move to just be "right" after successful
              upcomingMove = right;
            }

            else if ((ConvertDirection(prevX, prevY, straight) == -1 || ConvertDirection(prevX, prevY, straight) == 1) ) {
              upcomingMove = straight;
            }

            else if ((ConvertDirection(prevX, prevY, left) == -1 || ConvertDirection(prevX, prevY, left) == 1) ) {
              upcomingMove = left;
            }

            else if (upcomingMove == backward) {
              upcomingMove = right;
            }

            else {
              upcomingMove = backward;
            }
          }
        }
        SendMoveMessage(avatarId, upcomingMove, sockfd2); // send AM_AVATAR_MOVE message to server with avatarId and upcomingMove (aka: move in direction)
      }
    }   

    /* If there are no more turns remaining for the avatar */

    else if (ntohl(amAvatarTurn.type) == AM_AVATAR_OUT_OF_TURN) {
      printf("Avatar is out of turn.\n");
      exit(0);

    }

    else if (ntohl(amAvatarTurn.type) == AM_TOO_MANY_MOVES) {
      printf("Avatar has taken too many moves.\n");
      exit(0);

    }

    else if (ntohl(amAvatarTurn.type) == AM_SERVER_TIMEOUT) {
      printf("Server timed out.\n");
      exit(0);

    }

    else if (ntohl(amAvatarTurn.type) == AM_SERVER_DISK_QUOTA) {
      printf("Server has reached disk quota.\n");
      exit(0);

    }

    else if (ntohl(amAvatarTurn.type) == AM_SERVER_OUT_OF_MEM) {
      printf("Server ran out of memory.\n");
      exit(0);

    }

    /* If the maze has been solved */
    else if (ntohl(amAvatarTurn.type) == AM_MAZE_SOLVED) {

      printf("Maze Solved!\n");

      int hash = ntohl(amAvatarTurn.maze_solved.Hash);
      int nMoves = ntohl(amAvatarTurn.maze_solved.nMoves);
      int endDifficulty = ntohl(amAvatarTurn.maze_solved.Difficulty);
      int endAvatars = ntohl(amAvatarTurn.maze_solved.nAvatars);

      fprintf(logfile, "Hash: %d nMoves: %d Difficulty: %d nAvatars: %d\n", hash, nMoves, endDifficulty, endAvatars);
      exit(0);
    }
  }
  return NULL;
}


/*
 *
 * StartThreads - starts n threads with pthread_create and builds a struct of 
 *    parameters for each
 *
 * Returns an int indicating successful launch of all child processes
 *
 */
int StartThreads(int nAvatars, int difficulty, AM_Message message, struct hostent *server) {

  int avatarId = 0;

  /* Start n threads corresponding to n avatars */

  while (avatarId < nAvatars) {

    /* Define parameters for the avatar */

    AvatarInitData *params;
    params = calloc(1, sizeof(AvatarInitData));
    params->AvatarId = avatarId;
    params->nAvatars = nAvatars;
    params->difficulty = difficulty;
    params->server = server;
    params->message = message;


    /* Initialize thread */

    pthread_t avatarThread;
    int avatar = pthread_create(&avatarThread, NULL, InitiateAvatar, params);

    if (avatar) {
      fprintf(stderr, "Failed to create thread.\n");
      return(0);
    }


    avatarId++;
  }
  return (1);
}




/*
 *
 * DetermineLogfile - generates logfile string based on number of 
 * avatars and set difficulty.
 *
 * Returns a string corresponding to the name of the logfile in the
 * following format:
 *     "Amazing_username_nAvatars_difficulty.log"
 *
 */
char* DetermineLogfile(int nAvatars, int difficulty) {

  char *filename = NULL;

  char *username = getenv("USER");

  /* Allocate space for filename */
  filename = (char *)malloc((strlen("Amazing___.log")*sizeof(char) + strlen(username))*sizeof(char) + sizeof(int)*2 + 1);
  if (filename == NULL) {
  fprintf(stderr, "Unable to allocate memory for filename.\n");
  return (NULL);
  }

  /* Store string as filename */
  sprintf(filename, "Amazing_%s_%d_%d.log", username, nAvatars, difficulty);

  return filename;
}




gboolean on_window_configure_event(GtkWidget *da, GdkEventConfigure *event, gpointer user_data) {
  static int oldw = 0;
  static int oldh = 0;

  // create a pixmap
  if (oldw != event->width || oldh != event->height) {
    GdkPixmap *tmppixmap = gdk_pixmap_new(da->window, event->width, event->height, -1);
    // copy the old to the new
    int minw = oldw, minh = oldh;
    if (event->width < minw) { minw = event->width; }
    if (event->height < minh) { minh = event->height; }
    gdk_draw_drawable(tmppixmap, da->style->fg_gc[GTK_WIDGET_STATE(da)], pixmap, 0, 0, 0, 0, minw, minh);

    // get rid of old pixmap
    g_object_unref(pixmap);
    pixmap = tmppixmap;
  }
  oldw = event->width;
  oldh = event->height;
  return TRUE;
}

gboolean on_window_expose_event(GtkWidget *da, GdkEventExpose *event, gpointer user_data) {
  gdk_draw_drawable(da->window, da->style->fg_gc[GTK_WIDGET_STATE(da)], pixmap, 
        event->area.x, event->area.y, 
        event->area.x, event->area.y,
        event->area.width, event->area.height);
  return TRUE;
}


void *do_draw(void *ptr) {
  currently_drawing = 1;
  gdk_threads_enter();  
  
  int width, height;
  
  gdk_drawable_get_size(pixmap, &width, &height);
  gdk_threads_leave();

  // create a surface to draw on
  cairo_surface_t *cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create(cst);

  //  static int i = 0;
  // ++i; i = i % 300;
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);

    Avatar *avatar = ((Avatar *)ptr);

  int av_id = avatar->id;
  int av_x = ntohl(avatar->pos.x);
  int av_y = ntohl(avatar->pos.y);

  
  int y, x;
  for (x=0; x<(width + 1); x = x + size_multiplier) {
    for (y=0; y<(height + 1); y = y + size_multiplier) { 
      cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);

      // The wall is blocked
      cairo_set_source_rgb(cr, 0, 0, 0);
      if (maze[x / size_multiplier][y / size_multiplier].northSide == 0) { // A horizontal line
        // printf("North border\n");  
        cairo_move_to(cr, x, y);
        cairo_line_to(cr, x + size_multiplier, y);      
        cairo_stroke(cr);
      }
      if (maze[x / size_multiplier][y / size_multiplier].westSide == 0) { 
        // printf("East border\n");
        cairo_move_to(cr, x, y);
        cairo_line_to(cr, x, y + size_multiplier);
        cairo_stroke(cr);
      }
      if (maze[x / size_multiplier][y / size_multiplier].southSide == 0) {
        // printf("South border\n");
        cairo_move_to(cr, x, y + size_multiplier);
        cairo_line_to(cr, x + size_multiplier, y + size_multiplier);      
        cairo_stroke(cr); 
      }
      if (maze[x / size_multiplier][y / size_multiplier].eastSide == 0) {
        cairo_move_to(cr, x + size_multiplier, y);
        cairo_line_to(cr, x + size_multiplier, y + size_multiplier);
        cairo_stroke(cr);
      }

      // The avatar is in the position
      if (((x / size_multiplier) == av_x) && ((y / size_multiplier) == av_y)) { 
        printf("Avatar\n");
        cairo_move_to(cr, x + size_multiplier/2, y + size_multiplier/2);
        char id[25];
        sprintf(id, "%d", av_id);
        cairo_show_text(cr, id);
      }

    }
  }

  //do not access gdkPixmap outside gtk_main()
  gdk_threads_enter();

  cairo_t *cr_pixmap = gdk_cairo_create(pixmap);
  cairo_set_source_surface(cr_pixmap, cst, 0, 0);
  cairo_paint(cr_pixmap);
  cairo_destroy(cr_pixmap);

  gdk_threads_leave();

  cairo_surface_destroy(cst);

  currently_drawing = 0;
  return NULL;
}

gboolean timer_exe(GtkWidget * window) {

  static gboolean first_execution = TRUE;

  int drawing_status = g_atomic_int_get(&currently_drawing);
  
  if (drawing_status == 0) {
    static pthread_t thread_info;
    int iret;
    if(first_execution != TRUE) {
      pthread_join(thread_info, NULL);
    }
    iret = pthread_create( &thread_info, NULL, do_draw, NULL);
    if (iret) {
      fprintf(stderr, "Unable to create thread to draw canvas.\n");
    }
  }

  int width, height;
  gdk_drawable_get_size(pixmap, &width, &height);
  gtk_widget_queue_draw_area(window, 0, 0, width, height);

  first_execution = FALSE;
  return TRUE;

}



int display_window(int *argc, char ***argv, int w, int h) {
  
  gdk_threads_init();
  gdk_threads_enter();

  gtk_init(argc, argv); // pointers to the original arguments to the main function

  GtkWidget* window;
  window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(G_OBJECT(window), "expose_event", G_CALLBACK(on_window_expose_event), NULL);
  g_signal_connect(G_OBJECT(window), "configure_event", G_CALLBACK(on_window_configure_event), NULL);

  gtk_widget_set_size_request(window, w * size_multiplier, h * size_multiplier);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

  gtk_widget_show_all(window);

  pixmap = gdk_pixmap_new(window->window, w * size_multiplier, h * size_multiplier, -1);
  gtk_widget_set_app_paintable(window, TRUE);
  gtk_widget_set_double_buffered(window, FALSE);

  (void)g_timeout_add(33, (GSourceFunc)timer_exe, window);

  gtk_main();
  gdk_threads_leave();

  return 0;
}


int main(int argc, char* argv[]) {


  /* Argument Checking */

  char *program = argv[0];

  int nAvatars, difficulty;
  nAvatars = difficulty = -1;
  char *hostname = NULL;


  int ch;
  char *end;
  long val = -1;
  struct hostent *server;
  while ((ch = getopt(argc, argv, "n:d:h:")) != -1)
    switch(ch)
    {

      /* nAvatars */
      case 'n':

        val = strtol(optarg, &end, 0);

        if (!optarg){
          fprintf(stderr, "[%s] Usage: [-n nAvatars] requires an argument.\n", program);
          return(0);
        }

        if (val <= 1 || val > AM_MAX_AVATAR || strlen(end) != 0) {
          fprintf(stderr, "[%s] Usage: [-n nAvatars] requires an a positive integer greater than 1 and less than 10.\n", program);
          return(0);
        }

        nAvatars = (int) val;
        val = -1;
        break;

      /* Difficulty */
      case 'd':

        val = strtol(optarg, &end, 0);

        if (!optarg){
          fprintf(stderr, "[%s] Usage: [-d difficulty] requires an argument.\n", program);
          return(0);
        }
          
        if (val < 0 || val > AM_MAX_DIFFICULTY || strlen(end) != 0) {
          fprintf(stderr, "[%s] Usage: [-n difficulty] requires an integer between 0 and 9.\n", program);
          return(0);
        }

        difficulty = val;
        break;

      /* Hostname */
      case 'h':

        /* Check that a server under hostname exists and get IP */
        hostname = optarg;
        if ((server = gethostbyname(hostname)) == NULL) {         
          fprintf(stderr, "[%s] Error: Unable to identify host.\n", program);
          return(0);
        }

        break;

      default:
          fprintf(stderr, "[%s] Usage: [-n nAvatars] [-d difficulty] [-h hostname]\n", program);
          return(0);
      }

  if (nAvatars == -1) {
     fprintf(stderr, "[%s] Usage: [-n nAvatars] not specified.\n", program);
     return(0);
  }
  if (difficulty == -1) {
     fprintf(stderr, "[%s] Usage: [-d difficulty] not specified.\n", program);
     return(0);
  }
  if (hostname == NULL) {
     fprintf(stderr, "[%s] Usage: [-h hostname] not specified.\n", program);
     return(0);
  }

  fprintf(stdout, "Arguments successfully passed, attempting to establish connection...\n");

  /* Create socket */

  int sockfd;
  struct sockaddr_in servAddr;

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
     fprintf(stderr, "[%s] Error: Unable to create socket.\n", program);
     return(0);
  }

  bzero((char *) &servAddr, sizeof(servAddr)); // Initialize buffer to 0s

  servAddr.sin_family = AF_INET; // Routine

  printf("Server Port: %d\n", htons(atoi(AM_SERVER_PORT)));
  servAddr.sin_port = htons(atoi(AM_SERVER_PORT)); // Convert port no. to network byte order

  // Copy address into sockaddr_in struct
  bcopy((char *)server->h_addr_list[0], (char *)&servAddr.sin_addr.s_addr, server->h_length); // Address



  /* Connect to server */

  if (connect(sockfd, (struct sockaddr *) &servAddr, sizeof(servAddr)) == -1) {
     fprintf(stderr, "[%s] Error: Unable to connact to the server.\n", program);
     return(0);
  }
  else {
     fprintf(stdout, "[%s]: Connection to server established.\n", program);
  }



  /* Generate AM_INIT message and write to server */

  AM_Message *amInit = calloc(1, sizeof(AM_Message));

  amInit->type = htonl(AM_INIT); // Message type (init)
  amInit->init.nAvatars = htonl(nAvatars); // Number of avatars
  amInit->init.Difficulty = htonl(difficulty); // Maze difficulty

  if (send(sockfd, amInit, sizeof(AM_Message), 0) == -1) {
    fprintf(stderr, "[%s] Error: Failed to send AM_INIT message to server.\n", program);
    free(amInit);
    return(0);
  }


  /* Listen for and get info from AM_INIT_OK from server */

  AM_Message amInitOk;
  memset(&amInitOk, 0, sizeof(amInitOk));

  int recvResponse = recv(sockfd, &amInitOk, sizeof(AM_Message), 0);

  if (recvResponse == 0) {
    fprintf(stderr, "[%s] Error: No AM_INIT_OK message available from server.\n", program);
    free(amInit);
    return(0);  
  }
  if (recvResponse == -1) {
    fprintf(stderr, "[%s] Error: Failed to receive AM_INIT_OK message from server.\n", program);
    free(amInit);
    return(0);
  }

  if (IS_AM_ERROR(amInitOk.type)) {
    fprintf(stderr, "[%s] Received error message from server. Exiting.\n", program);
    return(0);
  }

  mazeWidth = ntohl(amInitOk.init_ok.MazeWidth);
  mazeHeight = ntohl(amInitOk.init_ok.MazeHeight);

  // initializes every maze square
  for (int x = 0; x < mazeWidth; x++) {
    for (int y = 0; y < mazeHeight; y++) {
      maze[x][y].northSide = -1;
      maze[x][y].eastSide = -1;
      maze[x][y].southSide = -1;
      maze[x][y].westSide = -1;
    }
  }


  fprintf(stdout, "Successfully communicated with server.\n");
  free(amInit);

  /* Create logfile for processes */

  char *filename = DetermineLogfile(nAvatars, difficulty); //create appropriate filename

  logfile = fopen(filename, "w");

  if (logfile == NULL) {
    fprintf(stderr, "[%s] Error: Failed to generate logfile.\n", program);
    return (0);
   }

  // Get local time & print username, mazeport, and time to first line
  time_t dateTime;
  time(&dateTime);
  fprintf(logfile, "Username: %s MazePort: %d Timestamp: %s", getenv("USER"), ntohl(amInitOk.init_ok.MazePort), asctime(localtime(&dateTime)));

  close(sockfd);



  /* Create thread for display of window */

  AM_Message *frameMessage = calloc(1, sizeof(AM_Message));
  frameMessage->type = htonl(AM_INIT_OK);
  frameMessage->init_ok.MazeWidth = amInitOk.init_ok.MazeWidth;
  frameMessage->init_ok.MazeHeight = amInitOk.init_ok.MazeHeight;

  pthread_t frame;
  int frameFails;
  frameFails = pthread_create(&frame, NULL, OpenFrame, frameMessage);

  if (frameFails) {
    fprintf(stderr, "Failed to open graphics frame.\n");
    exit(0);
  }



  /* Start avatar threads */

  int allStarted = StartThreads(nAvatars, difficulty, amInitOk, server);
  if (!allStarted) {
    fprintf(stderr, "[%s] Error: Unable to start all processes.\n", program);
    return(0);
   }


  /* End startup if child processes successfully begun */
  while (!mazeSolved) {
    continue;
  }
  
  
  printf("Exiting from main.\n");
  fprintf(logfile, "Maze Solved! Timestamp: %s", asctime(localtime(&dateTime)));
  fclose(logfile);
  free(filename);
  return(0);
}





