#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <sys/syscall.h>

typedef struct Memory {
    uintptr_t begin, end, offset;
} Memory;

// this compiler exiension is needed so compiler doesn't optimize unused function away
// this marks that this MUST be included in the final executable as a function
// ig i could've used -O0 compiler flag but that's not fun
__attribute__((noinline, used))
int hello_world(void) {
    write(1, "Hello world\n", 12);
    return 69;
}

// Here we read the /proc/self/maps to find memory regions
// that kernel kindly allocated for our program
// It is found by using the full path of the executable
int find_executable_regions(Memory *memory) {
    // Here we are getting the process pathname
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe",
                           exe_path,
                           sizeof(exe_path) - 1);

    if (len == -1) {
        return 0;
    }

    exe_path[len] = '\0';

    // Getting memory regions of the process
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        perror("Could not open /proc/self/maps");
        return 0;
    }

    // Parsing the scheiße and extracting memory block's
    // beginning, end and offset
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        unsigned long start, end, offset;
        char perms[5];
        char path[4096] = {0};

        if (sscanf(line,
                   "%lx-%lx %4s %lx %*s %*s %4095[^\n]",
                   &start, &end, perms, &offset, path) >= 3)
        {
            if (strcmp(perms, "r-xp") == 0) {
                if(strcmp(path, exe_path) == 0) {
                    memory->begin = start;
                    memory->end = end;
                    memory->offset = offset;
                }
            }
        }
    }

    fclose(maps);
    return 1;
}

// Some calls may never return and hang indefinetely so we set up a timer
// If the call hasn't finished in specified time interval - kill the fucker
static int wait_with_timeout(pid_t pid, int *status, int timeout_ms)
{
    const int interval_us = 1000;
    int elapsed_us = 0;

    while (elapsed_us < timeout_ms * 1000) {
        pid_t r = waitpid(pid, status, WNOHANG);

        if (r == pid)
            return 0;

        if (r == -1)
            return -1;

        usleep(interval_us);
        elapsed_us += interval_us;
    }

    // Probe got stuck
    kill(pid, SIGKILL);

    if (waitpid(pid, status, 0) == -1)
        return -1;

    return 1;
}

int scan_for_address(Memory *mem) {
    // Scanning all the addresses
    for(uintptr_t addr = mem->begin; addr < mem->end; ++addr){
        // Forking a child so we can catch its return codes and errors
        // Also it is done so main heap isn't corrupted accidentally
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            return -1;
        }

        // Child performing black magic with address conversion
        if(pid == 0) {
            // This shit is just for debugging
            printf("probe child pid=%d addr=%lx\n",
                   getpid(),
                   (unsigned long)addr);
            fflush(stdout);

            // Setting strict setcomp mode so this shit can't run any other functions except:
            // read, write, _exit, and sigreturn
            if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT) < 0) {
                return -1;
            }

            // Casting pointer to the function pointer with function's signature
            int (*fn)(void) = (int(*)(void))addr;
            // Calling the mf
            int result = fn();

            // We can't use _exit() here cuz this is prohibited by setcomp
            if(result == 69) {
                syscall(SYS_exit, 69);
            }
            syscall(SYS_exit, 0);
        }

        // Back to parent
        int status;

        // Checking timeouts
        int wr = wait_with_timeout(pid, &status, 10);
        if(wr == -1) {
            perror("wait");
            return -1;
        }

        // If timeout is caught, we give up on current address
        if (wr == 1) {
            printf("TIMEOUT: 0x%lx\n", (unsigned long)addr);
            continue;
        }

        // If process really exited and code was 69 then we caught the fucker
        // This is the address of the hello_world function
        if(WIFEXITED(status) && WEXITSTATUS(status) == 69) {
            printf("FOUND: 0x%lx\n", ((unsigned long)addr)+(mem->offset));
            return 0;
        }
    }
    return -1;
}

int main() {
    Memory mem = {0};

    // Looking for executable region
    int rc = find_executable_regions(&mem);
    if(!rc) {
        perror("Error finding memory region!");
        return 1;
    }
    printf("[%d] Executable region: %lx-%lx.\n Offset: %lx\n",
           getpid(), mem.begin, mem.end, mem.offset);

    // Starting brute forcing memory lol
    rc = scan_for_address(&mem);
    if(rc < 0) {
        perror("Error while scanning");
        return 1;
    }

    return 0;
}
