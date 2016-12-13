
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "libnetfiles.h"



/////////////////////////////////////////////////////////////
//
// Function declarations 
//
/////////////////////////////////////////////////////////////

void emptyFDtable( char *hostname );


 

/////////////////////////////////////////////////////////////
//
// Declare global variables
//
/////////////////////////////////////////////////////////////






/////////////////////////////////////////////////////////////

void emptyFDtable( char *hostname )
{
    int i = 0;
    int fd = 0;

    // 
    // Clean up FD table before begin testing.
    //
    netserverinit( hostname, UNRESTRICTED_MODE );
    for (i=0; i < FD_TABLE_SIZE ; i++) {
        fd = (-10 * (i+1));
        netclose(fd);
        //printf("emptyFDtable(): netclose(%d)\n", fd);
    };
}


/////////////////////////////////////////////////////////////


int main(int argc, char *argv[])
{
    char *hostname = NULL;
    int  rc = FAILURE;
    int  fd = -1;
    char buf[256] = "";
    bzero( buf, 256);


    if (argc < 2) {
        fprintf(stderr, "Usage: %s hostname\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    hostname = argv[1];


    // 
    // Clean up FD table before begin testing.
    //
    emptyFDtable( hostname );


    //
    // Test 01: netserverinit in exclusive mode
    //
    rc = netserverinit( hostname, EXCLUSIVE_MODE );
    if ( rc == SUCCESS ) 
    {
        printf("test 01: PASSED: netserverinit \"%s\", exclusive, errno= %d, h_errno= %d\n",
                         hostname,errno,h_errno);
    } else {
        printf("test 01: FAILED: netserverinit \"%s\", exclusive, errno= %d (%s), h_errno= %d\n",
                         hostname,errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 02: netopen "./testdata/junk.txt", this test should pass
    //
    rc = netopen( "./testdata/junk.txt", O_RDONLY); 
    if ( rc == FAILURE ) 
    {
        printf("test 02: FAILED: netopen(\"junk.txt\",O_RDONLY) exclusive, errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    } else {
        printf("test 02: PASSED: netopen(\"junk.txt\",O_RDONLY) exclusive returns fd= %d, errno= %d, h_errno= %d\n",
                 rc, errno,h_errno);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 03: netclose "./testdata/junk.txt", this test should pass
    //
    fd = netopen("./testdata/junk.txt", O_RDONLY); 
    rc = netclose( fd );
    if ( rc != FAILURE ) 
    {
        printf("test 03: PASSED: netclose(%d) returns %d, errno= %d, h_errno= %d\n",
                 fd, rc, errno,h_errno);
    } else {
        printf("test 03: FAILED: netclose(%d) returns %d, errno= %d (%s), h_errno= %d\n",
                 fd, rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    // Test 04: call netserverinit again with a bad hostname
    rc = netserverinit( "NoSuchHostName", TRANSACTION_MODE);
    if (( rc == FAILURE ) && (h_errno == HOST_NOT_FOUND))
    {
        printf("test 04: PASSED: netserverinit \"NoSuchHostName\" returns HOST_NOT_FOUND, errno= %d (%s), h_errno= %d\n",
                  errno, strerror(errno), h_errno);
    } else {
        printf("test 04: FAILED: netserverinit \"NoSuchHostName\" did not return HOST_NOT_FOUND, transaction, errno= %d (%s), h_errno= %d\n",
                  errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    // Test 05: netopen "./testdata/junk.txt", this test should fail now
    rc = netopen( "./testdata/junk.txt", O_RDWR); 
    if ( rc == FAILURE ) 
    {
        printf("test 05: PASSED: netopen failed because no netserverinit, errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
    } else {
        printf("test 05: FAILED: netopen should fail because no netserverinit, errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 06: netserverinit in unrestricted mode
    //
    rc = netserverinit( hostname, UNRESTRICTED_MODE );
    if ( rc == SUCCESS ) 
    {
        printf("test 06: PASSED: netserverinit \"%s\", unrestricted, errno= %d, h_errno= %d\n",
                         hostname,errno,h_errno);
    } else {
        printf("test 06: FAILED: netserverinit \"%s\", unrestricted, errno= %d (%s), h_errno= %d\n",
                         hostname,errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 07: netopen with a NULL pathname.  This test should fail.
    //
    rc = netopen( NULL, O_RDWR); 
    if ( rc == FAILURE ) 
    {
        printf("test 07: PASSED: netopen with NULL pathname, errno= %d (%s), h_errno= %d\n",
                         errno, strerror(errno), h_errno);
    } else {
        printf("test 07: FAILED: netopen with NULL pathname, errno= %d (%s), h_errno= %d\n",
                         errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 08: netopen with a blank pathname.  This test should fail.
    //
    rc = netopen( "", O_RDWR); 
    if ( rc == FAILURE ) 
    {
        printf("test 08: PASSED: netopen with blank pathname, errno= %d (%s), h_errno= %d\n",
                         errno, strerror(errno), h_errno);
    } else {
        printf("test 08: FAILED: netopen with blank pathname, errno= %d (%s), h_errno= %d\n",
                         errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 09: netopen with a non-existing pathname for read/write access.  
    //          This test should pass even though the file does not exist.
    //
    rc = netopen( "NoSuchFile.xyz", O_RDWR); 
    if ( rc != FAILURE ) 
    {
        printf("test 09: PASSED: netopen(\"NoSuchFile.xyz\",O_RDWR) unrestrict, returns fd= %d, errno= %d, h_errno= %d\n",
                 rc, errno, h_errno);
    } else {
        printf("test 09: FAILED: netopen(\"NoSuchFile.xyz\",O_RDWR) unrestrict, returns fd= %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 10: netopen with a non-existing pathname for read-only access.  
    //          This test should fail because the file does not exist.
    //
    rc = netopen( "NoSuchFile.xyz", O_RDONLY); 
    if ((rc == FAILURE) && (errno == ENOENT)) 
    {
        printf("test 10: PASSED: netopen(\"NoSuchFile.xyz\",O_RDONLY) unrestrict, errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
    } else {
        printf("test 10: FAILED: netopen(\"NoSuchFile.xyz\",O_RDONLY) unrestrict, errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 11: netopen with a directory name.  This test should fail.
    //
    rc = netopen( "/usr", O_RDWR); 
    if ((rc == FAILURE) && (errno == EISDIR)) 
    {
        printf("test 11: PASSED: netopen(\"/usr\",O_RDWR) unrestrict, errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
    } else {
        printf("test 11: FAILED: netopen(\"/usr\",O_RDWR) unrestrict, errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 12: netopen "./testdata/noaccess.txt" without access permission.  
    //          This test should fail.
    //
    rc = netopen( "./testdata/noaccess.txt", O_RDWR); 
    if ((rc == FAILURE) && (errno == EACCES)) 
    {
        printf("test 12: PASSED: netopen(\"./testdata/noaccess.txt\",O_RDWR) unrestrict, errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
    } else {
        printf("test 12: FAILED: netopen(\"./testdata/noaccess.txt\",O_RDWR) unrestrict, errno= %d (%s), h_errno= %d\n",
                 errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 13: netopen "./testdata/junk.txt" with O_RDONLYE, this test should pass
    //
    rc = netopen("./testdata/junk.txt", O_RDONLY); 
    if ( rc != FAILURE ) 
    {
        printf("test 13: PASSED: netopen(\"./testdata/junk.txt\",O_RDONLY) unrestrict returns fd= %d, errno= %d, h_errno= %d\n",
                 rc, errno,h_errno);
    } else {
        printf("test 13: FAILED: netopen(\"./testdata/junk.txt\",O_RDONLY) unrestrict returns fd= %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 14: netopen "./testdata/junk.txt" with O_WRONLY, this test should pass
    //
    rc = netopen("./testdata/junk.txt", O_WRONLY); 
    if ( rc != FAILURE ) 
    {
        printf("test 14: PASSED: netopen(\"./testdata/junk.txt\",O_WRONLY) unrestrict returns fd= %d, errno= %d, h_errno= %d\n",
                 rc, errno,h_errno);
    } else {
        printf("test 14: FAILED: netopen(\"./testdata/junk.txt\",O_WRONLY) unrestrict returns fd= %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 15: netopen "./testdata/junk.txt" with O_RDWR, this test should pass
    //
    rc = netopen("./testdata/junk.txt", O_RDWR); 
    if ( rc != FAILURE ) 
    {
        printf("test 15: PASSED: netopen(\"./testdata/junk.txt\",O_RDWR) unrestrict returns fd= %d, errno= %d, h_errno= %d\n",
                 rc, errno,h_errno);
    } else {
        printf("test 15: FAILED: netopen(\"./testdata/junk.txt\",O_RDWR) unrestrict returns fd= %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 16: netclose "./testdata/junk.txt" with O_WRONLY
    //
    fd = netopen("./testdata/junk.txt", O_WRONLY); 
    rc = netclose( fd );
    if ( rc != FAILURE ) 
    {
        printf("test 16: PASSED: netclose(%d) returns %d, errno= %d, h_errno= %d\n",
                 fd, rc, errno,h_errno);
    } else {
        printf("test 16: FAILED: netclose(%d) returns %d, errno= %d (%s), h_errno= %d\n",
                 fd, rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 17: netclose "./testdata/junk.txt" with O_RDWR
    //
    fd = netopen("./testdata/junk.txt", O_RDWR); 
    rc = netclose( fd );
    if ( rc != FAILURE ) 
    {
        printf("test 17: PASSED: netclose(%d) returns %d, errno= %d, h_errno= %d\n",
                 fd, rc, errno,h_errno);
    } else {
        printf("test 17: FAILED: netclose(%d) returns %d, errno= %d (%s), h_errno= %d\n",
                 fd, rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 18: netclose with an invalid fd, this test should fail
    //
    rc = netclose( 123 );
    if ((rc == FAILURE) && (errno == EBADF))
    {
        printf("test 18: PASSED: netclose(123) returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
    } else {
        printf("test 18: FAILED: netclose(123) returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 19: netserverinit in exclusive mode
    //
    rc = netserverinit( hostname, EXCLUSIVE_MODE );
    if ( rc == SUCCESS ) 
    {
        printf("test 19: PASSED: netserverinit \"%s\", exclusive, errno= %d, h_errno= %d\n",
                         hostname,errno,h_errno);
    } else {
        printf("test 19: FAILED: netserverinit \"%s\", exclusive, errno= %d (%s), h_errno= %d\n",
                         hostname,errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 20: netopen "./testdata/junk.txt" with O_RDWR in exclusive mode. 
    //          This test should pass.
    //
    rc = netopen("./testdata/junk.txt", O_RDWR); 
    if ( rc != FAILURE ) 
    {
        printf("test 20: PASSED: netopen(\"./testdata/junk.txt\",O_RDWR) exclusive returns fd= %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
    } else {
        printf("test 20: FAILED: netopen(\"./testdata/junk.txt\",O_RDWR) exclusive returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 21: netopen "./testdata/junk.txt" with O_RDONLY in exclusive mode. 
    //          This test should pass because I am only openning the file
    //          in read-only mode.
    //
    rc = netopen("./testdata/junk.txt", O_RDONLY); 
    if (rc != FAILURE)
    {
        printf("test 21: PASSED: netopen(\"./testdata/junk.txt\",O_RDONLY) exclusive returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
    } else {
        printf("test 21: FAILED: netopen(\"./testdata/junk.txt\",O_RDONLY) exclusive returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 22: netopen "./testdata/junk.txt" with O_WRONLY in exclusive mode. 
    //          This test should fail because this file i already opened for
    //          writing by another fd in test case 19 above.
    //
    rc = netopen("./testdata/junk.txt", O_WRONLY); 
    if ((rc == FAILURE) && (errno == EACCES))
    {
        printf("test 22: PASSED: netopen(\"./testdata/junk.txt\",O_WRONLY) exclusive returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
    } else {
        printf("test 22: FAILED: netopen(\"./testdata/junk.txt\",O_WRONLY) exclusive returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 23: netclose "./testdata/junk.txt" with O_RDWR in exclusive mode
    //
    fd = netopen("./testdata/junk.txt", O_RDWR); 
    rc = netclose( fd );
    if ( rc != FAILURE ) 
    {
        printf("test 23: PASSED: netclose(%d) returns %d, errno= %d, h_errno= %d\n",
                 fd, rc, errno,h_errno);
    } else {
        printf("test 23: FAILED: netclose(%d) returns %d, errno= %d (%s), h_errno= %d\n",
                 fd, rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 24: netopen "./testdata/junk.txt" with O_WRONLY in exclusive mode. 
    //          This test should pass because I have already closed the fd
    //          that holds the exclusive write to this file in test case 22. 
    //
    rc = netopen("./testdata/junk.txt", O_WRONLY); 
    if ( rc != FAILURE ) 
    {
        printf("test 24: PASSED: netopen(\"./testdata/junk.txt\",O_WRONLY) exclusive returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
    } else {
        printf("test 24: FAILED: netopen(\"./testdata/junk.txt\",O_WRONLY) exclusive returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");



    //
    // Test 25: netclose "./testdata/junk.txt" with O_RDWR in exclusive mode
    //
    fd = netopen("./testdata/junk.txt", O_WRONLY); 
    rc = netclose( fd );
    if ( rc != FAILURE ) 
    {
        printf("test 25: PASSED: netclose(%d) returns %d, errno= %d, h_errno= %d\n",
                 fd, rc, errno,h_errno);
    } else {
        printf("test 25: FAILED: netclose(%d) returns %d, errno= %d (%s), h_errno= %d\n",
                 fd, rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 26: netserverinit in transaction mode
    //
    rc = netserverinit( hostname, TRANSACTION_MODE );
    if ( rc == SUCCESS ) 
    {
        printf("test 26: PASSED: netserverinit \"%s\", transaction, errno= %d, h_errno= %d\n",
                         hostname,errno,h_errno);
    } else {
        printf("test 26: FAILED: netserverinit \"%s\", transaction, errno= %d (%s), h_errno= %d\n",
                         hostname,errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 27: netopen "./testdata/junk.txt" with O_WRONLY in transaction mode
    //          This test should fail because this file is opened.
    //
    rc = netopen("./testdata/junk.txt", O_WRONLY); 
    if ((rc == FAILURE) && (errno == EACCES))
    {
        printf("test 27: PASSED: netopen(\"./testdata/junk.txt\",O_WRONLY) transaction returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
    } else {
        printf("test 27: FAILED: netopen(\"./testdata/junk.txt\",O_WRONLY) transaction returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 28: netopen "./testdata/junk.txt" with O_WRONLY in transaction mode
    //          This test should pass after closing all opened fd on this file.
    //
    emptyFDtable(hostname);

    rc = netopen("./testdata/junk.txt", O_WRONLY); 
    if ( rc != FAILURE ) 
    {
        printf("test 28: PASSED: netopen(\"./testdata/junk.txt\",O_WRONLY) transaction returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
    } else {
        printf("test 28: FAILED: netopen(\"./testdata/junk.txt\",O_WRONLY) transaction returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


    //
    // Test 29: netopen "./testdata/junk.txt" with O_RDWR in unrestricted mode
    //          This test should pass after even when this file has already
    //          been opened in transaction mode.
    //
    rc = netserverinit( hostname, UNRESTRICTED_MODE );
    if ( rc == SUCCESS ) 
    {
        //printf("test 29: PASSED: netserverinit \"%s\", unrestricted, errno= %d, h_errno= %d\n",
        //                 hostname,errno,h_errno);
    } else {
        printf("test 29: FAILED: netserverinit \"%s\", unrestricted, errno= %d (%s), h_errno= %d\n",
                         hostname,errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };

    rc = netopen("./testdata/junk.txt", O_RDWR); 
    if (rc != FAILURE)
    {
        printf("test 29: PASSED: netopen(\"./testdata/junk.txt\",O_RDWR) unrestricted returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
    } else {
        printf("test 29: FAILED: netopen(\"./testdata/junk.txt\",O_RDWR) unrestricted returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");




    //
    // Test 30: netwrite "./testdata/junk.txt" with O_RDWR in unrestricted mode
    //          This test should pass after 
    //
    emptyFDtable(hostname);

    rc = netserverinit( hostname, UNRESTRICTED_MODE );
    if ( rc == SUCCESS ) 
    {
        //printf("test 30: PASSED: netserverinit \"%s\", unrestricted, errno= %d, h_errno= %d\n",
        //                 hostname,errno,h_errno);
    } else {
        printf("test 30: FAILED: netserverinit \"%s\", unrestricted, errno= %d (%s), h_errno= %d\n",
                         hostname,errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };

    fd = netopen("./testdata/writeThis.txt", O_RDWR); 
    if (fd == FAILURE)
    {
        printf("test 30: FAILED: netopen(\"./testdata/writeThis.txt\",O_RDWR) unrestricted returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };


/*
    rc = netwrite(fd, buf, 0);
    rc = netwrite(fd, buf, 2047);
    rc = netwrite(fd, buf, 2048);
    rc = netwrite(fd, buf, 2049);
    rc = netwrite(fd, buf, 4095);
    rc = netwrite(fd, buf, 4096);
    rc = netwrite(fd, buf, 4097);
    rc = netwrite(fd, buf, 20479);
    rc = netwrite(fd, buf, 20480);
    rc = netwrite(fd, buf, 20481);
    rc = netwrite(fd, buf, 22527);
    rc = netwrite(fd, buf, 22528);
    rc = netwrite(fd, buf, 22529);

    rc = netwrite(fd, buf, 2049);
    rc = netwrite(fd, buf, 4096);
    rc = netwrite(fd, buf, 20481);

    rc = netwrite(fd, buf, 3048);

    rc = netwrite(fd, buf, 0);
    strcpy(buf, "Hello Toni.  This has been a long and frustrating ordeal for three weeks!\n");
    strcpy(buf, "1234567890123456789012345678901234567890");
    rc = netwrite(fd, buf, strlen(buf));
*/

    strncpy(buf, "123456789012345678901234567890123456789012345", 45);
    rc = netwrite(fd, buf, 45);

    if (rc != FAILURE)
    {
        printf("test 30: PASSED: netwrite(\"./testdata/writeThis.txt\",O_RDWR) unrestricted returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
    } else {
        printf("test 30: FAILED: netwrite(\"./testdata/writeThis.txt\",O_RDWR) unrestricted returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");





    //
    // Test 31: netread "./testdata/junk.txt" with O_RDONLY in unrestricted mode
    //          This test should pass after 
    //
    rc = netserverinit( hostname, UNRESTRICTED_MODE );
    if ( rc == SUCCESS ) 
    {
        printf("test 31: PASSED: netserverinit \"%s\", unrestricted, errno= %d, h_errno= %d\n",
                         hostname,errno,h_errno);
    } else {
        printf("test 31: FAILED: netserverinit \"%s\", unrestricted, errno= %d (%s), h_errno= %d\n",
                         hostname,errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };

    fd = netopen("./testdata/writeThis.txt", O_RDONLY); 
    if (fd == FAILURE)
    {
        printf("test 31: FAILED: netopen(\"./testdata/writeThis.txt\",O_RDONLY) unrestricted returns %d, errno= %d (%s), h_errno= %d\n",
                 rc, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };


    //strcpy(buf, "Hello Toni.  This has been a long and frustrating ordeal for three weeks!\n");
    //
    //
#define  BYTE_SIZE  23

    int nBytesWant = BYTE_SIZE;
    int nBytesRead =  0;
    char data[ BYTE_SIZE +1 ] = "";
    bzero( data, BYTE_SIZE+1);
    nBytesRead = netread( fd, data, nBytesWant );

    printf("test 31: netread received %d bytes -- %s\n", nBytesRead, data);



    if ( nBytesRead == nBytesWant )
    {
        printf("test 31: PASSED: netread(\"./testdata/writeThis.txt\",O_RDWR) unrestricted returns nBytesRead= %d, errno= %d (%s), h_errno= %d\n",
                 nBytesRead, errno, strerror(errno), h_errno);
    } else {
        printf("test 31: FAILED: netread(\"./testdata/writeThis.txt\",O_RDWR) unrestricted returns nBytesRead= %d, errno= %d (%s), h_errno= %d\n",
                 nBytesRead, errno, strerror(errno), h_errno);
        exit(EXIT_FAILURE);
    };
    printf("---------------------------------------------------------------------------\n");


}


////////////////////////////////////////////////////////////////////////////////

