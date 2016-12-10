#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
// Data structure of a file descriptor
//
/////////////////////////////////////////////////////////////

typedef struct {
    int  fd;                      // File descriptor (must be negative)
    FILE_CONNECTION_MODE fcMode;  // File connection mode
    int fileOpenFlags;            // Open file flags
    char pathname[256];           // file path name
} NET_FD_TYPE;



typedef struct {
    int sockfd;  // file transfer socket
    int port;    // port number
    int inUse;   // TRUE= socket in use.  Otherwise, FALSE.
    NET_FD_TYPE netFd;
} FILE_TRANSFER_SOCKET_TYPE;


/////////////////////////////////////////////////////////////
//
// Function declarations
//
/////////////////////////////////////////////////////////////

void initialize();
static void sig_handler( const int signo );
static void SetupSignals();
void *ProcessNetCmd( void *newSocket_FD );
int Do_netopen( NET_FD_TYPE *netFd );
int Do_netwrite( int nBytes, pthread_t *pTids, int *portCount, char *portList );
int getSockfd( const int port );
void *netwriteListener( void *sockfd );
int savePartfile( int netfd, int seqNum, char *data, int nBytes);
int reconstruct( const int netfd, const int parts);


//
// Utility functions to manage file descriptor table
//
int findFD( NET_FD_TYPE *netFd );
int createFD( NET_FD_TYPE *netFd );
int deleteFD( int fd );
void printFDtable();
int canOpen( NET_FD_TYPE *netFd );
NET_FD_TYPE *LookupFDTable( const int netfd );



/////////////////////////////////////////////////////////////
//
// Declare global variables
//
/////////////////////////////////////////////////////////////

//static int  bTerminate = FALSE;
//static pthread_t HB_thread_ID = 0;
int  bTerminate = FALSE;
pthread_t HB_thread_ID = 0;


//
// Here is my net file descriptor table
//
NET_FD_TYPE FD_Table[ FD_TABLE_SIZE ];


//
// An array of sockets used for net file transfer
//
//FILE_TRANSFER_SOCKET_TYPE  xferSock[ MAX_FILE_TRANSFER_SOCKETS ];



/////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
    int sockfd  = 0;
    int sockOpt = 1;
    int newsockfd = 0;
    int rc = 0;

    struct sockaddr_in serv_addr, cli_addr;
    int clilen = sizeof(cli_addr);
    pthread_t    ProcessNetCmd_threadID = 0;


    SetupSignals();  // Set up signal handlers


    //
    // Initialize file descriptor table
    //
    initialize();

    //
    // Create a new socket for my listener
    //
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr,"netfileserver: socket() failed, errno= %d\n", errno);
        exit(EXIT_FAILURE);
    }

    rc = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &sockOpt, sizeof(sockOpt));
    if (rc != 0)
    {
        fprintf(stderr,"netfileserver: setsockopt() returned %d, errno= %d\n", rc, errno);
    };



    //
    // Initialize the server address structure to be
    // used for binding my socket to a port number.
    // This port number is hard-coded in the
    // "libnetfiles.h" header file called "PORT_NUMBER".
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
        if ((newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, (socklen_t *)&clilen)) < 0)
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

            pthread_create(&ProcessNetCmd_threadID, NULL, &ProcessNetCmd, &newsockfd );

            //printf("netfileserver: listener spawned a new worker thread with ID %d\n",ProcessNetCmd_threadID);
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

//    for (i=0; i < MAX_FILE_TRANSFER_SOCKETS; i++) {
//        close( xferSock[i].sockfd );
//
//        //printf("netfileserver: closed xferSock[%d].sockfd= %d, port= %d\n",
//        //        i, xferSock[i].sockfd, xferSock[i].port);
//    }

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


void *ProcessNetCmd( void *newSocket_FD )
{
    int rc = 0;
    int netfd = -1;
    int nBytes = -1;
    int *sockfd = newSocket_FD;
    NET_FD_TYPE  *newFd = NULL;
    int filePartsCount = 0;

    char msg[MSG_SIZE] = "";
    char myThreadLabel[64] = "";
    NET_FUNCTION_TYPE netFunc = INVALID;

    sprintf(myThreadLabel, "netfileserver: ProcessNetCmd %ld,", pthread_self());


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

            //printf("%s responded \"%s\"\n", myThreadLabel, msg);
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
            //printf("%s responded \"%s\"\n", myThreadLabel, msg);

            free( newFd );
            break;

        case NET_READ:
            //printf("%s received \"netread\"\n", myThreadLabel);

            //printf("%s responded \"%s\"\n", myThreadLabel, msg);
            break;

        case NET_WRITE:
            //printf("%s received \"netwrite\"\n", myThreadLabel);

            //
            // Incoming message format is:
            //     4,netfd,nBytes,0
            //
            sscanf(msg, "%u,%d,%d", &netFunc, &netfd, &nBytes);


            //
            // Check if this netfd is opened for O_RDONLY
            //
            int portCount = 0;
            char portList[128] = "";
            pthread_t pTids[MAX_FILE_TRANSFER_SOCKETS];

            NET_FD_TYPE *pFD = LookupFDTable(netfd);
            if ( pFD == NULL ) {
                // There is no such netfd in my file
                // descriptor table.
                errno = EBADF;
                rc = FAILURE;
            }

            if ((rc != FAILURE) && (pFD->fileOpenFlags == O_RDONLY)) {
                // This netfd is opened for read-only mode
                errno = EACCES;
                rc = FAILURE;
            }

            if (rc != FAILURE) {
                //
                // Call the "Do_netwrite" function.  This function will
                // spawn one or more netwriteListener thread(s) to
                // handle file write in multiple parts each with a
                // sequence number.  It will return the total number of
                // of file parts that will be created.  We need this
                // parts count to reconstruct the final data file.
                //
                filePartsCount = Do_netwrite(nBytes, pTids, &portCount, portList);
                //printf("%s Do_netwrite returns filePartsCount= %d\n", myThreadLabel, filePartsCount);


//int j;
//for (j=0; j<10; j++) {
//    printf("%s After Do_netwrite, pTids[%d]= %ld\n", myThreadLabel, j, pTids[j]);
//}

            }


            //
            // Compose a response configuration message.  This message
            // tells the client how to do the net file write in one or
            // more smaller parts.  The format is:
            //
            //    result,errno,h_errno,netFd,portCount,portList
            //
            if ( rc == FAILURE  ) {
                sprintf(msg, "%d,%d,%d,%d,%d,%s", FAILURE, errno, h_errno, netfd, portCount, portList);
            }
            else {
                sprintf(msg, "%d,%d,%d,%d,%d,%s", SUCCESS, errno, h_errno, netfd, portCount, portList);
            }
            printf("%s responded \"%s\"\n", myThreadLabel, msg);

            //
            // Send my configuration response back to the client
            //
            rc = write(*sockfd, msg, strlen(msg) );
            if ( rc < 0 ) {
                fprintf(stderr,"%s fails to write to socket\n", myThreadLabel);
            }




            // Wait for all spawned netwriteListener threads to finish
            int i;
            for (i=0; i < portCount; i++) {
                printf("%s waiting for netwriteListener thread %ld\n", myThreadLabel, pTids[i]);
                pthread_join(pTids[i], NULL);
                printf("%s netwriteListener thread %ld finished\n", myThreadLabel, pTids[i]);
            }


            // TODO: Reconstruct the written file from all the piece parts
            nBytes = reconstruct( netfd, filePartsCount); // Total bytes written
            printf("%s netwriteListener: reconstruct returns %d bytes\n", myThreadLabel, nBytes);
            rc = SUCCESS;


            //
            // Compose my final response message.  The format is:
            //
            //    result,errno,h_errno,nBytes
            //
            if ( rc == FAILURE  ) {
                sprintf(msg, "%d,%d,%d,%d", FAILURE, errno, h_errno, FAILURE);
            }
            else {
                sprintf(msg, "%d,%d,%d,%d", SUCCESS, errno, h_errno, nBytes);
            }
            printf("%s responded \"%s\"\n", myThreadLabel, msg);
            break;

        case NET_CLOSE:
            //printf("%s received \"netclose\"\n", myThreadLabel);

            //
            // Incoming message format is:
            //     5,netfd,0,0
            //
            sscanf(msg, "%u,%d", &netFunc, &netfd);


            //
            // On success, "deleteFD" returns the file descriptor
            // that was closed.  Otherwise, it will return a "-1".
            //
            //printf("%s trying to delete netfd %d\n", myThreadLabel, netfd);
            rc = deleteFD( netfd );
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
            //printf("%s responded \"%s\"\n", myThreadLabel, msg);

            break;

        case INVALID:
        default:
            printf("%s received invalid net function\n", myThreadLabel);

            //printf("%s responded \"%s\"\n", myThreadLabel, msg);
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

    //
    // Verify the specified file exists and accessible
    //
    rc = open(newFd->pathname, newFd->fileOpenFlags);
    if ( rc < 0 ) {
        // File open failed
        //fprintf(stderr,"netopen: errno= %d \"%s\", h_errno= %d\n", errno, strerror(errno), h_errno);

        if ((errno == ENOENT) && (newFd->fileOpenFlags != O_RDONLY)) {
            // This file does not exist but I am trying to write to
            // it to create.  I can ignore this open error.
            errno = 0;
        }
        else {
            // This file cannot be opened.  It may be a
            // directory or no file access permission.
            return FAILURE;
        }
    }

    //
    // Successfully opened the given file.
    //
    close( rc );
    //printf("netopen: opened and closed \"%s\"\n", newFd->pathname);


    //
    // Now we need to check the net file connection access policy.
    //
    if ( canOpen(newFd) == FALSE ) {
        //printf("canOpen() returns FALSE\n");
        //printFDtable();

        // Not allowed by access policy
        errno = EACCES;
        return FAILURE;
    };


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

// Need to write "nBytes" to net server.  This function
// must determine how many ports to open.  Each port will
// listen for and accept a smaller part of the input file.
//
// This function returns the total number of parts that
// will be received from the client.  This information is
// needed to reconstruct the final file from all the smaller
// parts coming in from the client.  If the return number is
// 0, that means no need to reconstruct the file.  A "-1" is
// returned in case of error.  Otherwise, the total number
// of parts count is returned.
//
int Do_netwrite( int nBytes, pthread_t *pTids, int *portCount, char *portList )
{
    portList[0] = '\0';

    //
    // Step 1: Check to see if creating an empty file
    //
    if ( nBytes <= 0 ) {
        // Writing an empty file
        printf("netfileserver: Do_netwrite received nBytes= %d, will create empty file.\n", nBytes);

/*** TODO: create an empty file.  ***/

        return 0;  // No parts count to return
    }


    //
    // Step 2: Calculate the number of ports need to write
    //         "nBytes" of data to this file.  Each port
    //         will handle one part of the data file.  So,
    //         portCount also represents the total number
    //         of file parts count.
    //
    int portWanted = (nBytes / (int)DATA_CHUNK_SIZE);
    if ( (nBytes % DATA_CHUNK_SIZE) != 0 ) portWanted++;

    if (portWanted <= 0) portWanted = 1;
    if (portWanted > MAX_FILE_TRANSFER_SOCKETS) portWanted = MAX_FILE_TRANSFER_SOCKETS;

    //printf("netfileserver: Do_netwrite said %d ports are needed to transfer %d bytes\n",
    //               portWanted, nBytes);

    //
    // Step 3: Try to open the number of ports as
    //         calculated from above.  We may get
    //         less than the number of ports wanted
    //         because some ports may be in use.
    //
    int sockfd = -1;
    int port = PORT_NUMBER + 1;
    int j = 0;
    int i = 1;
    for (i=1; i <= MAX_FILE_TRANSFER_SOCKETS; i++) {
        //
        // Look for an open port
        //
        sockfd = getSockfd(port);
	if (sockfd != FAILURE) {
            //
            // Opened this port for listening
            //
            *portCount = (*portCount) +1;
            char sTemp[16] = "";
            sprintf(sTemp, "%d,", port);
            strcat(portList, sTemp);

            //
            // Step 4: Spawn a new netwriteListener thread to
            //         listen for data coming in from a port
            //
            pthread_create(&pTids[j], NULL, &netwriteListener, &sockfd );
            printf("netfileserver: Do_netwrite spawned thread %ld, sockfd= %d\n",pTids[j],sockfd);
            j++;

sleep(1);

        }

        port++;

        // Break the for loop if I got all the ports I wanted
        if ( *portCount >= portWanted) break;
    }

    //printf("netfileserver: Do_netwrite portWanted= %d, portCount= %d, portList= %s\n",
    //               portWanted, *portCount, portList);

    if ( *portCount <= 0 ) {
        // All ports are in use.  Cannot do net write now.
        printf("netfileserver: no port is available.  Cannot do netwrite now.\n");
        errno = ETIMEDOUT;
        return FAILURE;
    }

    return *portCount;
}



/////////////////////////////////////////////////////////////


void initialize()
{
    int i = 0;

    //
    // Initialize net file descriptor table
    //
    for (i=0; i < FD_TABLE_SIZE; i++) {
        FD_Table[i].fd = 0;  // valid fd must be negative
        FD_Table[i].fcMode = INVALID_FILE_MODE;
        FD_Table[i].fileOpenFlags = O_RDONLY;
        FD_Table[i].pathname[0] = '\0';
    }
}

/////////////////////////////////////////////////////////////


int getSockfd( const int port )
{
    int sockfd = -1;
    int sockOpt = 1;
    int rc = 0;
    struct sockaddr_in serv_addr;


    //
    // Create a new socket for my listener
    //
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr,"netfileserver: socket() failed, errno= %d\n", errno);
        return FAILURE;
    }

    rc = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &sockOpt, sizeof(sockOpt));
    if (rc != 0)
    {
        fprintf(stderr,"netfileserver: setsockopt() returned %d, errno= %d\n", rc, errno);
    };


    //
    // Initialize the server address structure to be
    // used for binding my socket to a port number.
    // This port number is hard-coded in the
    // "libnetfiles.h" header file called "PORT_NUMBER".
    //
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
        // This port may have already opened by another thread
        if ( errno == EADDRINUSE ) {
            fprintf(stderr,"netfileserver: port %d already in use, errno= %d\n",
                      port, errno);
        }
        return FAILURE;
    }


    //
    // Listen on the socket.  Allow a connection
    // backlog of size "50".
    //
    if (listen(sockfd, 50) < 0)
    {
        fprintf(stderr,"netfileserver: listen() failed, errno= %d\n", errno);
        close(sockfd);
        return FAILURE;
    }
    else
    {
        //printf("netfileserver: sockfd %d is listening\n", sockfd);
    }

    printf("netfileserver: socket %d is listening and binded to port %d\n", sockfd, port);
    return sockfd;
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
// This function returns a NET_FD_TYPE data structure found
// in the file descriptor table by the given "netfd".
//
/////////////////////////////////////////////////////////////

NET_FD_TYPE *LookupFDTable( const int netfd )
{
    int i = 0;

    //
    // Find the given file descriptor in the file descriptor table
    //
    for (i=0; i < FD_TABLE_SIZE; i++) {
        if (FD_Table[i].fd == netfd) {
            // Found the file descriptor specified
            //printFDtable();

            return &FD_Table[i];
        }
    }

    // Cannot find the file descriptor specified
    //printFDtable();
    return NULL;
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

    //
    // Try to find this fd from my current table.  If it is
    // already in the table, then this file can be opened
    // as specified.
    //
    i = findFD(newFd);
    if ( i >= 0 ) {
        // Found the file descriptor.
        //printFDtable();
        //printf("canOpen: found fd %d\n", FD_Table[i].fd);
        return TRUE;
    }

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
            // has exclusive write permission (i.e. O_WRONLY or O_RDWR)
            // or use in a transaction mode access.
            //

            for (i=0; i < FD_TABLE_SIZE; i++) {
                if (strcmp(FD_Table[i].pathname, newFd->pathname) == 0)
                {
                    // Found the pathname specified.  Now check
                    // if this fd is allowed to do writing.
                    //
                    if (FD_Table[i].fcMode == TRANSACTION_MODE) {
                        //
                        // This file is already opened by one of
                        // the fd's in transaction mode.
                        //
                        return FALSE;
                    };

                    if ((newFd->fileOpenFlags != O_RDONLY) &&
                        (FD_Table[i].fcMode == EXCLUSIVE_MODE)) {

                        if ((FD_Table[i].fileOpenFlags == O_RDWR) ||
                            (FD_Table[i].fileOpenFlags == O_WRONLY))
                        {
                            //
                            // I want to open this file exclusively
                            // with write permission.  However, this
                            // file is already opened in exclusive mode
                            // with write permission by one of the fd's
                            // in the file descriptor table.
                            //
                            return FALSE;
                        }
                    };
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


void *netwriteListener( void *sfd )
{
    //int *pfd = sfd;
    //int sockfd = *pfd;
    const int sockfd = *((int *)sfd);
    
    int newsockfd = 0;
    struct sockaddr_in  cli_addr;
    int clilen = sizeof(cli_addr);
    int rc = 0;

    char msg[MSG_SIZE] = "";
    char myThreadLabel[64] = "";
    sprintf(myThreadLabel, "netfileserver: netwriteListener %ld,", pthread_self());

    //printf("%s sfd= %ld, *sfd= %d, sockfd= %d\n", myThreadLabel, (long)sfd, *pfd, sockfd);

    printf("%s waiting to accept from sockfd %d\n", myThreadLabel, sockfd);
    if ((newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, (socklen_t *)&clilen)) < 0)
    {
        //
        // Socket accept function returns an error
        //
        fprintf(stderr,"%s accept() failed, sockfd= %d, errno= %d (%s), h_errno= %d\n",
                 myThreadLabel, sockfd, errno, strerror(errno), h_errno);

        if ( newsockfd != 0 ) close(newsockfd);
        if ( sockfd != 0 ) close(sockfd);
        rc = FAILURE;
        pthread_exit( &rc );
    }
    if ( sockfd != 0 ) close(sockfd);


    //
    // First incoming message format is:
    //     netwrite, netfd, seqNum, nBytes
    //
    bzero(msg, MSG_SIZE);
    rc = read(newsockfd, msg, MSG_SIZE -1);
    if ( rc < 0 ) {
        fprintf(stderr,"%s fails to read from socket, errno= %d, h_errno= %d\n",
                 myThreadLabel, errno, h_errno);
        if ( sockfd != 0 ) close(sockfd);
        rc = FAILURE;
        pthread_exit( &rc );
    }
    printf("%s received \"%s\"\n", myThreadLabel, msg);

    int netFunc = -1;
    int netfd = -1;
    int seqNum = -1;
    int nBytes = -1;
    sscanf(msg, "%d,%d,%d,%d", &netFunc, &netfd, &seqNum, &nBytes);
    //printf("%s netFunc= %d, netfd= %d, seqNum= %d, nBytes= %d\n",
    //         myThreadLabel, netFunc, netfd, seqNum, nBytes);


    //
    // Send my response back to the client
    //     resultCode, errno, h_errno, seqNum, nBytes
    //
    bzero(msg, MSG_SIZE);
    sprintf(msg, "%d,%d,%d,%d,%d", SUCCESS, errno, h_errno, seqNum, nBytes);
    rc = write(newsockfd, msg, strlen(msg) );
    if ( rc < 0 ) {
        fprintf(stderr,"%s fails to write \"%s\"\n", myThreadLabel, msg);
        if ( sockfd != 0 ) close(sockfd);
        rc = FAILURE;
        pthread_exit( &rc );
    }
    printf("%s responded \"%s\"\n", myThreadLabel, msg);


    //
    // Read nBytes of data
    //
    char *data;
    data = malloc(nBytes * sizeof(char));
    bzero(data, nBytes);

    rc = read(newsockfd, data, nBytes);
    if ( rc < 0 ) {
        fprintf(stderr,"%s fails to read from socket, errno= %d, h_errno= %d\n",
                 myThreadLabel, errno, h_errno);
        if ( sockfd != 0 ) close(sockfd);
        rc = FAILURE;
        pthread_exit( &rc );
    }

    nBytes = rc;  // This is the number of bytes received
    printf("%s received %d bytes of data\n", myThreadLabel, nBytes);

//char sTemp[4096] = "";
//bzero(sTemp, 4096);
//strncpy(sTemp, data, nBytes);
//printf("%s data= \"%s\"\n", myThreadLabel, sTemp);

    //
    // TODO : Save the received data into a temporary file
    //        using the sequence number as part of the file name
    //


    rc = savePartfile( netfd, seqNum, data, nBytes);
    free(data);


    //
    // Send my response back to the client
    //     resultCode, errno, h_errno, nBytes
    //
    bzero(msg, MSG_SIZE);
    sprintf(msg, "%d,%d,%d,%d", SUCCESS, errno, h_errno, nBytes);
    rc = write(newsockfd, msg, strlen(msg) );
    if ( rc < 0 ) {
        fprintf(stderr,"%s fails to write \"%s\"\n", myThreadLabel, msg);
        if ( sockfd != 0 ) close(sockfd);
        rc = FAILURE;
        pthread_exit( &rc );
    }
    printf("%s responded \"%s\"\n", myThreadLabel, msg);

    if ( newsockfd != 0 ) close(newsockfd);
    rc = SUCCESS;
    pthread_exit( &nBytes );

}


/////////////////////////////////////////////////////////////


int savePartfile( int netfd, int seqNum, char *data, int nBytes)
{
    NET_FD_TYPE  *fileInfo;
    char tempfile[256] = "";
    char fileExt[16] = "";
    FILE *fp;
    int rc = FAILURE;


//char sTemp[4096] = "";
//bzero(sTemp, 4096);
//strncpy(sTemp, data, nBytes);
//printf("netfileserver: thread %ld: savePartfile: data= \"%s\"\n", pthread_self(), sTemp);


    // Lookup file information from the given netfd
    //printf("netfileserver: savePartfile: netfd= \"%d\"\n", netfd);
    //printFDtable();

    fileInfo = LookupFDTable( netfd );
    strcpy( tempfile, fileInfo->pathname );
    sprintf(fileExt, ".%d", seqNum );
    strcat( tempfile, fileExt);
    //printf("netfileserver: savePartfile: temp tempfile= \"%s\"\n", tempfile);

    // Open the temp file for writing
    fp = fopen(tempfile,"w");
    if ( fp == NULL ) {
        // Fail to open the temp file
        fprintf(stderr,"netfileserver: savePartfile: fails to open \"%s\", errno= %d\n",tempfile,errno);
        return FAILURE;
    }

    rc = fwrite(data, sizeof(char), nBytes, fp);
    if ( fflush(fp) != 0 ) {
        fprintf(stderr,"netfileserver: savePartfile: fails to fflush \"%s\", errno= %d\n",tempfile,errno);
    }

    if ( fp != NULL ) fclose(fp);

    return rc;
}


/////////////////////////////////////////////////////////////


int reconstruct( const int netfd, const int parts)
{
    NET_FD_TYPE  *fileInfo;
    char tempfile[256] = "";
    char fileExt[16] = "";
    FILE *fpWrite, *fpRead;
    long iTotalFileSize = 0;


    if ( parts <= 0 ) return SUCCESS;

    // Find the file to write to
    fileInfo = LookupFDTable( netfd );
    //printf("netfileserver: reconstruct: pathname= \"%s\"\n", fileInfo->pathname);

    // Open the final data file for writing
    fpWrite = fopen(fileInfo->pathname,"w");
    if ( fpWrite == NULL ) {
        // Fail to open the data file for writing
        fprintf(stderr,"netfileserver: reconstruct: fails to open \"%s\" for write, errno= %d\n",
                   fileInfo->pathname, errno);
        return FAILURE;
    }

    long bufsize = 0;
    char *data = NULL;
    int seqNum = 1;

    for (seqNum=1; seqNum<= parts; seqNum++) {
        strcpy( tempfile, fileInfo->pathname );
        sprintf(fileExt, ".%d", seqNum );
        strcat( tempfile, fileExt);
        //printf("netfileserver: reconstruct: tempfile= \"%s\"\n", tempfile);

        // Open the temp file for reading
        fpRead = fopen(tempfile,"r");
        if ( fpRead == NULL ) {
            // Fail to open the temp file
            fprintf(stderr,"netfileserver: reconstruct: fails to open \"%s\" for read, errno= %d\n",
                      tempfile,errno);
            if (fpWrite != NULL) fclose(fpWrite);
            return FAILURE;
        }


        // Read the entire part file into memory
        bufsize = 0;
        data = NULL;

        // Go to the end of the temp file.
        if (fseek(fpRead, 0L, SEEK_END) == 0) {
            // Get the size of the file.
            bufsize = ftell(fpRead);
            if (bufsize != -1) {
                // Allocate our buffer to that size.
                data = malloc(sizeof(char) * (bufsize + 1));
                bzero(data,bufsize+1);

                // Go back to the start of the file.
                if (fseek(fpRead, 0L, SEEK_SET) != 0) {
                    // Error
                    if (data != NULL) free(data);
                    data = NULL;
                }
            }

            if ( data != NULL ) {
                // Read the entire file into memory.
                size_t dataSize = fread(data, sizeof(char), bufsize, fpRead);
                if (dataSize == 0) {
                    fprintf(stderr,"netfileserver: reconstruct: fails to read \"%s\", errno= %d\n",
                       tempfile, errno);
                }
            }
        }
        if (fpRead != NULL) fclose(fpRead);

        if ( data != NULL ) {
            // Write the part data into the output file
            int nBytes = -1;
            nBytes = fwrite(data, sizeof(char), bufsize, fpWrite);
            iTotalFileSize = iTotalFileSize + nBytes;
            if (data != NULL)   free(data);

            if ( seqNum == 1 ) {
                //
                // After the first part in the file sequence,
                // I must open the data file for append mode.
                //
                fclose(fpWrite);
                fpWrite = fopen(fileInfo->pathname,"a");
            }
        }
        else {
            // Cannot retrieve the piece part data
            return FAILURE;
        }


        // Remove the part file
        remove(tempfile);

    }  //Append the next piece part

    // Finish reconstructing the data file from piece parts
    if (fpWrite != NULL) fclose(fpWrite);


    printf("netfileserver: reconstruct: created \"%s\", filesize= %ld\n",
             fileInfo->pathname, iTotalFileSize);

    return iTotalFileSize;
}
