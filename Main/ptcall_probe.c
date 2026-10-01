#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>\n#include <sys/mman.h>

#include <ps5/kernel.h>
#include "pt.h"

#define TARGET_PID_FILE "/data/trophy_unlocker_target_pid.txt"
#define LOG_FILE "/data/ptcall_probe_log.txt"

static void log_line(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        size_t len = (size_t)((n < (int)sizeof(buf)) ? n : (int)sizeof(buf) - 1);
        write(fd, buf, len);
        write(fd, "\n", 1);
        close(fd);
    }
}

static int read_target_pid(void) {
    char buf[32] = {0};
    int fd = open(TARGET_PID_FILE, O_RDONLY, 0);
    if (fd < 0) return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    return atoi(buf);
}

static intptr_t resolve_getpid(int pid) {
    static const char *mods[] = {
        "libkernel.sprx",
        "libkernel_sys.sprx",
        "libkernel_web.sprx",
        NULL
    };
    for (int i = 0; mods[i]; ++i) {
        uint32_t h = 0;
        if (kernel_dynlib_handle(pid, mods[i], &h) == 0 && h != 0) {
            intptr_t addr = kernel_dynlib_dlsym(pid, h, "getpid");
            if (addr) {
                log_line("[resolve] %s h=0x%08x getpid=0x%lx", mods[i], h, (unsigned long)addr);
                return addr;
            }
        }
    }
    return 0;
}

int main(void) {
    unlink(LOG_FILE);

    int pid = read_target_pid();
    log_line("[start] target pid=%d", pid);
    if (pid <= 0) {
        log_line("[error] invalid target pid");
        return 2;
    }

    intptr_t getpid_addr = resolve_getpid(pid);
    if (!getpid_addr) {
        log_line("[error] getpid not resolved");
        return 3;
    }

    if (pt_attach(pid) != 0) {
        log_line("[error] pt_attach failed errno=%d", errno);
        return 4;
    }
    log_line("[attach] success");

    long remote_pid = pt_call(pid, getpid_addr, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL);
    log_line("[call] getpid returned=%ld", remote_pid);

    int detach_rc = pt_detach(pid, SIGCONT);
    kill(pid, SIGCONT);
    log_line("[detach] rc=%d", detach_rc);

    if (remote_pid != pid) {
        log_line("[result] FAIL expected=%d got=%ld", pid, remote_pid);
        return 5;
    }

    log_line("[result] SUCCESS pt_call executed inside target process");
    return 0;
}
