#ifndef PVZ2_PORT_LOCALE_H
#define PVZ2_PORT_LOCALE_H
#ifdef __cplusplus
extern "C" {
#endif
void pvz2_locale_load(void);
int pvz2_locale_index(void);
int pvz2_locale_override(void);
int pvz2_port_settings_save(int override, int locale);
const char *pvz2_locale_name(int index);
const char *pvz2_locale(void);
const char *pvz2_language(void);
const char *pvz2_country(void);
#ifdef __cplusplus
}
#endif
#endif
