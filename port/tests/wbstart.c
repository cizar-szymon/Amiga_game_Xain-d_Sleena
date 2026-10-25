/* WBStart: starts a program as a double click on its icon does (workbench.library
 * OpenWorkbenchObjectA, OS 3.5+: the Workbench start-up message, the icon's tool types and
 * stack, no Shell). Used after every build to check the start from the icon.
 *
 *   WBStart PROGRAM          e.g. WBStart Work:XainSleena/XainSleena_Fast
 */
#include <stdio.h>
#include <proto/exec.h>
#include <proto/wb.h>

struct Library *WorkbenchBase;

int main(int argc, char **argv)
{
    int ok;
    if (argc < 2) {
        printf("usage: WBStart PROGRAM\n");
        return 5;
    }
    WorkbenchBase = OpenLibrary((STRPTR)"workbench.library", 44);
    if (!WorkbenchBase) {
        printf("WBStart: workbench.library 44 (OS 3.5+) needed\n");
        return 20;
    }
    ok = OpenWorkbenchObjectA((STRPTR)argv[1], NULL) != 0;
    CloseLibrary(WorkbenchBase);
    printf("WBStart %s: %s\n", argv[1], ok ? "started" : "FAILED");
    return ok ? 0 : 10;
}
