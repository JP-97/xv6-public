#include "fcntl.h"
#include "types.h"
#include "user.h"


#define stdout 1
int main(void)
{
    int fd;
    int i;
    char buf[50];
    printf(stdout, "Starting with read count:%d\n", getreadcount());

    printf(stdout, "small file test\n");
    
    fd = open("small", O_CREATE|O_RDWR);
    if(fd >= 0){
        printf(stdout, "creat small succeeded; ok\n");
    } else {
        printf(stdout, "error: creat small failed!\n");
        exit();
    }
    
    if(write(fd, "test", strlen("test")) != strlen("test")){
        printf(stdout, "error: write failed\n");
        exit();
    }

    printf(stdout, "writes ok\n");
    close(fd);

    fd = open("small", O_RDONLY);

    if(fd >= 0){
        printf(stdout, "open small succeeded ok\n");
    } else {
        printf(stdout, "error: open small failed!\n");
        exit();
    }

    i = read(fd, buf, strlen("test"));
    if(i == strlen("test")){
        printf(stdout, "read succeeded ok\n");
    } else {
        printf(stdout, "read failed\n");
        exit();
    }
    close(fd);

    printf(stdout, "Total number of reads now: %d\n", getreadcount());
}