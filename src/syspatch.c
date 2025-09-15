#include <string.h>
#include <stdio.h>
#include <pspsdk.h>
#include <pspdisplay.h>
#include <pspdisplay_kernel.h>
#include <pspsysmem_kernel.h>
#include <psputilsforkernel.h>
#include <pspiofilemgr.h>
#include <pspinit.h>

#include <ark.h>
#include <cfwmacros.h>
#include <module2.h>
#include <rebootconfig.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <systemctrl_private.h>

#include "popsdisplay.h"

extern ARKConfig* ark_config;
extern RebootConfigARK* reboot_config;

// Previous Module Start Handler
STMOD_HANDLER previous = NULL;

static int draw_thread = -1;
static volatile int do_draw = 0;
static u32* fake_vram = (u32*)0x0A400000; // use extra RAM for fake framebuffer
int (* DisplaySetFrameBuf)(void*, int, int, int) = NULL;
int (* DisplayGetFrameBuf)(void**, int*, int*, int) = NULL;
int (* DisplayWaitVblankStart)() = NULL;

extern SEConfig* se_config;


// hooked function to copy framebuffer
int (* _sceDisplaySetFrameBufferInternal)(int pri, void *topaddr, int width, int format, int sync) = NULL;
int sceDisplaySetFrameBufferInternalHook(int pri, void *topaddr, int width, int format, int sync){
    int res = 0;
    copyPSPVram(topaddr);
    if (_sceDisplaySetFrameBufferInternal) // passthrough
        res = _sceDisplaySetFrameBufferInternal(pri, topaddr, width, format, sync); 
    return res;
}

// patch pops display to set up our own screen handler
void patchVitaPopsDisplay(SceModule* mod){
    u32 display_func = sctrlHENFindFunction("sceDisplay_Service", "sceDisplay_driver", 0x3E17FE8D);
    if (display_func){
        // protect vita pops vram
        sceKernelAllocPartitionMemory(6, "POPS VRAM CONFIG", PSP_SMEM_Addr, 0x1B0, (void *)0x09FE0000);
        sceKernelAllocPartitionMemory(6, "POPS VRAM", PSP_SMEM_Addr, 0x3C0000, (void *)0x090C0000);
        memset((void *)0x49FE0000, 0, 0x1B0);
        memset((void *)0x490C0000, 0, 0x3C0000);
        // initialize screen configuration
        initVitaPopsVram();
        // patch display function
        HIJACK_FUNCTION(display_func, sceDisplaySetFrameBufferInternalHook,
            _sceDisplaySetFrameBufferInternal);
    }
}

int sceDisplayGetFrameBufPatched(void** p, int* w, int* f, int t){
    if (p) *p = fake_vram;
    if (w) *w = PSP_SCREEN_LINE;
    if (f) *f = PSP_DISPLAY_PIXEL_FORMAT_8888;
    return 0;
}

// background thread to relocate screen
int pops_draw_thread(int argc, void* argp){
    DisplayWaitVblankStart();
    initVitaPopsVram();
    DisplayWaitVblankStart();
    while (do_draw){
        void* framebuf = NULL;
        int pixelformat, bufferwidth;
        DisplayGetFrameBuf(&framebuf, &bufferwidth, &pixelformat, 0);
        if (!framebuf){
            framebuf = fake_vram;
        }
        copyPSPVram(framebuf);
        DisplayWaitVblankStart();
    }
    return 0;
}

void start_draw_thread(){
    if (draw_thread < 0){
        do_draw = 1;
        draw_thread = sceKernelCreateThread("psxloader", (void*)pops_draw_thread, 0x10, 0x10000, PSP_THREAD_ATTR_VFPU, NULL);
        sceKernelStartThread(draw_thread, 0, NULL);
    }
}

void end_draw_thread(){
    if (draw_thread >= 0){
        do_draw = 0;
        sceKernelWaitThreadEnd(draw_thread, NULL);
        sceKernelDeleteThread(draw_thread);
        draw_thread = -1;
    }
}

int sceKernelSuspendThreadPatched(SceUID thid) {
    SceKernelThreadInfo info;
    info.size = sizeof(SceKernelThreadInfo);
    if (sceKernelReferThreadStatus(thid, &info) == 0) {
        if (strcmp(info.name, "popsmain") == 0) {
            start_draw_thread();
        }
    }
    return sceKernelSuspendThread(thid);
}

int sceKernelResumeThreadPatched(SceUID thid) {
    SceKernelThreadInfo info;
    info.size = sizeof(SceKernelThreadInfo);
    if (sceKernelReferThreadStatus(thid, &info) == 0) {
        if (strcmp(info.name, "popsmain") == 0) {
        	end_draw_thread();
        }
    }
    return sceKernelResumeThread(thid);
}

int (*_sctrlKernelExitVSH)(void*) = NULL;
int popsExit(){
    int k1 = pspSdkSetK1(0);
    // attempt to exit via ps1cfw_enabler
    sceIoOpen("ms0:/__popsexit__", 0, 0);
    // fallback to regular exit
    pspSdkSetK1(k1);
    return _sctrlKernelExitVSH(NULL);
}

static int vram_clear(){
    while (1){
        initVitaPopsVram();
        sceKernelDelayThread(100);
    }
    return 0;
}

int (*arkLauncher)() = NULL;
int popsLauncher(){
    
    if (draw_thread >= 0) return 0; // disallow exit when plugin screen handler is running

    // init pops vram and pause pops, this fixes screen when going back to launcher
    DisplayWaitVblankStart();
    initVitaPopsVram();

    // thread to constantly configure the screen (fixes race condition with pops reconfiguring vram before we pause it)
    int thid = sceKernelCreateThread("psxloader", &vram_clear, 10, 0x10000, PSP_THREAD_ATTR_VFPU, NULL);
    sceKernelStartThread(thid, 0, NULL);

    // send pausepops command
    sceIoOpen("ms0:/__popspause__", 0, 0);

    // pause pops thread if running
    SceUID popsthread = sctrlGetThreadUIDByName("popsmain");
    if (popsthread >= 0){
        sceKernelSuspendThread(popsthread);
    }

    // wait a bit
    DisplayWaitVblankStart();
    DisplayWaitVblankStart();
    sceKernelSuspendThread(thid);

    // launcher reboot
    return arkLauncher();
}

// This patch injects Inferno with no ISO to simulate an empty UMD drive on homebrew
int (*_sctrlKernelLoadExecVSHWithApitype)(int apitype, const char * file, struct SceKernelLoadExecVSHParam * param) = NULL;
int sctrlKernelLoadExecVSHWithApitypeFixed(int apitype, const char * file, struct SceKernelLoadExecVSHParam * param)
{
    // This allows homebrew to launch using PSP Go style apitypes
    reboot_config->fake_apitype = apitype; // reuse space of this variable
    if (apitype == 0x155) apitype = 0x144;

    if (apitype == 0x141 && sctrlSEGetBootConfFileIndex() != MODE_INFERNO){ // homebrew API not using Inferno
        sctrlSESetBootConfFileIndex(MODE_INFERNO); // force inferno to simulate UMD drive
        sctrlSESetUmdFile(""); // empty UMD drive (makes sceUmdCheckMedium return false)
    }
    return _sctrlKernelLoadExecVSHWithApitype(apitype, file, param);
}

int ARKVitaPopsOnModuleStart(SceModule * mod){

    static int booted = 0;

    // Patch sceKernelExitGame Syscalls
    if (strcmp(mod->modname, "sceLoadExec") == 0) {
        // fix vsh exit
        HIJACK_FUNCTION(K_EXTRACT_IMPORT(sctrlKernelExitVSH), popsExit, _sctrlKernelExitVSH);
        REDIRECT_FUNCTION(sctrlHENFindFunction(mod->modname, "LoadExecForUser", 0x05572A5F), popsExit);
        REDIRECT_FUNCTION(sctrlHENFindFunction(mod->modname, "LoadExecForUser", 0x2AC9954B), popsExit);
        goto flush;
    }

    // Patch display for homebrew
    if(strcmp(mod->modname, "sceDisplay_Service") == 0) {
        DisplaySetFrameBuf = (void*)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay", 0x289D82FE);
        DisplayGetFrameBuf = (void*)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay", 0xEEDA2E54);
        DisplayWaitVblankStart = (void*)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay", 0x984C27E7);
        if (sceKernelInitApitype() != 0x144){
            patchVitaPopsDisplay(mod);
        }
        goto flush;
    }

    if (strcmp(mod->modname, "sceLowIO_Driver") == 0) {
        // Protect pops memory
        sceKernelAllocPartitionMemory(6, "", PSP_SMEM_Addr, 0x80000, (void *)0x09F40000);
        memset((void *)0x49F40000, 0, 0x80000);
        goto flush;
    }
    
    // Patch Kermit Peripheral Module to load flash0
    if(strcmp(mod->modname, "sceKermitPeripheral_Driver") == 0)
    {
        extern void patchKermitPeripheral();
        patchKermitPeripheral();
        goto flush;
    }

    // patch to allow plugins to display (i.e. cwcheat)
    if (isLoadingPlugins() && sceKernelInitKeyConfig() == PSP_INIT_KEYCONFIG_POPS) {
        if (mod->text_addr&0x80000000){ // kernel plugin
            sctrlHookImportByNID(mod, "ThreadManForKernel", 0x9944F31F, sceKernelSuspendThreadPatched);
            sctrlHookImportByNID(mod, "ThreadManForKernel", 0x75156E8F, sceKernelResumeThreadPatched);
        }
        else {
            sctrlHookImportByNID(mod, "ThreadManForUser", 0x9944F31F, sceKernelSuspendThreadPatched);
            sctrlHookImportByNID(mod, "ThreadManForUser", 0x75156E8F, sceKernelResumeThreadPatched);
        }
        sctrlHookImportByNID(mod, "sceDisplay", 0x289D82FE, 0);
        sctrlHookImportByNID(mod, "sceDisplay", 0xEEDA2E54, sceDisplayGetFrameBufPatched);
        goto flush;
    }

    // Configure ps1cfw_enabler
    if (strcmp(mod->modname, "sceKernelLibrary") == 0){
        // clear config
        sceIoOpen("ms0:/__popsclear__", 0, 0);
        // send current game information (ID and path)
        if (sceKernelInitKeyConfig() == PSP_INIT_KEYCONFIG_POPS){
            char gameid[20];
            char config[300];
            char* path = sceKernelInitFileName();
            u32 n = sizeof(gameid);
            memset(gameid, 0, n);
            sctrlGetInitPARAM("DISC_ID", NULL, &n, gameid);
            sprintf(config, "ms0:/__popsconfig__/%s/%s", gameid, path+5);
            sceIoOpen(config, 0, 0);
        }
        goto flush;
    }

    // Boot Complete Action not done yet
    if(booted == 0)
    {
        // Boot is complete
        if(isSystemBooted())
        {

            // Initialize Memory Stick Speedup Cache
            if (se_config->msspeed)
                msstorCacheInit("ms");

            if (sceKernelInitKeyConfig() == PSP_INIT_KEYCONFIG_POPS){
                // Set fake framebuffer so that plugins like cwcheat can be displayed
                DisplaySetFrameBuf((void *)fake_vram, PSP_SCREEN_LINE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
                memset((void *)fake_vram, 0, SCE_PSPEMU_FRAMEBUFFER_SIZE);
                // enable vitapops
                sceIoOpen("ms0:/__popsresume__", 0, 0);
            }

            // notify ps1cfw_enabler that boot is complete
            sceIoOpen("ms0:/__popsbooted__", 0, 0);

            // fix launcher exit
            HIJACK_FUNCTION(K_EXTRACT_IMPORT(sctrlArkExitLauncher), popsLauncher, arkLauncher);

            // Boot Complete Action done
            booted = 1;
            goto flush;
        }
    }

flush:
    sctrlFlushCache();

    // Forward to previous Handler
    if(previous) return previous(mod);
    return 0;
}

void initVitaPopsSysPatch(){

    // Register Module Start Handler
    previous = sctrlHENSetStartModuleHandler(ARKVitaPopsOnModuleStart);

    // patch to fix go-style apitypes
    HIJACK_FUNCTION(K_EXTRACT_IMPORT(sctrlKernelLoadExecVSHWithApitype), sctrlKernelLoadExecVSHWithApitypeFixed, _sctrlKernelLoadExecVSHWithApitype);
}