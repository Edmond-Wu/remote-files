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

typedef struct QNode {
	int file_descriptor;
	struct QNode *next;
} QNode;

/////////////////////////////////////////////////////////////
//
// Function declarations
//
/////////////////////////////////////////////////////////////

void initialize();
static void sig_handler( const int signo );
static void SetupSignals();
int getSockfd( const int port ); // create a socket binded to a port
int findOpenPorts();

//
//Queue stuff
//
QNode* createQNode(int fd);
void enqueue(int fd);
int dequeue();


//
// Functions for processing commands sent by client
//
void *ProcessNetCmd( void *newSocket_FD );


//
// Functions for processing "netopen"
//
int Do_netopen( NET_FD_TYPE *netFd );


//
// Functions for processing "netwrite"
//
int Do_netwrite( const int nBytes, pthread_t *pTids, int *portCount, char *portList );
void *netwriteListener( void *sockfd );
int savePartfile( int netfd, int seqNum, char *data, int nBytes);
int reconstruct( const int netfd, const int parts);


//
// Functions for processing "netread"
//
int Do_netread( const int nBytesWant, const int fileSize, pthread_t *pTids, int *portCount, char *portList );
void *netreadListener( void *sockfd );
char *readFile( const int netfd, const int iStartPos, const int iBytesWanted);


//
// Utility functions to manage file descriptor table
//
int matchFD( NET_FD_TYPE *netFd );
int createFD( NET_FD_TYPE *netFd );
int deleteFD( int fd );
int tableFull();
NET_FD_TYPE *LookupFDtable( const int netfd );
void printFDtable();


//
// Utility functions for "netfd" permission checks
//
int canOpen( NET_FD_TYPE *netFd );
int canWrite( const int netfd, const int nBytes);
int canRead( const int netfd, const int nBytes, int *fileSize);



/////////////////////////////////////////////////////////////
//
// Declare global variables
//
/////////////////////////////////////////////////////////////

int  bTerminate = FALSE;
pthread_t HB_thread_ID = 0;
int queue_size = 0;
QNode *queue = NULL;

//
// Here is my net file descriptor table
//
NET_FD_TYPE   FD_Table[ FD_TABLE_SIZE ];




/////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
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
    if (rc != 0) {
        fprintf(stderr,"netfileserver: setsockopt() returned %d, errno= %d\n", rc, errno);
    }

    //
    // Initialize the server address structure to be
    // used for binding my socket to a port number.
    // This port number is hard-coded in the
    // "libnetfiles.h" header file called "NET_SERVER_PORT_NUM".
    //
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(NET_SERVER_PORT_NUM);
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
	    pthread_create(&ProcessNetCmd_threadID, NULL, &ProcessNetCmd, &newsockfd);
	    

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


    printf("netfileserver: terminated\n");
}


/////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////
//Queue functons
QNode* createQNode(int fd) {
	QNode *node = malloc(sizeof(QNode));
	node->file_descriptor = fd;
	node->next = NULL;
	return node;
}

void enqueue(int fd) {
	QNode *added = createQNode(fd);
	if (queue == NULL) {
		queue = added;
		queue_size = 1;
		return;
	}
	QNode *ptr = queue;
	while (ptr->next != NULL)
		ptr = ptr->next;
	ptr->next = added;
	queue_size++;
}

int dequeue() {
	if (queue == NULL)
		return FAILURE;
	int fd = queue->file_descriptor;
	if (queue->next == NULL) {
		free(queue);
		queue = NULL;
		return fd;
	}
	QNode* dequeued = queue;
	queue = queue->next;
	queue_size--;
	free(dequeued);
	return fd;
}

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
    int nBytesWant = -1;
    int *sockfd = newSocket_FD;
    NET_FD_TYPE  *newFd = NULL;
    int filePartsCount = 0;

    char msg[MSG_SIZE] = "";
    char myThreadLabel[64] = "";
    NET_FUNCTION_TYPE netFunc = INVALID;

    int portCount = 0;
    char portList[128] = "";

    // An array of spawned thread ID's
    pthread_t   pTids[MAX_FILE_TRANSFER_SOCKETS];



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

            //
            // Incoming message format is:
            //    3,netFd,nBytesWant,0
            //

            sscanf(msg, "%u,%d,%d", &netFunc, &netfd, &nBytesWant);

            // Set up initial conditions
            portCount = 0;
            strcpy(portList,"0");
            filePartsCount = 0;

            //
            // Check if reading is allowed for this "netfd"
            //
            int fileSize = 0;
            rc = canRead(netfd, nBytesWant, &fileSize);

            if (rc == SUCCESS) {
                 //
                 // Call the "Do_netread" function.  This function will
                 // spawn one or more netreadListener thread(s) to
                 // handle file read in multiple parts each with a
                 // sequence number.  It will return the total number of
                 // of file parts that will be created.  We need this
                 // parts count to reconstruct the final data read.
                 //
                 filePartsCount  = Do_netread(nBytesWant, fileSize, pTids, &portCount, portList);

                 rc = SUCCESS;
                 if ( filePartsCount == FAILURE )  rc = FAILURE;

	         //printf("%s Do_netread returns filePartsCount= %d\n", myThreadLabel, filePartsCount);


	         //int j;
	         //for (j=0; j<10; j++) {
	         //    printf("%s After Do_netread, pTids[%d]= %ld\n", myThreadLabel, j, pTids[j]);
	         //}
	    }


	    //
	    // Compose a response configuration message.  This message
	    // tells the client how to do the net file read in one or
	    // more smaller parts.  The format is:
	    //
	    //    result,errno,h_errno,netFd,fileSize,portCount,portList
	    //
	    bzero(msg, MSG_SIZE);
	    if ( rc == FAILURE  ) {
		sprintf(msg, "%d,%d,%d,%d,%d,0,0", FAILURE, errno, h_errno, netfd, fileSize);
	    }
	    else {
		if ( filePartsCount == 0 ) {
		    sprintf(msg, "%d,%d,%d,%d,%d,0,0", SUCCESS, errno, h_errno, netfd, fileSize);
		}
		else {
		    sprintf(msg, "%d,%d,%d,%d,%d,%d,%s",SUCCESS,errno,h_errno,netfd,fileSize,portCount,portList);
		}
	    }
	    //printf("%s responded \"%s\", length= %d\n", myThreadLabel, msg, (int)strlen(msg));

	    //
	    // Send my configuration response back to the client
	    //
	    rc = write(*sockfd, msg, strlen(msg) );
	    if ( rc < 0 ) {
		fprintf(stderr,"%s fails to write config msg to socket\n", myThreadLabel);
		if ( *sockfd != 0 ) close(*sockfd);
		pthread_exit( NULL );
	    }


	    //
	    // At this point, I have all my netreadListener threads spawned
	    // and ready to receive communications from the client.
	    //
	    nBytes = 0;
            int *nBytesRecv = NULL;

	    if ( filePartsCount > 0 ) {
		// Wait for all spawned netreadListener threads to finish
		int i;
		for (i=0; i < filePartsCount; i++) {
		    pthread_join(pTids[i], (void **)&nBytesRecv);
		    //printf("%s netreadListener thread %ld finished, nBytesRecv= %d\n",
		    //            myThreadLabel, pTids[i], (int)(*nBytesRecv));

                    //
                    // Calculate the total bytes send to the client for the netread cmd
                    //
                    nBytes = nBytes + (int)(*nBytesRecv);
                    free(nBytesRecv);
		}
	    }

	    rc = SUCCESS;
	    //printf("%s netreadListener: total of %d bytes sent to client\n", myThreadLabel, nBytes);


	    //
	    // Compose my final response message.  The format is:
	    //
	    //    result,errno,h_errno,nBytes
	    //
	    if ( nBytes == FAILURE  ) {
		sprintf(msg, "%d,%d,%d,%d", FAILURE, errno, h_errno, FAILURE);
	    }
	    else {
		sprintf(msg, "%d,%d,%d,%d", SUCCESS, errno, h_errno, nBytes);
	    }
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
	    // Check if writing is allowed for this "netfd"
	    //
	    rc = canWrite(netfd, nBytes);

	    if (rc == SUCCESS) {
		if (nBytes > 0) {
		    //
		    // Call the "Do_netwrite" function.  This function will
		    // spawn one or more netwriteListener thread(s) to
		    // handle file write in multiple parts each with a
		    // sequence number.  It will return the total number of
		    // of file parts that will be created.  We need this
		    // parts count to reconstruct the final data file.
		    //
		    filePartsCount  = Do_netwrite(nBytes, pTids, &portCount, portList);

		    rc = SUCCESS;
		    if ( filePartsCount == FAILURE )  rc = FAILURE;

		    //printf("%s Do_netwrite returns filePartsCount= %d\n", myThreadLabel, filePartsCount);

	            //int j;
	            //for (j=0; j<10; j++) {
	            //    printf("%s After Do_netwrite, pTids[%d]= %ld\n", myThreadLabel, j, pTids[j]);
	            //}

		}
		else {
		    //
		    // create an empty file.
		    //
		    NET_FD_TYPE *pFD = LookupFDtable(netfd);
		    FILE *fp = fopen(pFD->pathname,"w");
		    if ( fp == NULL ) {
			// Fail to open the temp file
			fprintf(stderr,"%s fails to create \"%s\", errno= %d\n",myThreadLabel,pFD->pathname,errno);
			rc = FAILURE;
		    }

		    if ( fp != NULL ) fclose(fp);
		    filePartsCount = 0;
		    portCount = 0;
		    sprintf(portList, "%d", portCount);
		    rc = SUCCESS;
		}
	    }  // Execute the netwrite function

	    //
	    // Compose a response configuration message.  This message
	    // tells the client how to do the net file write in one or
	    // more smaller parts.  The format is:
	    //
	    //    result,errno,h_errno,netFd,portCount,portList
	    //
	    bzero(msg, MSG_SIZE);
	    if ( rc == FAILURE  ) {
		sprintf(msg, "%d,%d,%d,%d,0,0", FAILURE, errno, h_errno, netfd);
	    }
	    else {
		if ( filePartsCount == 0 ) {
		    sprintf(msg, "%d,%d,%d,%d,0,0", SUCCESS, errno, h_errno, netfd);
		}
		else {
		    sprintf(msg, "%d,%d,%d,%d,%d,%s", SUCCESS, errno, h_errno, netfd, portCount, portList);
		}
	    }
	    //printf("%s responded \"%s\", length= %d\n", myThreadLabel, msg, (int)strlen(msg));

	    //
	    // Send my configuration response back to the client
	    //
	    rc = write(*sockfd, msg, strlen(msg) );
	    if ( rc < 0 ) {
		fprintf(stderr,"%s fails to write config msg to socket\n", myThreadLabel);
		if ( *sockfd != 0 ) close(*sockfd);
		pthread_exit( NULL );
	    }


	    if ( filePartsCount > 0 ) {
		// Wait for all spawned netwriteListener threads to finish
		int i;
		for (i=0; i < filePartsCount; i++) {
		    //printf("%s waiting for netwriteListener thread %ld\n", myThreadLabel, pTids[i]);
		    pthread_join(pTids[i], NULL);
		    //printf("%s netwriteListener thread %ld finished\n", myThreadLabel, pTids[i]);
		}


		//
		// Reconstruct the written file from all the piece parts
		//
		nBytes = reconstruct( netfd, filePartsCount); // Total bytes written
		//printf("%s netwriteListener: reconstruct returns %d bytes\n", myThreadLabel, nBytes);
		rc = SUCCESS;
	    }


	    //
	    // Compose my final response message.  The format is:
	    //
	    //    result,errno,h_errno,nBytes
	    //
	    if ( nBytes == FAILURE  ) {
		sprintf(msg, "%d,%d,%d,%d", FAILURE, errno, h_errno, FAILURE);
	    }
	    else {
		sprintf(msg, "%d,%d,%d,%d", SUCCESS, errno, h_errno, nBytes);
	    }
	    //printf("%s responded \"%s\"\n", myThreadLabel, msg);
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
		sprintf(msg, "%d,%d,%d,0", FAILURE, errno, h_errno);
	    }
	    else {
		sprintf(msg, "%d,%d,%d,%d", SUCCESS, errno, h_errno, rc);
	    }
	    //printf("%s responded \"%s\"\n", myThreadLabel, msg);

	    break;

	case INVALID:
	default:
	    //printf("%s received invalid net function\n", myThreadLabel);
	    errno = EINVAL;
	    rc = FAILURE;
	    sprintf(msg, "%d,%d,%d,0", FAILURE, errno, h_errno);
	    break;
    }



    //
    // Send my final server response back to the client
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
	enqueue(newFd->fd);
	return FAILURE;
    };


    //
    // Store this new file descriptor in fd table
    //
    int old_rc = rc;
    rc = createFD( newFd );
    if ( rc == FAILURE ) {
	// No more empty slot in file descriptor table.
	errno = ENFILE;
	enqueue(old_rc);
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
int Do_netwrite( const int nBytes, pthread_t *pTids, int *portCount, char *portList )
{
    *portCount = 0;
    portList[0] = '\0';

    //
    // Step 1: Check to see if creating an empty file
    //
    if ( nBytes <= 0 ) {
	// Nothing to write
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
    int port = NET_SERVER_PORT_NUM + 1;
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
	    int *pSockfd = malloc(sizeof(int));
	    *pSockfd= sockfd;
	    pthread_create(&pTids[j], NULL, &netwriteListener, pSockfd );
	    //printf("netfileserver: Do_netwrite spawned thread %ld, sockfd= %d\n",pTids[j],*pSockfd);
	    j++;
	}

	port++;

	// Break the for loop if I got all the ports I wanted
	if ( *portCount >= portWanted) break;
    }

    //printf("netfileserver: Do_netwrite portWanted= %d, portCount= %d, portList= %s\n",
    //               portWanted, *portCount, portList);

    if ( *portCount <= 0 ) {
	// All ports are in use.  Cannot do net write now.
	//printf("netfileserver: no port is available.  Cannot do netwrite now.\n");
	errno = ETIMEDOUT;
	return FAILURE;
    }

    return *portCount;
}


/////////////////////////////////////////////////////////////



// This function returns the number of portCount which is
// also the same as the number of "netreadListener" threads
// spawned by this function.
//
int Do_netread( const int nBytesWant, const int fileSize, pthread_t *pTids, int *portCount, char *portList )
{
    *portCount = 0;
    portList[0] = '\0';


    //
    // Step 1: Calculate the actual number of bytes need to be
    //         read from the file.
    //
    if ( nBytesWant <= 0 ) {
        // Cannot want negative number of bytes
        return 0;  // No parts count to return
    }

    int iBytesForRead = 0;
    if ( nBytesWant <= fileSize ) {
        // Want less bytes than fileSize
        iBytesForRead = nBytesWant;
    }
    else {
        // Want more bytes than fileSize
        iBytesForRead = fileSize;
    }



    //
    // Step 2: Calculate the number of ports need to read
    //         "iBytesForRead" bytes of data from this file.
    //         Each port will handle one part of the data file.
    //         So, portCount also represents the total number
    //         of file parts count.
    //
    int portWanted = (iBytesForRead / (int)DATA_CHUNK_SIZE);
    if ( (iBytesForRead % DATA_CHUNK_SIZE) != 0 ) portWanted++;

    if (portWanted <= 0) portWanted = 1;
    if (portWanted > MAX_FILE_TRANSFER_SOCKETS) portWanted = MAX_FILE_TRANSFER_SOCKETS;

    //printf("netfileserver: Do_netread %d ports are needed to transfer %d bytes\n",
    //               portWanted, iBytesForRead);

    //
    // Step 3: Try to open the number of ports as
    //         calculated from above.  We may get
    //         less than the number of ports wanted
    //         because some ports may be in use.
    //
    int sockfd = -1;
    int port = NET_SERVER_PORT_NUM + 1;
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
            *portCount = (*portCount) + 1;
            char sTemp[16] = "";
            sprintf(sTemp, "%d,", port);
            strcat(portList, sTemp);

            //
            // Step 4: Spawn a new netreadListener thread to
            //         send data to the client
            //
            int *pSockfd = malloc(sizeof(int));
            *pSockfd= sockfd;
            pthread_create(&pTids[j], NULL, &netreadListener, pSockfd );

            //printf("netfileserver: Do_netread spawned thread %ld, sockfd= %d\n",pTids[j],*pSockfd);
            j++;
        }

        port++;

        // Break the for loop if I got all the ports I wanted
        if ( *portCount >= portWanted) break;
    }

    //printf("netfileserver: Do_netread portWanted= %d, portCount= %d, portList= %s\n",
    //               portWanted, *portCount, portList);

    if ( *portCount <= 0 ) {
        // All ports are in use.  Cannot do net read now.
        printf("netfileserver: no port is available.  Cannot do netread now.\n");
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
    // "libnetfiles.h" header file called "NET_SERVER_PORT_NUM".
    //
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
        // This port may have already opened by another thread
        if ( errno == EADDRINUSE ) {
            //fprintf(stderr,"netfileserver: port %d already in use, errno= %d\n",
            //          port, errno);
            errno = 0;
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

    //printf("netfileserver: socket %d is listening and binded to port %d\n", sockfd, port);
    return sockfd;
}


/////////////////////////////////////////////////////////////



//
// matchFD() function returns the index of the FD table that
// contains the NET_FD_TYPE entry that we are looking for.
// The return value is **NOT** the actual file descriptor
// value.  In order to get the actual file descriptor, you
// must do this:
//
// FD_Table[rc].fd  where rc is the value returned by matchFD()
//

int matchFD( NET_FD_TYPE *newFd )
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

NET_FD_TYPE *LookupFDtable( const int netfd )
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
    rc = matchFD( newFd );
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

int tableFull() {
	int full = 1; //true initially
	int i;
	for (i = 0; i < FD_TABLE_SIZE; i++) {
		if (FD_Table[i].pathname == '\0') {
			full = 0;
			return full;
		}
	}
	return full;
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
    // Search each entry in the file descriptor table looking
    // for the pathname.
    //
    for (i=0; i < FD_TABLE_SIZE; i++) {
        if (strcmp(FD_Table[i].pathname, newFd->pathname) == 0)
        {
            //
            // Found the specified pathname.  This file
            // has already been opened by a client.
            //
            FILE_CONNECTION_MODE  fc = FD_Table[i].fcMode;
            int  oFlag = FD_Table[i].fileOpenFlags;


            //
            // I am told to open this file in a new mode of
            // operation.  I must determine if that is allowed.
            //
            switch (newFd->fcMode) {
                case TRANSACTION_MODE:
                    //
                    // For transaction mode, that means this file must
                    // not be opened by another client for any reason.
                    //
                    return FALSE;  // Already opened by another client
                    break;

                case EXCLUSIVE_MODE:
                case UNRESTRICTED_MODE:
                    //
                    // Check if this file can be opened in exclusive mode.
                    // This means no fd has been assigned to this file that
                    // has any kind of write permission (i.e. O_WRONLY or O_RDWR)
                    //
                    if (fc == TRANSACTION_MODE) return FALSE;

                    switch (newFd->fileOpenFlags) {
                        case O_RDONLY:
                            // Allow to open
                            break;

                        case O_WRONLY:
                        case O_RDWR:
                            if ((fc == EXCLUSIVE_MODE) && (oFlag != O_RDONLY))
                                return FALSE;

                            break;
                     }
                     break;

                case INVALID_FILE_MODE:
                     return FALSE;
                     break;

            } // Switch on new wanted connection mode

        }  // Found the pathname in FD_Table

    }  // For each entry in FD_Table


    //
    // At this point, I have passed all tests.  I am allowed
    // to open this file in the specified connection mode and
    // file open flags.
    //
    return TRUE;
}

/////////////////////////////////////////////////////////////


int canWrite( const int netfd, const int nBytes)
{
    //
    // Check if this netfd is opened for O_RDONLY
    //

    NET_FD_TYPE *pFD = LookupFDtable(netfd);
    if ( pFD == NULL ) {
        // There is no such netfd in my file
        // descriptor table.
        errno = EBADF;
        return FAILURE;
    }

    if (pFD->fileOpenFlags == O_RDONLY) {
        // This netfd is opened for read-only mode
        errno = EACCES;
        return FAILURE;
    }

    if (nBytes < 0) {
        // Invalid input argument.  Cannot write negative
        // number of bytes into a file.
        errno = EINVAL;
        return FAILURE;
    }

    // This "netfd" is allowed for writing
    return SUCCESS;
}

/////////////////////////////////////////////////////////////


int canRead( const int netfd, const int nBytesWant, int *fileSize)
{
    *fileSize = 0;

    //
    // Check if this netfd is opened for O_RDONLY
    //

    NET_FD_TYPE *fileInfo = LookupFDtable(netfd);
    if ( fileInfo == NULL ) {
        // There is no such netfd in my file
        // descriptor table.
        errno = EBADF;
        return FAILURE;
    }

    if (fileInfo->fileOpenFlags == O_WRONLY) {
        // This netfd is opened for write-only mode
        errno = EACCES;
        return FAILURE;
    }

    if (nBytesWant < 0) {
        // Invalid input argument.  Cannot read negative
        // number of bytes from a file.
        errno = EINVAL;
        return FAILURE;
    }

    //
    // Get the file size
    //
    FILE *fpRead = NULL;

    // Open the file for reading
    fpRead = fopen(fileInfo->pathname,"r");
    if ( fpRead == NULL ) {
        // Fail to open the temp file
        fprintf(stderr,"netfileserver: canRead: fails to open \"%s\" for read, errno= %d\n",
                  fileInfo->pathname, errno);
        errno = EACCES;
        return FAILURE;
    }

    // Go to the end of the temp file.
    if (fseek(fpRead, 0L, SEEK_END) == 0) {
        *fileSize = ftell(fpRead);   // Get the size of the file.
    }
    if (fpRead != NULL) fclose(fpRead);


    // This "netfd" is allowed for reading
    return SUCCESS;
}


/////////////////////////////////////////////////////////////


void *netwriteListener( void *sfd )
{
    const int sockfd = *((int *)sfd);

    int newsockfd = 0;
    struct sockaddr_in  cli_addr;
    int clilen = sizeof(cli_addr);
    int rc = 0;

    char msg[MSG_SIZE] = "";
    char myThreadLabel[128] = "";
    sprintf(myThreadLabel, "netfileserver: netwriteListener %ld,", pthread_self());


    free(sfd);
    //printf("%s waiting to accept from sockfd %d\n", myThreadLabel, sockfd);
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
    //printf("%s received \"%s\"\n", myThreadLabel, msg);

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
    //printf("%s responded \"%s\"\n", myThreadLabel, msg);


    //
    // Read nBytes of data from the client
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
    //printf("%s received %d bytes of data\n", myThreadLabel, nBytes);


    //
    // Save the received data into a temporary file
    // using the sequence number as part of the file name
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
    //printf("%s responded \"%s\"\n", myThreadLabel, msg);

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


    //printf("netfileserver: savePartfile: netfd= \"%d\"\n", netfd);
    //printFDtable();
    //
    // Lookup file information from the given netfd to compose
    // the temporary file name.  It is the target filename
    // with a numeric extension such as ".1", ".2", etc.
    //
    fileInfo = LookupFDtable( netfd );
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
    fileInfo = LookupFDtable( netfd );
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

                    if (data != NULL) free(data);
                    data = NULL;
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


    //printf("netfileserver: reconstruct: created \"%s\", filesize= %ld\n",
    //         fileInfo->pathname, iTotalFileSize);

    return iTotalFileSize;
}


/////////////////////////////////////////////////////////////


void *netreadListener( void *sfd )
{
    const int sockfd = *((int *)sfd);

    int newsockfd = 0;
    struct sockaddr_in  cli_addr;
    int clilen = sizeof(cli_addr);
    int rc = 0;

    char msg[MSG_SIZE] = "";
    char myThreadLabel[128] = "";
    sprintf(myThreadLabel, "netfileserver: netreadListener %ld,", pthread_self());


    free(sfd);
    //printf("%s waiting to accept from sockfd %d\n", myThreadLabel, sockfd);
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
    //     netread, netfd, seqNum, iStartPos, nBytes
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
    //printf("%s received \"%s\"\n", myThreadLabel, msg);

    int netFunc   = -1;
    int netfd     =  0;
    int seqNum    = -1;
    int iStartPos = -1;
    int nBytes    = -1;
    sscanf(msg, "%d,%d,%d,%d,%d", &netFunc, &netfd, &seqNum, &iStartPos, &nBytes);

    //printf("%s netFunc= %d, netfd= %d, seqNum= %d, iStartPos= %d, nBytes= %d\n",
    //         myThreadLabel, netFunc, netfd, seqNum, iStartPos, nBytes);


    //
    // read "nBytes" of data starting at position "iStartPos"
    // from the file referred to as "netfd"
    //
    char *pData = NULL;
    pData = readFile( netfd, iStartPos, nBytes);


    //
    // Send "nBytes" of data to the client
    //
    if (( pData != NULL) && (nBytes > 0 )) {
        rc = write(newsockfd, pData, nBytes);
        if ( rc < 0 ) {
            fprintf(stderr,"%s fails to send %d bytes of data to client\n", myThreadLabel, nBytes);
            if ( pData != NULL ) free(pData);
            if ( sockfd != 0 ) close(sockfd);
            rc = FAILURE;
            pthread_exit( &rc );
        }
        if ( pData != NULL ) free(pData);
        //printf("%s sent %d bytes of data to client\n", myThreadLabel, nBytes);
    }



    //
    // Read the response from the client saying the number
    // of bytes received.  The incoming message format is:
    //
    //     resultCode, errno, h_errno, nBytes
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

    //printf("%s received \"%s\"\n", myThreadLabel, msg);

    int resultCode  = FAILURE;
    int *pBytesRecv  = malloc(sizeof(int));
    sscanf(msg, "%d,%d,%d,%d", &resultCode, &errno, &h_errno, pBytesRecv);

    //printf("%s resultCode= %d, errno= %d, h_errno= %d, iBytesRecv= %d\n",
    //         myThreadLabel, resultCode, errno, h_errno, *pBytesRecv);


    if ( newsockfd != 0 ) close(newsockfd);
    rc = SUCCESS;
    pthread_exit( pBytesRecv );
}


/////////////////////////////////////////////////////////////


char *readFile( const int netfd, const int iStartPos, const int iBytesWanted)
{
    NET_FD_TYPE  *fileInfo = NULL;
    FILE *fpRead = NULL;
    char *pData  = NULL;


    if ((iStartPos <0) || (iBytesWanted <=0)) return NULL;

    // Find the file to read from
    fileInfo = LookupFDtable( netfd );

    if ( fileInfo == NULL ) return NULL;
    //printf("netfileserver: readFile: pathname= \"%s\"\n", fileInfo->pathname);

    // Open the data file for reading
    fpRead = fopen(fileInfo->pathname,"r");
    if ( fpRead == NULL ) {
        // Fail to open the data file for reading
        fprintf(stderr,"netfileserver: readFile: fails to open \"%s\" for read, errno= %d\n",
                   fileInfo->pathname, errno);
        return NULL;
    }


    //
    // Set the file position to "iStartPos" relative
    // to the beginning of the file.
    //
    if (fseek(fpRead, iStartPos, SEEK_SET) == 0) {
        pData = malloc( iBytesWanted * sizeof(char) );
        bzero(pData, iBytesWanted);

        if ( pData != NULL ) {
            int iBytesRead = 0;
            iBytesRead = (int)fread(pData, sizeof(char), iBytesWanted, fpRead);
            if ( iBytesRead > 0) {
                // Read "iBytesRead" from the file
                if (fpRead != NULL) fclose(fpRead);
                return pData;
            }
        }
    } // End of fseek()


    // Fail to read from the file
    fprintf(stderr,"netfileserver: readFile: fails to read \"%s\", errno= %d, iStartPos= %d, iBytesWanted= %d\n",
               fileInfo->pathname, errno, iStartPos, iBytesWanted);

    if (pData != NULL)  free(pData);
    if (fpRead != NULL) fclose(fpRead);
    return NULL;
}
