#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <ps5/kernel.h>
#include "pt.h"

long pt_call(pid_t pid, intptr_t addr, ...);

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


    uint32_t th = 0, uh = 0;
    intptr_t fn_create_ctx = 0, fn_create_handle = 0, fn_register_ctx = 0;
    intptr_t fn_destroy_handle = 0, fn_destroy_ctx = 0;
    intptr_t fn_get_fg = 0, fn_get_initial = 0;

    if (kernel_dynlib_handle(pid, "libSceNpTrophy2.sprx", &th) == 0 && th != 0) {
        fn_create_ctx = kernel_dynlib_dlsym(pid, th, "sceNpTrophy2CreateContext");
        fn_create_handle = kernel_dynlib_dlsym(pid, th, "sceNpTrophy2CreateHandle");
        fn_register_ctx = kernel_dynlib_dlsym(pid, th, "sceNpTrophy2RegisterContext");
        fn_destroy_handle = kernel_dynlib_dlsym(pid, th, "sceNpTrophy2DestroyHandle");
        fn_destroy_ctx = kernel_dynlib_dlsym(pid, th, "sceNpTrophy2DestroyContext");
    }

    if (kernel_dynlib_handle(pid, "libSceUserService.sprx", &uh) == 0 && uh != 0) {
        fn_get_fg = kernel_dynlib_dlsym(pid, uh, "sceUserServiceGetForegroundUser");
        fn_get_initial = kernel_dynlib_dlsym(pid, uh, "sceUserServiceGetInitialUser");
    }

    log_line("[stage3] Trophy2 h=0x%08x CreateCtx=0x%lx CreateHandle=0x%lx Register=0x%lx",
             th, (unsigned long)fn_create_ctx, (unsigned long)fn_create_handle,
             (unsigned long)fn_register_ctx);
    log_line("[stage3] UserService h=0x%08x GetForeground=0x%lx GetInitial=0x%lx",
             uh, (unsigned long)fn_get_fg, (unsigned long)fn_get_initial);

    if (!fn_create_ctx || !fn_create_handle || !fn_register_ctx) {
        log_line("[stage3] ERROR missing required Trophy2 exports");
        return 6;
    }

    if (pt_attach(pid) != 0) {
        log_line("[stage3] ERROR pt_attach errno=%d", errno);
        return 7;
    }
    log_line("[stage3] attach success");

    intptr_t scratch = pt_mmap(pid, 0, 4096, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANON, -1, 0);
    log_line("[stage3] scratch=0x%lx", (unsigned long)scratch);
    if (!scratch || scratch == (intptr_t)-1) {
        pt_detach(pid, SIGCONT);
        kill(pid, SIGCONT);
        log_line("[stage3] ERROR pt_mmap failed");
        return 8;
    }

    int32_t user_id = 0;
    if (fn_get_fg) {
        for (int retry = 0; retry < 6; ++retry) {
            int32_t zero = 0;
            pt_copyin(pid, &zero, scratch + 4, sizeof(zero));
            long urc = pt_call(pid, fn_get_fg,
                               scratch + 4,
                               0ULL, 0ULL, 0ULL, 0ULL, 0ULL);
            pt_copyout(pid, scratch + 4, &user_id, sizeof(user_id));
            log_line("[stage3] GetForegroundUser retry=%d rc=0x%lx uid=%d",
                     retry, (unsigned long)urc, user_id);
            if (user_id > 1) break;
            if (retry < 5) usleep(500000);
        }
    }

    if (user_id <= 1 && fn_get_initial) {
        int32_t zero = 0;
        pt_copyin(pid, &zero, scratch + 4, sizeof(zero));
        long urc = pt_call(pid, fn_get_initial,
                           scratch + 4,
                           0ULL, 0ULL, 0ULL, 0ULL, 0ULL);
        pt_copyout(pid, scratch + 4, &user_id, sizeof(user_id));
        log_line("[stage3] GetInitialUser rc=0x%lx uid=%d",
                 (unsigned long)urc, user_id);
    }

    if (user_id <= 1) {
        log_line("[stage3] ERROR no valid user_id; stop before CreateContext");
        pt_munmap(pid, scratch, 4096);
        pt_detach(pid, SIGCONT);
        kill(pid, SIGCONT);
        return 9;
    }

    int32_t ctx_id = 0;
    pt_copyin(pid, &ctx_id, scratch + 16, sizeof(ctx_id));
    long rc_ctx = pt_call(pid, fn_create_ctx,
                          scratch + 16,
                          (intptr_t)user_id,
                          0ULL,
                          0ULL,
                          0ULL,
                          0ULL);
    pt_copyout(pid, scratch + 16, &ctx_id, sizeof(ctx_id));
    log_line("[stage3] CreateContext rc=0x%lx ctxId=%d user=%d",
             (unsigned long)rc_ctx, ctx_id, user_id);

    if ((int32_t)rc_ctx < 0 || ctx_id <= 0) {
        log_line("[stage3] STOP CreateContext failed; RegisterContext not called");
        pt_munmap(pid, scratch, 4096);
        pt_detach(pid, SIGCONT);
        kill(pid, SIGCONT);
        return 10;
    }

    int32_t handle_id = 0;
    pt_copyin(pid, &handle_id, scratch + 20, sizeof(handle_id));
    long rc_handle = pt_call(pid, fn_create_handle,
                             scratch + 20,
                             0ULL, 0ULL, 0ULL, 0ULL, 0ULL);
    pt_copyout(pid, scratch + 20, &handle_id, sizeof(handle_id));
    log_line("[stage3] CreateHandle rc=0x%lx handleId=%d",
             (unsigned long)rc_handle, handle_id);

    if ((int32_t)rc_handle < 0 || handle_id <= 0) {
        if (fn_destroy_ctx) {
            pt_call(pid, fn_destroy_ctx,
                    (intptr_t)ctx_id,
                    0ULL, 0ULL, 0ULL, 0ULL, 0ULL);
        }
        pt_munmap(pid, scratch, 4096);
        pt_detach(pid, SIGCONT);
        kill(pid, SIGCONT);
        log_line("[stage3] STOP CreateHandle failed");
        return 11;
    }

    long rc_register = pt_call(pid, fn_register_ctx,
                               (intptr_t)ctx_id,
                               (intptr_t)handle_id,
                               0ULL,
                               0ULL, 0ULL, 0ULL);
    log_line("[stage3] RegisterContext rc=0x%lx ctxId=%d handleId=%d",
             (unsigned long)rc_register, ctx_id, handle_id);

    if (fn_destroy_handle) {
        long drh = pt_call(pid, fn_destroy_handle,
                           (intptr_t)handle_id,
                           0ULL, 0ULL, 0ULL, 0ULL, 0ULL);
        log_line("[stage3] DestroyHandle rc=0x%lx", (unsigned long)drh);
    }

    if (fn_destroy_ctx) {
        long drc = pt_call(pid, fn_destroy_ctx,
                           (intptr_t)ctx_id,
                           0ULL, 0ULL, 0ULL, 0ULL, 0ULL);
        log_line("[stage3] DestroyContext rc=0x%lx", (unsigned long)drc);
    }

    log_line("[stage3] munmap rc=%d", pt_munmap(pid, scratch, 4096));
    int d3 = pt_detach(pid, SIGCONT);
    kill(pid, SIGCONT);
    log_line("[stage3] detach rc=%d", d3);
    log_line("[stage3] DONE RegisterContext rc=0x%lx",
             (unsigned long)rc_register);

    return 0;
}
