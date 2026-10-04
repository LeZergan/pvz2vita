#include "utils/port_locale.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
static const char *names[] = {"English", "German", "Spanish", "French", "Italian", "Portuguese (Brazil)"};
static const char *locales[] = {"en_US", "de_DE", "es_ES", "fr_FR", "it_IT", "pt_BR"};
static const char *languages[] = {"en", "de", "es", "fr", "it", "pt"};
static int selected, language_override;
void pvz2_locale_load(void) {
    selected = 0;
    language_override = 0;
    FILE *settings = fopen(DATA_PATH "port_settings.txt", "rb");
    if (!settings) {
        rename(DATA_PATH "port_settings.txt.old", DATA_PATH "port_settings.txt");
        settings = fopen(DATA_PATH "port_settings.txt", "rb");
    }
    if (settings) {
        char data[64]={0};
        size_t length=fread(data,1,sizeof(data)-1,settings);
        int tail=fgetc(settings);
        int valid=!ferror(settings) && tail==EOF && length>0 && !memchr(data,0,length);
        long values[3]={0};char *next=data,*end=data;
        for(unsigned i=0;valid && i<3;++i) {
            errno=0;values[i]=strtol(next,&end,10);
            valid=next!=end && errno!=ERANGE && (!*end || isspace((unsigned char)*end));next=end;
        }
        while(isspace((unsigned char)*next))++next;
        valid=valid && !*next;
        long version=values[0],override=values[1],locale=values[2];
        if (valid && (version == 2 || version == 3) && (override == 0 || override == 1) && locale >= 0 && locale < 6) {
            /* RC26/RC27 could mistake a launch-button edge for an override
             * edit. Reset their preferences once; version 3 persists only
             * deliberate changes made after the release guard is armed. */
            if(version == 3) {selected = locale; language_override = override;}
            fclose(settings); return;
        }
        fclose(settings);
    }
}
int pvz2_port_settings_save(int override, int locale) {
    if (locale < 0 || locale >= 6 || (override != 0 && override != 1)) return 0;
    FILE *f = fopen(DATA_PATH "port_settings.txt.tmp", "wb");
    if (!f) return 0;
    int ok = fprintf(f, "3 %d %d\n", override, locale) > 0 && fflush(f) == 0;
    if (ok && fsync(fileno(f))) ok = 0;
    if (fclose(f)) ok = 0;
    if (!ok) { remove(DATA_PATH "port_settings.txt.tmp"); return 0; }
    /* Verify exact persisted bytes before staging the previous preferences. */
    char expected[16],actual[16]={0};
    int length=snprintf(expected,sizeof(expected),"3 %d %d\n",override,locale);
    f=fopen(DATA_PATH "port_settings.txt.tmp","rb");
    if(!f)return 0;
    ok=fread(actual,1,sizeof(actual),f)==(size_t)length && !ferror(f) && !memcmp(expected,actual,length);
    if(fclose(f))ok=0;
    if(!ok){remove(DATA_PATH "port_settings.txt.tmp");return 0;}
    FILE *previous = fopen(DATA_PATH "port_settings.txt", "rb");
    if (previous) {
        fclose(previous);
        if(remove(DATA_PATH "port_settings.txt.old") && errno!=ENOENT)return 0;
        if (rename(DATA_PATH "port_settings.txt", DATA_PATH "port_settings.txt.old")) return 0;
    }
    if (rename(DATA_PATH "port_settings.txt.tmp", DATA_PATH "port_settings.txt")) {
        rename(DATA_PATH "port_settings.txt.old", DATA_PATH "port_settings.txt");
        return 0;
    }
    remove(DATA_PATH "port_settings.txt.old");
    selected = locale; language_override = override;
    return 1;
}
int pvz2_locale_index(void) { return selected; }
int pvz2_locale_override(void) { return language_override; }
const char *pvz2_locale_name(int i) { return i >= 0 && i < 6 ? names[i] : names[0]; }
const char *pvz2_locale(void) { return locales[language_override ? selected : 0]; }
const char *pvz2_language(void) { return languages[language_override ? selected : 0]; }
const char *pvz2_country(void) { return pvz2_locale() + 3; }
