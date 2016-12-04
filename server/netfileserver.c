#include <stdio.h>
#include <stdlib.h>
//#include <string.h>
#include <strings.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#include <sys/stat.h>

#include "libnetfiles.h"


/////////////////////////////////////////////////////////////
//
// Data type and configuration definitions
//
// ---------------------------------------------------------
//
// Max size of the file descriptor table.  

#define FD_TABLE_SIZE   5  


//
// Data structure of a file descriptor
//
typedef struct {
    int  fd;                      // File descriptor (must be negative)
    FILE_CONNECTION_MODE fcMode;  // File connection mode
    int fileOpenFlags;            // Open file flags
    char pathname[256];           // file path name
} NET_FD_TYPE;


/////////////////////////////////////////////////////////////
//
// Function declarations 
//
/////////////////////////////////////////////////////////////

static void sig_handler( const int signo );
static void SetupSignals();
void *WorkerThread( void *newSocket_FD );
int Do_netopen( NET_FD_TYPE *netFd );


//
// Utility functions to manage file descriptor table
//
void initFD();
int findFD( NET_FD_TYPE *netFd );
int createFD( NET_FD_TYPE *netFd );
//int deleteFD( NET_FD_TYPE *netFd );
int deleteFD( int fd );
void printFDtable();
int canOpen( NET_FD_TYPE *netFd );



/////////////////////////////////////////////////////////////
//
// Declare global variables
//
/////////////////////////////////////////////////////////////

static int  bTerminate = FALSE;
static pthread_t HB_thread_ID = 0;


//
// Here is my file descriptor table
//
NET_FD_TYPE FD_Table[ FD_TABLE_SIZE ];



/////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
    int sockfd = 0;
    int newsockfd = 0;

    struct sockaddr_in serv_addr, cli_addr;
    int clilen = sizeof(cli_addr);
    pthread_t    Worker_thread_ID = 0;


    SetupSignals();  // Set up signal handlers


    //
    // Initialize file descriptor table 
    //
    initFD();

    //
    // Create a new socket for my listener
    //
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr,"netfileserver: socket() failed, errno= %d\n", errno);
        exit(EXIT_FAILURE);
    }

    //
    // Initialize the server address structure to be
    // used for binding my socket to a port number.
    //
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT_NUMBER);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
    {
        fprintf(stderr,"netfileserver: bind() failed, errno= %d\n", errno);
        exit(EXIT_FAILURE);
    }


    //
    // Listen on the socket.  Allow a connection 
    // backlog of size "50".
    //
    if (listen(sockfd, 50) < 0)
    {
        fprintf(stderr,"netfileserver: listen() failed, errno= %d\n", errno);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    else
    {
        //printf("netfileserver: listener is listening on socket %d\n", sockfd);
    }


    //
    // Start the listener to listen for incoming requests from
    // client.  When a new request comes in, this listener will 
    // automatically "accept" the request and spawn a worker
    // thread to handle it.  Then, the listener will go back to
    // wait for the next incoming request.  This listening action
    // will continue until a SIGTERM is received.  When that 
    // happens, this listener will close the socket and exit the
    // main function.
    //

    while (bTerminate == FALSE)
    {
        //printf("netfileserver: listener is waiting to accept incoming request\n");
        if ((newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) < 0)
        {
            //
            // Socket accept function returns an error
            //
            if ( errno != EINTR )
            {
                fprintf(stderr,"netfileserver: accept() failed, errno= %d\n", errno);

            	if ( newsockfd != 0 ) close(newsockfd);
            	if ( sockfd != 0 ) close(sockfd);
                exit(EXIT_FAILURE);
            }
            else
            {
            	//printf("netfileserver: accept() received interrupt\n");

            	//if ( HB_thread_ID != 0 )
            	//{
            		//
            		// The previous heart beat thread is still running.
            		// It should have terminated before we get here.
            		// It must be hung.  Kill it now.
            		//
            		//WriteLogInt("listener(): Cancel heart beat thread ID",
            					//HB_thread_ID, LOG_INFO);
            		//rc = pthread_cancel( HB_thread_ID );
            		//WriteLogInt("listener(): pthread_cancel returns",rc,LOG_INFO);
            		//HB_thread_ID = 0;
            	//};

            	if ( newsockfd != 0 ) close(newsockfd);
            	if ( sockfd != 0 )    close(sockfd);
                bTerminate = TRUE;  // Signal listener to terminate
            };
        }
        else
        {
            //
            // Accepted an incoming request from my socket.  Need 
            // to spawn a worker thread to handle this request.
            //
            //printf("netfileserver: listener accepted a new request from socket\n");

            pthread_create(&Worker_thread_ID, NULL, &WorkerThread, &newsockfd );

            //printf("netfileserver: listener spawned a new worker thread with ID %d\n",Worker_thread_ID);
        };

        //
        // We will reset our heart beat timer every
        // time we receive an incoming request from the
        // socket.
        //
//        SetAlarm( HEART_BEAT_TIME );  // Reset the heart beat timer
    };


    if ( newsockfd != 0 ) close(newsockfd);
    if ( sockfd != 0 ) close(sockfd);

    printf("netfileserver: terminated\n");

}


/////////////////////////////////////////////////////////////


static void sig_handler( const int signo )
{
    switch ( signo )
    {
    case SIGINT:
    	printf("nerfileserver: sig_handler caught SIGINT\n");
    	bTerminate = TRUE;
    	break;

    case SIGTERM:
    	printf("nerfileserver: sig_handler caught SIGTERM\n");
    	bTerminate = TRUE;
    	break;

    case SIGALRM:
    	printf("nerfileserver: sig_handler caught SIGALRM");

    	//
    	// SIGALRM is my alarm timer
    	//
    	//SetAlarm(0);	// reset alarm

    	if ( getppid() == 1 )
    	{
    		//
    		// Only the parent process needs to do heart beat
    		//
    		if ( HB_thread_ID == 0 )
    		{
    			//
    			// We do not have a heart beat thread running
    			// at this time.  Start it up now.
    			//
    			printf("nerfileserver: sig_handler creating a new heart beat thread\n");
    			//rc = pthread_create(&HB_thread_ID, NULL, HeartBeat, (void *)NULL );
    			//WriteLogInt("sig_handler(): pthread_create returns",rc,LOG_INFO);
    			//WriteLogInt("sig_handler(): HB thread ID =",HB_thread_ID,LOG_INFO);

    			//bRestartListener = FALSE;
    			//SetAlarm( HEART_BEAT_TIME );  // Reset the heart beat timer
    		}
    		else
    		{
    			//
    			// The previous heart beat thread is still running.
    			// It should have terminated before we get the alarm
    			// signal again.  It must be hung.  Time to restart
    			// my daemon listener thread.
    			//
    			printf("nerfileserver: sig_handler cancelling heart beat thread\n");
    			//rc = pthread_cancel( HB_thread_ID );
    			//printf("sig_handler: pthread_cancel returns",rc);

    			//bRestartListener = TRUE;
    			//SetAlarm(2);  // Reset the heart beat timer
    		};
    	};

    	break;

    default:
    	//
    	// We received a signal that we don't know how to handle
    	//
    	printf("nerfileserver: sig_handler caught unhandled signal %d\n", signo);
    	break;

    }; // end of the switch statement

    //
    // We must set up the signal handler again after we
    // have received a signal.  Otherwise, this program will
    // crash if we receive the same signal the second time.
    //
    SetupSignals();
}

/////////////////////////////////////////////////////////////

static void SetupSignals()
{
    //
    // Set up to catch the SIGINT signal
    //
    if ( signal(SIGINT, sig_handler) == SIG_ERR )
    	printf("netfileserver: cannot set up SIGINT\n");

    //
    // Set up to catch the SIGTERM signal
    //
    if ( signal(SIGTERM, sig_handler) == SIG_ERR )
    	printf("netfileserver: cannot set up SIGTERM\n");


    //
    // Set up to catch the SIGALRM signal
    //
    if ( signal(SIGALRM, sig_handler) == SIG_ERR )
    	printf("netfileserver: cannot set up SIGALRM\n");

}

/////////////////////////////////////////////////////////////


void *WorkerThread( void *newSocket_FD )
{
    int rc = 0;
    int fd = -1;
    int *sockfd = newSocket_FD;
    NET_FD_TYPE  *newFd = NULL;

    char msg[MSG_SIZE] = "";
    char myThreadLabel[64] = "";
    NET_FUNCTION_TYPE netFunc = INVALID;

    sprintf(myThreadLabel, "netfileserver: workerThread %d,", pthread_self());


    //printf("%s PID= %d\n",myThreadLabel, (int)getpid());

    rc = pthread_detach( pthread_self() );
    //printf("%s pthread_detach returns %d\n",myThreadLabel, rc);

    rc = read(*sockfd, msg, MSG_SIZE -1);
    if ( rc < 0 ) {
        fprintf(stderr,"%s fails to read from socket\n", myThreadLabel);
        if ( *sockfd != 0 ) close(*sockfd);
	pthread_exit( NULL );
    }
    else 
    {
        //printf("%s received \"%s\"\n", myThreadLabel, msg);
    }


    //
    // Decode the incoming message.  Find out which
    // net function is requested.
    //
    sscanf(msg, "%u,", &netFunc);
 
    switch (netFunc)
    {
        case NET_SERVERINIT:
            //
            // Incoming message format is:
            //     1,0,0,0
            //
            //printf("%s received \"netserverinit\"\n", myThreadLabel);

            // Compose a response message.  The format is:
            //
            //    result,0,0,0
            //
            sprintf(msg, "%d,0,0,0", SUCCESS);

            //printf("%s responding with \"%s\"\n", myThreadLabel, msg);
            break;

        case NET_OPEN:
            //printf("%s received \"netopen\"\n", myThreadLabel);

            //
            // Incoming message format is:
            //     2,connectionMode,fileOpenFlags,pathname
            //
            newFd = malloc( sizeof(NET_FD_TYPE) );

            sscanf(msg, "%u,%d,%d,%s", &netFunc, (int *)&(newFd->fcMode), 
                      &(newFd->fileOpenFlags), newFd->pathname );


            //
            // The "Do_netopen" function returns a new file descriptor 
            // on success.  Otherwise, it will return a "-1".
            //
            rc = Do_netopen( newFd );
            //printf("%s Do_netopen returns fd %d\n", myThreadLabel, rc);


            //
            // Compose a response message.  The format is:
            //
            //    result,errno,h_errno,netFd
            //
            if ( rc == FAILURE  ) {
                sprintf(msg, "%d,%d,%d,%d", FAILURE, errno, h_errno, FAILURE);
            } 
            else {
                sprintf(msg, "%d,%d,%d,%d", SUCCESS, errno, h_errno, rc);
            }
            //printf("%s responding with \"%s\"\n", myThreadLabel, msg);

            free( newFd );
            break;

        case NET_READ:
            printf("%s received \"netread\"\n", myThreadLabel);

            //printf("%s responding with \"%s\"\n", myThreadLabel, msg);
            break;

        case NET_WRITE:
            printf("%s received \"netwrite\"\n", myThreadLabel);

            //printf("%s responding with \"%s\"\n", myThreadLabel, msg);
            break;

        case NET_CLOSE:
            //printf("%s received \"netclose\"\n", myThreadLabel);

            //
            // Incoming message format is:
            //     5,fd,0,0
            //
            sscanf(msg, "%u,%d", &netFunc, &fd); 


            //
            // On success, "deleteFD" returns the file descriptor 
            // that was closed.  Otherwise, it will return a "-1".
            //
            //printf("%s trying to delete fd %d\n", myThreadLabel, fd);
            rc = deleteFD( fd );
            //printf("%s deleteFD returns %d\n", myThreadLabel, rc);


            //
            // Compose a response message.  The format is:
            //
            //    result,errno,h_errno,netFd
            //
            if ( rc == FAILURE  ) {
                sprintf(msg, "%d,%d,%d,%d", FAILURE, errno, h_errno, FAILURE);
            } 
            else {
                sprintf(msg, "%d,%d,%d,%d", SUCCESS, errno, h_errno, rc);
            }
            //printf("%s responding with \"%s\"\n", myThreadLabel, msg);

            break;

        case INVALID:
        default:
            printf("%s received invalid net function\n", myThreadLabel);

            //printf("%s responding with \"%s\"\n", myThreadLabel, msg);
            break;
    }



    //
    // Send my server response back to the client
    //
    rc = write(*sockfd, msg, strlen(msg) );
    if ( rc < 0 ) {
        fprintf(stderr,"%s fails to write to socket\n", myThreadLabel);
    }
    
    if ( *sockfd != 0 ) close(*sockfd);
    pthread_exit( NULL );
}

/////////////////////////////////////////////////////////////

int Do_netopen( NET_FD_TYPE *newFd )
{
    int rc = -1;

    rc = open(newFd->pathname, newFd->fileOpenFlags);
    if ( rc < 0 ) {
        // File open failed
        //fprintf(stderr,"netopen: errno= %d \"%s\", h_errno= %d\n", errno, strerror(errno), h_errno);
        return FAILURE;
    }

    //
    // Successfully opened the given file.  Need to
    // assign a file descriptor to this file and
    // save it in my file descriptor table.
    //
    close( rc );
    //printf("netopen: opened and closed \"%s\"\n", newFd->pathname);

    //
    // At this point, we know the requested file exists and is 
    // accessible from this file server.  Now we need to 
    // implement the net file connection access policy.
    //
    if ( canOpen(newFd) == FALSE ) return FAILURE;


    //
    // Store this new file descriptor in fd table
    //
    rc = createFD( newFd );
    if ( rc == FAILURE ) {
        // No more empty slot in file descriptor table.
        errno = ENFILE;
        return FAILURE;
    } 

    return rc;  // This is the file descriptor   
}

/////////////////////////////////////////////////////////////


void initFD()
{
    int i = 0;

    for (i=0; i < FD_TABLE_SIZE; i++) {
        FD_Table[i].fd = 0;  // valid fd must be negative
        FD_Table[i].fcMode = INVALID_FILE_MODE;        
        FD_Table[i].fileOpenFlags = O_RDONLY;        
        FD_Table[i].pathname[0] = '\0';        
    }
}

/////////////////////////////////////////////////////////////
//
// findFD() function returns the index of the FD table that
// contains the NET_FD_TYPE entry that we are looking for.  
// The return value is **NOT** the actual file descriptor 
// value.  In order to get the actual file descriptor, you 
// must do this:
//
// FD_Table[rc].fd  where rc is the value returned by findFD()
//
/////////////////////////////////////////////////////////////

int findFD( NET_FD_TYPE *newFd )
{
    int i = 0;

    //
    // Find the given file descriptor in the file descriptor table
    //
    for (i=0; i < FD_TABLE_SIZE; i++) {
        if ((strcmp(FD_Table[i].pathname, newFd->pathname) == 0) &&
            (FD_Table[i].fcMode == newFd->fcMode) &&
            (FD_Table[i].fileOpenFlags == newFd->fileOpenFlags)) 
        {
            // Found the file descriptor specified
            //printFDtable();
            return i;
        }
    }

    // Cannot find the file descriptor specified
    //printFDtable();
    return FAILURE;
}


/////////////////////////////////////////////////////////////
//
// This function return a file descriptor.  A valid net file
// descriptor is derived from the index of the entry in the
// fd table.  Index 1 will have a net fd of (-10).  Index 2
// will have a net fd of (-20) and so on.  Any net fd that 
// is greater than or equal to "0" is invalid.
//
/////////////////////////////////////////////////////////////

int createFD( NET_FD_TYPE *newFd )
{
    int i = 0;
    int rc = -1;

    //
    // Try to find the file descriptor from fd table
    //
    rc = findFD( newFd );
    if ( rc >= 0 ) {
        // Found the file descriptor.  No need to re-create.
        //printFDtable();
        //printf("createFD: found fd %d, no need to create new fd\n", FD_Table[rc].fd);
        return FD_Table[rc].fd;
    }

    //
    // The given file descriptor is not in fd table.  Need to
    // find an available slot in the file descriptor table to
    // to save the information for this new file descriptor.
    //
    for (i=0; i < FD_TABLE_SIZE; i++) {
        if ( FD_Table[i].pathname[0] == '\0' ) {
            // Found an empty slot in my FD table
            FD_Table[i].fd = (-10 * (i+1));  // fd must be negative
            FD_Table[i].fcMode = newFd->fcMode;        
            FD_Table[i].fileOpenFlags = newFd->fileOpenFlags;        
            strcpy( FD_Table[i].pathname, newFd->pathname);
            //printFDtable();

            return FD_Table[i].fd;  // fd must be negative
         }
    }

    // File descriptor table is full
    //printFDtable();
    return FAILURE;
}


/////////////////////////////////////////////////////////////


int deleteFD( int fd )
{
    int i = 0;

    //
    // Try to find the file descriptor from fd table
    //
    for (i=0; i < FD_TABLE_SIZE; i++) {
        if ( FD_Table[i].fd == fd ) {
            // Found the fd that I want to delete
            FD_Table[i].fd = 0;  // fd must be negative
            FD_Table[i].fcMode = INVALID_FILE_MODE;        
            FD_Table[i].fileOpenFlags = O_RDONLY;        
            FD_Table[i].pathname[0] = '\0';        
            //printFDtable();

            return fd;
         }
    }
    //printFDtable();
    errno = EBADF;
    return FAILURE;
}

/////////////////////////////////////////////////////////////

void printFDtable()
{
    int i = 0;

    //
    // print the content of the fd table
    //
    for (i=0; i < FD_TABLE_SIZE; i++) {
        printf("FD_Table[%i]: fd= %d, fcMode= %d, fileOpenFlags= %d, pathname= %s\n",
                i, FD_Table[i].fd, FD_Table[i].fcMode, 
                FD_Table[i].fileOpenFlags, FD_Table[i].pathname);
    }
}

/////////////////////////////////////////////////////////////

int canOpen( NET_FD_TYPE *newFd )
{
    int i = 0;

    switch (newFd->fcMode) {
        case TRANSACTION_MODE:
            //
            // Check if this file can be opened in transaction mode.
            // This means no fd has been assigned to this file for
            // any reason in my file descriptor table.
            //
            // Find the given pathname in the file descriptor table
            //
            for (i=0; i < FD_TABLE_SIZE; i++) {
                if (strcmp(FD_Table[i].pathname, newFd->pathname) == 0) 
                {
                    // Found the pathname specified.  This file
                    // cannot be opened in the transaction mode.
                    return FALSE;
                }
            }
            // No fd has been assigned to this file.  It is okay
            // to open this file in tranaction mode.
            return TRUE;
            break;
 
        case EXCLUSIVE_MODE:
            //
            // Check if this file can be opened in exclusive mode.
            // This means no fd has been assigned to this file that
            // has write permission (i.e. O_WRONLY or O_RDWR).
            //
       
            for (i=0; i < FD_TABLE_SIZE; i++) {
                if (strcmp(FD_Table[i].pathname, newFd->pathname) == 0) 
                {
                    // Found the pathname specified.  Now check
                    // if this fd is allowed to do writing. 
                    //
                    if ((FD_Table[i].fileOpenFlags == O_WRONLY) ||
                        (FD_Table[i].fileOpenFlags == O_RDWR) ) 
                    {
                        return FALSE;
                    }
                }
            }
            // No fd has been assigned to this file that has 
            // permission to do file write.  Therefore, it is okay
            // to open this file in exclusive mode.
            return TRUE;
            break;
 
        case UNRESTRICTED_MODE:
            return TRUE;
            break;

        default:
            // invalid file connection mode
            errno = EINVAL;
            break;
    }

    return FALSE;
}

/////////////////////////////////////////////////////////////

