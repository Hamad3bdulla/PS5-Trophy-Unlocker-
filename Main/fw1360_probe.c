#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    char pad1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int open(const char *, int, ...);
int write(int, const void *, size_t);
int close(int);

#define O_WRONLY 0x0001
#define O_CREAT  0x0200
#define O_TRUNC  0x0400

static void notify_text(const char *text) {
    notify_request_t req;
    memset(&req, 0, sizeof req);
    size_t n = strlen(text);
    if (n >= sizeof req.message) n = sizeof req.message - 1;
    memcpy(req.message, text, n);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

int main(void) {
    const char *path = "/data/ps5_trophy_fw1360_probe.txt";
    const char *msg = "FW13.60 probe executed successfully\n";

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        (void)write(fd, msg, strlen(msg));
        close(fd);
    }

    notify_text("PS5 Trophy Unlocker FW13.60 probe executed");
    return 0;
}
