#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>

void* lcShared = 0;

/*
 * Name of the folder that will appear inside:
 *
 * On My iPhone
 * └── LiveContainer
 *     └── TweakFiles
 *
 * Change this if you want a different folder name.
 */
#define LC_TWEAK_FOLDER_NAME "TweakFiles"

/*
 * Environment variable exposed to tweaks.
 *
 * Tweaks can retrieve it with:
 *
 *     getenv("LC_TWEAKS_FOLDER")
 *
 * and receive the absolute path to the folder.
 */
#define LC_TWEAK_FOLDER_ENV "LC_TWEAKS_FOLDER"


/*
 * Creates:
 *
 *     $HOME/Documents/TweakFiles
 *
 * On iOS, LiveContainer's Documents directory is exposed through
 * the Files app as:
 *
 *     On My iPhone/LiveContainer
 */
static void setupTweakFolder(void) {
    const char *home = getenv("HOME");

    if (!home || home[0] == '\0') {
        fprintf(stderr, "[LiveContainer] HOME is not set\n");
        return;
    }

    char documentsPath[PATH_MAX];

    int documentsLength = snprintf(
        documentsPath,
        sizeof(documentsPath),
        "%s/Documents",
        home
    );

    if (documentsLength < 0 ||
        (size_t)documentsLength >= sizeof(documentsPath)) {
        fprintf(
            stderr,
            "[LiveContainer] Documents path is too long\n"
        );
        return;
    }

    /*
     * Make sure the Documents directory exists.
     *
     * Normally it already exists, but this makes the code robust.
     */
    struct stat documentsStat;

    if (stat(documentsPath, &documentsStat) != 0) {
        if (mkdir(documentsPath, 0755) != 0 && errno != EEXIST) {
            fprintf(
                stderr,
                "[LiveContainer] Failed to create Documents: %s\n",
                strerror(errno)
            );
            return;
        }
    }

    if (!S_ISDIR(documentsStat.st_mode)) {
        /*
         * If stat() succeeded but the path wasn't a directory,
         * don't attempt to continue.
         */
        if (stat(documentsPath, &documentsStat) == 0 &&
            !S_ISDIR(documentsStat.st_mode)) {
            fprintf(
                stderr,
                "[LiveContainer] Documents is not a directory\n"
            );
            return;
        }
    }

    /*
     * Construct:
     *
     *     $HOME/Documents/TweakFiles
     */
    char tweakFolderPath[PATH_MAX];

    int folderLength = snprintf(
        tweakFolderPath,
        sizeof(tweakFolderPath),
        "%s/%s",
        documentsPath,
        LC_TWEAK_FOLDER_NAME
    );

    if (folderLength < 0 ||
        (size_t)folderLength >= sizeof(tweakFolderPath)) {
        fprintf(
            stderr,
            "[LiveContainer] Tweak folder path is too long\n"
        );
        return;
    }

    /*
     * Create the directory.
     *
     * 0755:
     *
     *     owner: read/write/execute
     *     group: read/execute
     *     other: read/execute
     *
     * The directory is still inside LiveContainer's sandbox.
     */
    struct stat folderStat;

    if (stat(tweakFolderPath, &folderStat) != 0) {
        if (mkdir(tweakFolderPath, 0755) != 0 &&
            errno != EEXIST) {

            fprintf(
                stderr,
                "[LiveContainer] Failed to create tweak folder: %s\n",
                strerror(errno)
            );

            return;
        }
    } else if (!S_ISDIR(folderStat.st_mode)) {
        fprintf(
            stderr,
            "[LiveContainer] %s exists but is not a directory\n",
            tweakFolderPath
        );

        return;
    }

    /*
     * Export the path to the process environment.
     *
     * Because tweaks are loaded into this process, they inherit
     * this environment and can call getenv() to retrieve it.
     *
     * The third argument is 1, meaning overwrite an existing
     * value if one happens to exist.
     */
    if (setenv(
            LC_TWEAK_FOLDER_ENV,
            tweakFolderPath,
            1
        ) != 0) {

        fprintf(
            stderr,
            "[LiveContainer] Failed to set %s: %s\n",
            LC_TWEAK_FOLDER_ENV,
            strerror(errno)
        );

        return;
    }

    printf(
        "[LiveContainer] Tweak folder: %s\n",
        tweakFolderPath
    );

    printf(
        "[LiveContainer] %s=%s\n",
        LC_TWEAK_FOLDER_ENV,
        tweakFolderPath
    );
}


int LiveContainerMainC(int argc, char *argv[]) {
    const char *home = getenv("HOME");

    int (*lcMain)(int argc, char *argv[]) = 0;

    if (!home) {
        abort();
    }

    /*
     * Create the Files-visible folder and environment variable
     * BEFORE loading LiveContainerShared and before LiveContainerMain.
     *
     * This means subsequently loaded tweaks can use:
     *
     *     getenv("LC_TWEAKS_FOLDER")
     */
    setupTweakFolder();

    char path[PATH_MAX];

    snprintf(
        path,
        sizeof(path),
        "%s/Library/preloadLibraries.txt",
        home
    );

    FILE *file = fopen(path, "r");

    if (!file) {
        goto loadlc;
    }

    char line[PATH_MAX];

    while (fgets(line, sizeof(line), file)) {

        /*
         * Remove trailing newline if present.
         */
        size_t len = strlen(line);

        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        /*
         * Ignore empty lines.
         */
        if (line[0] == '\0') {
            continue;
        }

        dlopen(
            line,
            RTLD_LAZY | RTLD_GLOBAL
        );
    }

    fclose(file);

    remove(path);


loadlc:

    lcShared = dlopen(
        "@executable_path/Frameworks/LiveContainerShared.framework/LiveContainerShared",
        RTLD_LAZY | RTLD_GLOBAL
    );

    if (!lcShared) {
        fprintf(
            stderr,
            "[LiveContainer] Failed to load LiveContainerShared: %s\n",
            dlerror()
        );

        abort();
    }

    lcMain = dlsym(
        lcShared,
        "LiveContainerMain"
    );

    if (!lcMain) {
        fprintf(
            stderr,
            "[LiveContainer] Failed to find LiveContainerMain: %s\n",
            dlerror()
        );

        abort();
    }

    __attribute__((musttail))
    return lcMain(argc, argv);
}


#ifdef DEBUG

int main(int argc, char *argv[]) {

    if (lcShared == NULL) {
        __attribute__((musttail))
        return LiveContainerMainC(argc, argv);
    }

    int (*callAppMain)(int argc, char *argv[]) =
        dlsym(
            lcShared,
            "callAppMain"
        );

    if (!callAppMain) {
        fprintf(
            stderr,
            "[LiveContainer] Failed to find callAppMain: %s\n",
            dlerror()
        );

        abort();
    }

    __attribute__((musttail))
    return callAppMain(argc, argv);
}

#endif
