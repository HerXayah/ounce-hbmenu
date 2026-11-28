/* SPDX-License-Identifier: ISC
 * kaiHEN plugin settings exposed by the Settings menu.
 */
#include <stdio.h>
#include <string.h>
#include <kaihen.h>

#include "plugin_settings.h"

static Result openHen(Service* hen) {
    Service ro;
    Result rc = kaihenGetRoSession(&ro);
    if (R_FAILED(rc))
        return rc;
    return kaihenOpenHomebrewSession(&ro, hen);
}

static void copyName(char* dst, size_t dst_size, const char* src) {
    if (!src || !src[0]) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

int hbPluginSettingsLoad(HbPluginSettingsList* out) {
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));

    Service hen;
    Result rc = openHen(&hen);
    if (R_FAILED(rc))
        return 0;
    out->hen_available = true;

    u32 count = 0;
    rc = henGetPluginCount(&hen, &count);
    if (R_FAILED(rc)) {
        serviceClose(&hen);
        return 0;
    }
    if (count > HB_PLUGIN_MAX)
        count = HB_PLUGIN_MAX;

    for (u32 i = 0; i < count; i++) {
        HenPluginName plugin_name;
        memset(&plugin_name, 0, sizeof(plugin_name));
        if (R_FAILED(henGetPluginName(&hen, i, &plugin_name)))
            continue;
        plugin_name.name[sizeof(plugin_name.name) - 1] = '\0';
        if (!plugin_name.name[0])
            continue;

        Service plugin;
        if (R_FAILED(henOpenPlugin(&hen, &plugin_name, &plugin)))
            continue;

        KaihenPluginSettingsInfo info;
        memset(&info, 0, sizeof(info));
        rc = kaihenPluginSettingsGetInfo(&plugin, &info);
        if (R_FAILED(rc) || info.magic != KAIHEN_PLUGIN_SETTINGS_MAGIC ||
            info.version > KAIHEN_PLUGIN_SETTINGS_VERSION || info.count == 0) {
            serviceClose(&plugin);
            continue;
        }

        HbPluginSettings* entry = &out->plugins[out->count];
        copyName(entry->name, sizeof(entry->name),
                 info.name[0] ? info.name : plugin_name.name);
        /* HEN opens plugins by the name the bootstrap registered (the
         * file name), not by the display name from GetInfo. */
        copyName(entry->reg_name, sizeof(entry->reg_name), plugin_name.name);
        u32 option_count = info.count;
        if (option_count > HB_PLUGIN_OPTION_MAX)
            option_count = HB_PLUGIN_OPTION_MAX;
        for (u32 option = 0; option < option_count; option++) {
            KaihenPluginSetting setting;
            memset(&setting, 0, sizeof(setting));
            if (R_FAILED(kaihenPluginSettingsGetSetting(&plugin, option, &setting)))
                continue;
            setting.key[sizeof(setting.key) - 1] = '\0';
            setting.label[sizeof(setting.label) - 1] = '\0';
            if (!setting.key[0] || !setting.label[0])
                continue;
            entry->options[entry->option_count++] = setting;
        }
        serviceClose(&plugin);

        if (entry->option_count > 0)
            out->count++;
        else
            memset(entry, 0, sizeof(*entry));
    }

    serviceClose(&hen);
    return out->count;
}

bool hbPluginSettingsSet(const HbPluginSettings* plugin, const char* key,
                         uint8_t value, char *err, size_t err_size) {
    if (err && err_size)
        err[0] = '\0';
    if (!plugin || !key || !key[0]) {
        if (err && err_size)
            snprintf(err, err_size, "bad input");
        return false;
    }

    Service hen;
    Result rc = openHen(&hen);
    if (R_FAILED(rc)) {
        if (err && err_size)
            snprintf(err, err_size, "open HEN: 0x%x", rc);
        return false;
    }

    HenPluginName plugin_name;
    memset(&plugin_name, 0, sizeof(plugin_name));
    copyName(plugin_name.name, sizeof(plugin_name.name), plugin->reg_name);

    Service service;
    bool applied = false;
    rc = henOpenPlugin(&hen, &plugin_name, &service);
    if (R_FAILED(rc)) {
        if (err && err_size)
            snprintf(err, err_size, "open plugin '%s': 0x%x", plugin->reg_name, rc);
        serviceClose(&hen);
        return false;
    }
    KaihenPluginSettingRequest request;
    memset(&request, 0, sizeof(request));
    copyName(request.key, sizeof(request.key), key);
    request.value = value;
    rc = kaihenPluginSettingsSetSetting(&service, &request);
    applied = R_SUCCEEDED(rc);
    if (!applied && err && err_size)
        snprintf(err, err_size, "set '%s'=%u: 0x%x", key, (unsigned)value, rc);
    serviceClose(&service);
    serviceClose(&hen);
    return applied;
}

static bool readCfgBoolFallback(const char* reg_name, const char* key, bool* out_val) {
    char cfg_path[128];
    snprintf(cfg_path, sizeof(cfg_path), "sdmc:/kaihen/%s.cfg", reg_name);
    FILE* f = fopen(cfg_path, "r");
    if (!f)
        return false;
    char line[256];
    const size_t klen = strlen(key);
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            *out_val = (line[klen + 1] != '0' && line[klen + 1] != '\0' &&
                        line[klen + 1] != '\n' && line[klen + 1] != '\r');
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

bool hbPluginGetBool(const char* reg_name, const char* key, bool default_val) {
    if (!reg_name || !reg_name[0] || !key || !key[0])
        return default_val;

    Service hen;
    if (R_SUCCEEDED(openHen(&hen))) {
        HenPluginName pname;
        memset(&pname, 0, sizeof(pname));
        copyName(pname.name, sizeof(pname.name), reg_name);
        Service plugin;
        if (R_SUCCEEDED(henOpenPlugin(&hen, &pname, &plugin))) {
            KaihenPluginSettingsInfo info;
            memset(&info, 0, sizeof(info));
            if (R_SUCCEEDED(kaihenPluginSettingsGetInfo(&plugin, &info)) &&
                info.magic == KAIHEN_PLUGIN_SETTINGS_MAGIC &&
                info.version <= KAIHEN_PLUGIN_SETTINGS_VERSION) {
                u32 count = info.count > HB_PLUGIN_OPTION_MAX ? HB_PLUGIN_OPTION_MAX : info.count;
                for (u32 i = 0; i < count; i++) {
                    KaihenPluginSetting setting;
                    memset(&setting, 0, sizeof(setting));
                    if (R_SUCCEEDED(kaihenPluginSettingsGetSetting(&plugin, i, &setting))) {
                        setting.key[sizeof(setting.key) - 1] = '\0';
                        if (strcmp(setting.key, key) == 0) {
                            serviceClose(&plugin);
                            serviceClose(&hen);
                            return setting.value != 0;
                        }
                    }
                }
            }
            serviceClose(&plugin);
        }
        serviceClose(&hen);
    }

    bool cfg_val = default_val;
    if (readCfgBoolFallback(reg_name, key, &cfg_val))
        return cfg_val;
    return default_val;
}
