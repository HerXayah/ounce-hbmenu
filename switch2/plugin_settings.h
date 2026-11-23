/* SPDX-License-Identifier: ISC
 * kaiHEN plugin settings exposed by the Settings menu.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <kaihen/plugin.h>
#include <kaihen/plugin_settings.h>

#define HB_PLUGIN_MAX 16
#define HB_PLUGIN_OPTION_MAX 9

typedef struct {
    char name[HEN_PLUGIN_NAME_SIZE];
    KaihenPluginSetting options[HB_PLUGIN_OPTION_MAX];
    int option_count;
} HbPluginSettings;

typedef struct {
    HbPluginSettings plugins[HB_PLUGIN_MAX];
    int count;
    bool hen_available;
} HbPluginSettingsList;

/// Reads every published plugin's settings table through HEN. Returns the
/// number of plugins that exposed at least one option.
int hbPluginSettingsLoad(HbPluginSettingsList* out);

/// Applies one value through the plugin's service. Returns true on success.
bool hbPluginSettingsSet(const HbPluginSettings* plugin, const char* key,
                         uint8_t value);
