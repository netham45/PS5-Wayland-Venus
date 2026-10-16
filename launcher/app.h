/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <sys/types.h>
/* Payload Manager v0.5.2 src/process_mgr.c:19-26,63-66.
 * This exact naturally aligned layout was hardware-confirmed by probe.c. */
typedef struct app_info {
 uint32_t app_id;
 uint64_t unknown1;
 char title_id[14];
 char unknown2[0x3c];
} app_info_t;
int sceKernelGetAppInfo(pid_t pid, app_info_t *info);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
/* SDL release-2.30.x-ps5 src/main/ps5/SDL_ps5_main.c:12,18.
 * RetroArch 7d45412b src/main.cpp:704-725 also uses shell-owned native exit. */
int sceSystemServiceLoadExec(const char *path, const char **argv);
/* PS5_Vulkan f410e67 payload/ps5vkctl/main.c:132-135,245-282. */
uint32_t sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t app_id, char *title_id);
int sceLncUtilSuspendApp(uint32_t app_id);
uint32_t sceLncUtilKillApp(uint32_t app_id);
