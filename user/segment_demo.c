#include <stdio.h>
#include <stdlib.h>

int g_initialized = 123;
int g_bss;
const char g_literal[] = "hello from .rdata";

static void demo_function(void)
{
    puts("inside demo_function");
}

int main(void)
{
    int stack_var = 42;
    int* heap_var = (int*)malloc(sizeof(int) * 4);

    if (heap_var == NULL) {
        fputs("malloc failed\n", stderr);
        return 1;
    }

    heap_var[0] = 7;
    demo_function();

    printf("SegmentLayoutDemo is paused. Press Enter to exit.\n");
    printf("main=%p\n", (void*)main);
    printf("demo_function=%p\n", (void*)demo_function);
    printf("g_initialized=%p\n", (void*)&g_initialized);
    printf("g_bss=%p\n", (void*)&g_bss);
    printf("g_literal=%p\n", (const void*)g_literal);
    printf("heap_var=%p\n", (void*)heap_var);
    printf("stack_var=%p\n", (void*)&stack_var);
    getchar();

    free(heap_var);
    return 0;
}
