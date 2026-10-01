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
#define LOG_FILE "/data/trophy_unlock_id0_log.txt"

static void log_line(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        size_t len = (size_t)((n < (int)sizeof(buf)) ? n : (int)sizeof(buf)-1);
        write(fd, buf, len);
        write(fd, "\n", 1);
        close(fd);
    }
}

static int read_target_pid(void) {
    char buf[32] = {0};
    int fd = open(TARGET_PID_FILE, O_RDONLY, 0);
    if (fd < 0) return 0;
    ssize_t n = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (n <= 0) return 0;
    return atoi(buf);
}

static intptr_t sym(int pid, uint32_t h, const char *name) {
    return kernel_dynlib_dlsym(pid, h, name);
}

int main(void) {
    unlink(LOG_FILE);
    int pid = read_target_pid();
    log_line("[start] pid=%d trophy_id=0", pid);
    if (pid <= 0) return 2;

    uint32_t trophy_h=0, uds_h=0, user_h=0;
    kernel_dynlib_handle(pid, "libSceNpTrophy2.sprx", &trophy_h);
    kernel_dynlib_handle(pid, "libSceNpUniversalDataSystem.sprx", &uds_h);
    kernel_dynlib_handle(pid, "libSceUserService.sprx", &user_h);

    intptr_t t_create_ctx=sym(pid,trophy_h,"sceNpTrophy2CreateContext");
    intptr_t t_create_h=sym(pid,trophy_h,"sceNpTrophy2CreateHandle");
    intptr_t t_register=sym(pid,trophy_h,"sceNpTrophy2RegisterContext");
    intptr_t t_destroy_h=sym(pid,trophy_h,"sceNpTrophy2DestroyHandle");
    intptr_t t_destroy_ctx=sym(pid,trophy_h,"sceNpTrophy2DestroyContext");

    intptr_t get_fg=sym(pid,user_h,"sceUserServiceGetForegroundUser");

    intptr_t u_init=sym(pid,uds_h,"sceNpUniversalDataSystemInitialize");
    intptr_t u_term=sym(pid,uds_h,"sceNpUniversalDataSystemTerminate");
    intptr_t u_create_ctx=sym(pid,uds_h,"sceNpUniversalDataSystemCreateContext");
    intptr_t u_create_h=sym(pid,uds_h,"sceNpUniversalDataSystemCreateHandle");
    intptr_t u_register=sym(pid,uds_h,"sceNpUniversalDataSystemRegisterContext");
    intptr_t u_create_evt=sym(pid,uds_h,"sceNpUniversalDataSystemCreateEvent");
    intptr_t u_set_i32=sym(pid,uds_h,"sceNpUniversalDataSystemEventPropertyObjectSetInt32");
    intptr_t u_post=sym(pid,uds_h,"sceNpUniversalDataSystemPostEvent");
    intptr_t u_destroy_evt=sym(pid,uds_h,"sceNpUniversalDataSystemDestroyEvent");
    intptr_t u_destroy_h=sym(pid,uds_h,"sceNpUniversalDataSystemDestroyHandle");
    intptr_t u_destroy_ctx=sym(pid,uds_h,"sceNpUniversalDataSystemDestroyContext");

    log_line("[mods] trophy=0x%08x uds=0x%08x user=0x%08x", trophy_h, uds_h, user_h);
    log_line("[uds] init=0x%lx createCtx=0x%lx createH=0x%lx reg=0x%lx",
             (unsigned long)u_init,(unsigned long)u_create_ctx,
             (unsigned long)u_create_h,(unsigned long)u_register);
    log_line("[uds] createEvent=0x%lx setInt32=0x%lx post=0x%lx",
             (unsigned long)u_create_evt,(unsigned long)u_set_i32,(unsigned long)u_post);

    if (!t_create_ctx || !t_create_h || !t_register || !get_fg ||
        !u_create_ctx || !u_create_h || !u_register ||
        !u_create_evt || !u_set_i32 || !u_post) {
        log_line("[error] required symbol missing");
        return 3;
    }

    if (pt_attach(pid) != 0) {
        log_line("[error] attach errno=%d", errno);
        return 4;
    }
    log_line("[attach] success");

    intptr_t s=pt_mmap(pid,0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    log_line("[mmap] 0x%lx",(unsigned long)s);
    if (!s || s==(intptr_t)-1) goto detach;

    int32_t uid=0;
    pt_copyin(pid,&uid,s+0x10,4);
    long rc=pt_call(pid,get_fg,s+0x10,0ULL,0ULL,0ULL,0ULL,0ULL);
    pt_copyout(pid,s+0x10,&uid,4);
    log_line("[user] GetForeground rc=0x%lx uid=%d",(unsigned long)rc,uid);
    if (uid<=1) goto cleanup;

    int32_t tctx=0, th=0;
    pt_copyin(pid,&tctx,s+0x20,4);
    rc=pt_call(pid,t_create_ctx,s+0x20,(intptr_t)uid,0ULL,0ULL,0ULL,0ULL);
    pt_copyout(pid,s+0x20,&tctx,4);
    log_line("[trophy] CreateContext rc=0x%lx ctx=%d",(unsigned long)rc,tctx);
    if ((int32_t)rc<0 || tctx<=0) goto cleanup;

    pt_copyin(pid,&th,s+0x24,4);
    rc=pt_call(pid,t_create_h,s+0x24,0ULL,0ULL,0ULL,0ULL,0ULL);
    pt_copyout(pid,s+0x24,&th,4);
    log_line("[trophy] CreateHandle rc=0x%lx handle=%d",(unsigned long)rc,th);
    if ((int32_t)rc<0 || th<=0) goto cleanup_trophy;

    rc=pt_call(pid,t_register,(intptr_t)tctx,(intptr_t)th,0ULL,0ULL,0ULL,0ULL);
    log_line("[trophy] RegisterContext rc=0x%lx",(unsigned long)rc);
    if ((int32_t)rc<0) goto cleanup_trophy;

    int init_owned=0;
    if (u_init) {
        uint64_t init_param[2]={16,128*1024};
        pt_copyin(pid,init_param,s+0x80,sizeof(init_param));
        long irc=pt_call(pid,u_init,s+0x80,0ULL,0ULL,0ULL,0ULL,0ULL);
        log_line("[uds] Initialize rc=0x%lx",(unsigned long)irc);
        if ((int32_t)irc>=0) init_owned=1;
    }

    int32_t uh=0, uctx=0;
    pt_copyin(pid,&uh,s+0xa0,4);
    rc=pt_call(pid,u_create_h,s+0xa0,0ULL,0ULL,0ULL,0ULL,0ULL);
    pt_copyout(pid,s+0xa0,&uh,4);
    log_line("[uds] CreateHandle rc=0x%lx handle=%d",(unsigned long)rc,uh);
    if ((int32_t)rc<0 || uh<=0) goto cleanup_uds_init;

    pt_copyin(pid,&uctx,s+0xa4,4);
    rc=pt_call(pid,u_create_ctx,s+0xa4,(intptr_t)uid,0ULL,0ULL,0ULL,0ULL);
    pt_copyout(pid,s+0xa4,&uctx,4);
    log_line("[uds] CreateContext rc=0x%lx ctx=%d",(unsigned long)rc,uctx);
    if ((int32_t)rc<0 || uctx<=0) goto cleanup_uds_handle;

    rc=pt_call(pid,u_register,(intptr_t)uctx,(intptr_t)uh,0ULL,0ULL,0ULL,0ULL);
    log_line("[uds] RegisterContext rc=0x%lx",(unsigned long)rc);
    if ((int32_t)rc<0) goto cleanup_uds_ctx;

    const char evt_name[]="_UnlockTrophy";
    const char key_name[]="_trophy_id";
    pt_copyin(pid,evt_name,s+0x100,sizeof(evt_name));
    pt_copyin(pid,key_name,s+0x120,sizeof(key_name));

    uint64_t event_ptr=0, prop_ptr=0;
    pt_copyin(pid,&event_ptr,s+0x140,8);
    pt_copyin(pid,&prop_ptr,s+0x148,8);
    rc=pt_call(pid,u_create_evt,s+0x100,0ULL,s+0x140,s+0x148,0ULL,0ULL);
    pt_copyout(pid,s+0x140,&event_ptr,8);
    pt_copyout(pid,s+0x148,&prop_ptr,8);
    log_line("[unlock] CreateEvent rc=0x%lx event=0x%lx prop=0x%lx",
             (unsigned long)rc,(unsigned long)event_ptr,(unsigned long)prop_ptr);
    if ((int32_t)rc<0 || !event_ptr || !prop_ptr) goto cleanup_uds_ctx;

    rc=pt_call(pid,u_set_i32,(intptr_t)prop_ptr,s+0x120,0ULL,0ULL,0ULL,0ULL);
    log_line("[unlock] SetInt32(_trophy_id=0) rc=0x%lx",(unsigned long)rc);
    if ((int32_t)rc<0) goto cleanup_event;

    rc=pt_call(pid,u_post,(intptr_t)uctx,(intptr_t)uh,(intptr_t)event_ptr,0ULL,0ULL,0ULL);
    log_line("[unlock] PostEvent rc=0x%lx",(unsigned long)rc);
    log_line("[result] trophy_id=0 post_rc=0x%lx",(unsigned long)rc);

cleanup_event:
    if (u_destroy_evt && event_ptr)
        log_line("[cleanup] DestroyEvent rc=0x%lx",
                 (unsigned long)pt_call(pid,u_destroy_evt,(intptr_t)event_ptr,0ULL,0ULL,0ULL,0ULL,0ULL));
cleanup_uds_ctx:
    if (u_destroy_ctx && uctx>0)
        log_line("[cleanup] UDS DestroyContext rc=0x%lx",
                 (unsigned long)pt_call(pid,u_destroy_ctx,(intptr_t)uctx,0ULL,0ULL,0ULL,0ULL,0ULL));
cleanup_uds_handle:
    if (u_destroy_h && uh>0)
        log_line("[cleanup] UDS DestroyHandle rc=0x%lx",
                 (unsigned long)pt_call(pid,u_destroy_h,(intptr_t)uh,0ULL,0ULL,0ULL,0ULL,0ULL));
cleanup_uds_init:
    if (init_owned && u_term)
        log_line("[cleanup] UDS Terminate rc=0x%lx",
                 (unsigned long)pt_call(pid,u_term,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL));
cleanup_trophy:
    if (t_destroy_h && th>0)
        log_line("[cleanup] Trophy DestroyHandle rc=0x%lx",
                 (unsigned long)pt_call(pid,t_destroy_h,(intptr_t)th,0ULL,0ULL,0ULL,0ULL,0ULL));
    if (t_destroy_ctx && tctx>0)
        log_line("[cleanup] Trophy DestroyContext rc=0x%lx",
                 (unsigned long)pt_call(pid,t_destroy_ctx,(intptr_t)tctx,0ULL,0ULL,0ULL,0ULL,0ULL));
cleanup:
    pt_munmap(pid,s,4096);
detach:
    {
        int dr=pt_detach(pid,SIGCONT);
        kill(pid,SIGCONT);
        log_line("[detach] rc=%d",dr);
    }
    return 0;
}
