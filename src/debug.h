#ifndef DEBUG_H
#define DEBUG_H

// Uncomment to enable debug prints
#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(msg, row) print_line((msg), (row))
#else
#define DEBUG_PRINT(msg, row) ((void)0)
#endif

#endif // DEBUG_H 