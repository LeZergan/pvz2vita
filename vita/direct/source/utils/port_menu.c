/* Down + Cross + L + R at launch opens settings before any game code runs.
 * This screen does not allocate a GL context or leave a gameplay hook behind. */
#include "utils/port_menu.h"
#include "utils/port_locale.h"
#include "utils/save_editor.h"
#include "utils/boot_check.h"
#include "utils/boot_font.h"
#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#define SAVE_PATH DATA_PATH "No_Backup/pp.dat"
#define FRAME_BUFFER_BYTES (2*1024*1024)
static void archive_info(char *name, size_t nc, char *details, size_t dc) {
    const char *path=pvz2_obb_path(),*base=strrchr(path,'/');
    snprintf(name,nc,"OBB: %s",base?base+1:path);
    FILE *f=fopen(path,"rb");
    if(!f){snprintf(details,dc,"Archive is missing or unreadable.");return;}
    long bytes=-1;
    if(!fseek(f,0,SEEK_END))bytes=ftell(f);
    rewind(f);
    unsigned char header[48]={0};
    size_t read=fread(header,1,sizeof(header),f);fclose(f);
    uint32_t version=0,groups=0;
    if(read==sizeof(header)&&!memcmp(header,"1bsr",4)){
        memcpy(&version,header+4,4);memcpy(&groups,header+40,4);
        snprintf(details,dc,"%ld bytes (%.2f MiB)\nRSB v%u / %u resource groups - mods accepted",
                 bytes,bytes>=0?bytes/1048576.0:0.0,version,groups);
    } else snprintf(details,dc,"%ld bytes - unrecognized archive header",bytes);
}
static void text(uint32_t *pixels, int x, int y, const char *s, uint32_t color) {
    int left=x;
    for(;*s && y<504;++s) {
        if(*s=='\n') {x=left;y+=22;continue;}
        if(x>912) {x=left;y+=22;}
        if(y>=504)break;
        unsigned ch=(unsigned char)*s;if(ch<32||ch>126)ch='?';
        for(int r=0;r<8;++r)for(int c=0;c<8;++c)if(g_font[(ch-32)*8+r]&(0x80>>c))
            for(int a=0;a<2;++a)for(int b=0;b<2;++b)pixels[(y+r*2+a)*960+x+c*2+b]=color;
        x+=16;
    }
}
void pvz2_port_menu(void) {
    char status[180]="Done applies your changes and exits. Relaunch to play.";
    if(!pvz2_save_recover(SAVE_PATH,status,sizeof(status)))pvz2_boot_screen(status);
    pvz2_locale_load();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    SceCtrlData pad={0};
    const unsigned chord=SCE_CTRL_DOWN|SCE_CTRL_CROSS|SCE_CTRL_L1|SCE_CTRL_R1;
    /* Hardware status bits (e.g. plugged-in headphones) are not held keys. */
    const unsigned input_buttons=0xffffu;
    int open=0;
    FILE *flag=fopen(DATA_PATH "port_menu.txt","rb");if(flag){fclose(flag);open=1;}
    /* Accept a press during the boot window as well as buttons held at launch. */
    for(unsigned i=0;!open && i<90;++i) {
        /* Ext2 maps the Vita's physical L/R buttons to the L1/R1 bits used
         * by this chord. The legacy API returns LTRIGGER/RTRIGGER instead. */
        if(sceCtrlPeekBufferPositiveExt2(0,&pad,1)>0 && !(pad.buttons&SCE_CTRL_INTERCEPTED) && (pad.buttons&chord)==chord)open=1;
        if(!open)sceKernelDelayThread(16667);
    }
    if(!open)return;
    SceUID block=sceKernelAllocMemBlock("pvz2_port_menu",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,2*FRAME_BUFFER_BYTES,NULL);
    uint32_t *buffers=NULL;
    if(block<0 || sceKernelGetMemBlockBase(block,(void**)&buffers)<0) {
        if(block>=0)sceKernelFreeMemBlock(block);
        pvz2_boot_screen("Cannot allocate the settings display. Close other apps and relaunch.");
        return;
    }
    Pvz2SaveProfile profiles[16];
    int count=pvz2_save_read(SAVE_PATH,profiles,16,status,sizeof(status));
    Pvz2SaveProfile original[16];
    if(count)memcpy(original,profiles,count*sizeof(*profiles));
    uint32_t changed_profiles=0;
    if(count)snprintf(status,sizeof(status),"Done applies changes and exits. Originals are backed up.");
    char archive[100],details[160];
    archive_info(archive,sizeof(archive),details,sizeof(details));
    unsigned selected=0, previous=0, row=0, buffer=0;
    int input_ready=0;
    unsigned released_samples=0;
    uint32_t coins=count?profiles[0].coins:0,gems=count?profiles[0].gems:0;
    int editing=0,digit=0;
    int language=pvz2_locale_index(),override=pvz2_locale_override();
    uint32_t staged=0;
    static const uint32_t powers[]={100000000,10000000,1000000,100000,10000,1000,100,10,1};
    for(;;) {
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
        unsigned pressed=0;
        if(sceCtrlPeekBufferPositiveExt2(0,&pad,1)>0 && !(pad.buttons&SCE_CTRL_INTERCEPTED)) {
            unsigned buttons=pad.buttons&input_buttons;
            /* Launch-button releases can arrive separately, or be intercepted
             * by the shell. Require three clean neutral samples before any
             * menu action; holding/repressing the launch chord never edits. */
            if(!input_ready) {
                if(!buttons) {if(++released_samples>=3)input_ready=1;}
                else released_samples=0;
            } else pressed=buttons&~previous;
            previous=buttons;
            if((buttons&chord)==chord) {
                pressed=0;input_ready=0;released_samples=0;
            }
        } else {input_ready=0;released_samples=0;previous=0;}
        if(editing) {
            if(pressed&SCE_CTRL_LEFT)digit=(digit+8)%9;
            if(pressed&SCE_CTRL_RIGHT)digit=(digit+1)%9;
            if(pressed&(SCE_CTRL_UP|SCE_CTRL_DOWN)) {
                unsigned old=(staged/powers[digit])%10;
                unsigned next=(old+((pressed&SCE_CTRL_UP)?1:9))%10;
                staged=staged-old*powers[digit]+next*powers[digit];
            }
            if(pressed&SCE_CTRL_CROSS){
                if(editing==1)profiles[selected].coins=coins=staged;
                else profiles[selected].gems=gems=staged;
                if(profiles[selected].coins!=original[selected].coins || profiles[selected].gems!=original[selected].gems)
                    changed_profiles|=1u<<selected;
                else changed_profiles&=~(1u<<selected);
                editing=0;
            }
            if(pressed&SCE_CTRL_CIRCLE)editing=0;
        } else {
            if(pressed&SCE_CTRL_UP)row=(row+5)%6;
            if(pressed&SCE_CTRL_DOWN)row=(row+1)%6;
            if(row==0 && (pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_CROSS)))override=!override;
            if(row==1 && override && (pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_CROSS))) {
                language=(language+((pressed&SCE_CTRL_LEFT)?5:1))%6;
            }
            if(row==2 && count && (pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_CROSS))) {
                selected=(selected+((pressed&SCE_CTRL_LEFT)?count-1:1))%count;
                coins=profiles[selected].coins;gems=profiles[selected].gems;
            }
            if(pressed&SCE_CTRL_CROSS) {
                if(count && (row==3||row==4)){editing=row==3?1:2;staged=row==3?coins:gems;if(staged>999999999u)staged=999999999u;digit=8;}
                if(row==5) {
                    int ok=1;
                    int saved_currencies=changed_profiles!=0;
                    if(changed_profiles)ok=pvz2_save_apply_batch(SAVE_PATH,profiles,count,changed_profiles,status,sizeof(status));
                    if(ok) {
                        changed_profiles=0;
                        if(count)memcpy(original,profiles,count*sizeof(*profiles));
                        ok=pvz2_port_settings_save(override,language);
                        if(!ok)snprintf(status,sizeof(status),saved_currencies?
                            "Currencies saved; settings failed. Check free space and retry Done.":
                            "Cannot save settings. Check free space and try Done again.");
                    }
                    if(ok)sceKernelExitProcess(0);
                }
            }
            if(pressed&SCE_CTRL_CIRCLE){sceKernelExitProcess(0);break;}
        }
        /* Draw only into the inactive buffer. Wait for the submitted buffer
         * to become visible before reusing the previous one next iteration. */
        uint32_t *pixels=(uint32_t*)((unsigned char*)buffers+buffer*FRAME_BUFFER_BYTES);
        for(unsigned i=0;i<960*544;++i)pixels[i]=0xff251a16;
        text(pixels,32,28,"PVZ2 VITA - SETTINGS",0xff84e4ac);
        text(pixels,32,60,archive,0xffeeeeee);
        text(pixels,32,82,details,0xffeeeeee);
        text(pixels,32,130,"Data folder: " GAME_DATA_PATH,0xffeeeeee);
        char lines[6][100];
        snprintf(lines[0],100,"Language override: %s",override?"On":"Off");
        snprintf(lines[1],100,"Language: %s",override?pvz2_locale_name(language):"English (automatic)");
        char player_name[33]={0};
        if(count)for(unsigned i=0;i<32&&profiles[selected].name[i];++i) {
            unsigned ch=(unsigned char)profiles[selected].name[i];player_name[i]=(ch>=32&&ch<=126)?ch:'?';
        }
        snprintf(lines[2],100,"Player %u/%d: %s",count?selected+1:0,count,count?player_name:"No save");
        snprintf(lines[3],100,"Coins: %u",coins);snprintf(lines[4],100,"Gems: %u",gems);
        snprintf(lines[5],100,"Done - apply changes and exit");
        for(unsigned i=0;i<6;++i){text(pixels,32,174+i*30,i==row?">":" ",0xff84e4ac);text(pixels,64,174+i*30,lines[i],i==row?0xff84e4ac:(i==1&&!override)?0xff999999:0xffeeeeee);}
        if(editing) {
            char value[96];snprintf(value,sizeof(value),"%s: %09u",editing==1?"Coins":"Gems ",staged);
            text(pixels,64,364,value,0xff84e4ac);
            text(pixels,64+7*16+digit*16,386,"^",0xff84e4ac);
            text(pixels,32,434,"Left/Right: digit   Up/Down: change digit\nX: accept value   Circle: cancel value",0xffeeeeee);
        } else {
            text(pixels,32,364,input_ready?status:"Release all launch buttons to use settings.",0xffeeeeee);
            text(pixels,32,456,"Up/Down: select   Left/Right: change option\nX: edit/Done   Circle: exit, discard changes",0xff84e4ac);
        }
        SceDisplayFrameBuf fb={sizeof(fb),pixels,960,SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,960,544};
        if(sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME)<0 || sceDisplayWaitSetFrameBuf()<0) {
            pvz2_boot_screen("Settings display failed. Close other apps and relaunch.");
            break;
        }
        buffer^=1;
    }
    // Detach the software framebuffer before returning its memory to the pool.
    sceDisplaySetFrameBuf(NULL,SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();sceDisplayWaitVblankStart();
    sceKernelFreeMemBlock(block);
}
