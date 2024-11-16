#include <stdio.h>

void
printVersion(char *whoami, char *Version ) {
   fprintf(stderr, "%s version %s\n", whoami, Version );
#ifdef DEBUG
   fprintf(stderr, "Compiled with DEBUG enabled. DEBUG level: %d\n", DEBUG);
#endif
   fprintf(stderr, "%s Copyright (C) 2023 John F Dey\n", whoami );
   fprintf(stderr, "ppurge comes with ABSOLUTELY NO WARRANTY;\n" );
   fprintf(stderr, "This is free software, you can redistribute it and/or\n");
   fprintf(stderr, "modify it under the\nterms of the GNU General Public");
   fprintf(stderr, " License as published by the Free Software Foundation;\n");
   fprintf(stderr, "GPL version 3 License\n");
}
