#include "types.h"
#include "user.h"
#include "clone.h"

int dummy_func2(void *args)
{
    printf(1, "Hello from grandchild thread!\n");
    sleep(20);
    exit(); // Calling return here tries to jump back to a caller, but there's nothing to return to on stack we create so it causes an exit - should probably be void
}

int dummy_func(void *args)
{
    printf(1, "Hello from child thread!\n");

    args = 0;
    struct clone_args clargs = {.dummy = "Test"};
    void *stack = malloc(100);
    printf(1, "CALLING CLONE WITH FUNCTION PTR: %p, STACK PTR: %p\n", dummy_func, stack);
    int tid = clone(dummy_func2, stack, args, &clargs);
    printf(1, "GOT BACK TID: %d\n", tid);

    sleep(20);
    exit(); // Calling return here tries to jump back to a caller, but there's nothing to return to on stack we create so it causes an exit - should probably be void
}

static void clonetest(void)
{  
    void *args, *stack;
    args = 0;
    stack = malloc(100);

    struct clone_args clargs = {.dummy = "Test"};
    printf(1, "CALLING CLONE WITH FUNCTION PTR: %p, STACK PTR: %p\n", dummy_func, stack);
    int tid = clone(dummy_func, stack, args, &clargs);
    printf(1, "GOT BACK TID: %d\n", tid);

    sleep(5);
}


int main(void)
{
    clonetest();
    exit();
}