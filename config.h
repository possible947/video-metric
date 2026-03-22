#ifndef CONFIG_H
#define CONFIG_H

/* Path separator used by the project */
#ifdef _WIN32
#   define PATH_SEPARATOR '\\\\'
#else
#   define PATH_SEPARATOR '/'
#endif

#endif /* CONFIG_H */
