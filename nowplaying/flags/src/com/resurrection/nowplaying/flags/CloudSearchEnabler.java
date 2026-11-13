package com.resurrection.nowplaying.flags;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.UserHandle;
import android.os.UserManager;
import android.provider.DeviceConfig;
import android.provider.Settings;
import android.util.Log;

/**
 * Keeps Android System Intelligence's Now Playing flags applied on a non-Pixel
 * device. Recognition itself is SoftwareMusicHal. This package re-applies the
 * flags at boot and after Play updates, skips Pixel's setup wizard, lets ASI read
 * the call state, and turns listening on once through ASI's own switch.
 */
public final class CloudSearchEnabler {
    private static final String TAG = "NowPlayingFlags";
    private static final String NAMESPACE = "device_personalization_services";
    private static final String ASI = "com.google.android.as";

    private static final String[][] FLAGS = {
        {"NowPlaying__now_playing_allowed", "true"},
        {"NowPlaying__enable_now_playing_for_main_user", "true"},
        {"NowPlaying__ambient_music_use_dsp_audio_source", "true"},
        {"NowPlaying__ambient_music_on_demand_enabled", "true"},
        {"NowPlaying__ambient_music_on_demand_classic_enabled_by_default", "true"},
        {"NowPlaying__cloud_api_allowed", "true"},
        {"NowPlaying__ambient_music_show_album_art", "true"},
        {"NowPlaying__ambient_music_download_album_art_via_astrea", "true"},
        {"NowPlaying__ambient_music_use_metadata_shards_database", "true"},
        {"NowPlaying__ambient_music_use_enhanced_matches_database", "true"},
        {"NowPlaying__on_demand_hide_if_fingerprinter_install_not_confirmed", "false"},
        {"NowPlaying__ambient_music_on_demand_search_use_new_opt_in_flow", "false"},
        {"NowPlaying__enable_now_playing_lock_screen_update", "true"},
        {"NowPlaying__enable_now_playing_sysui_updates", "true"},
        {"NowPlaying__enable_now_playing_updates", "true"},
        {"NowPlaying__enable_now_playing_quick_affordance", "true"},
        {"NowPlaying__enable_now_playing_quick_settings", "false"},
        {"PccNext__use_astrea_feature_setting_writer", "true"},
    };

    private CloudSearchEnabler() {}

    public static void apply(Context context) {
        try {
            Settings.Secure.putInt(context.getContentResolver(),
                    "now_playing_suw_visited_by_user", 1);
        } catch (Throwable t) {
            Log.w(TAG, "Unable to set secure NP settings", t);
        }
        for (String[] flag : FLAGS) {
            try {
                DeviceConfig.setProperty(NAMESPACE, flag[0], flag[1], /* makeDefault */ true);
            } catch (Throwable t) {
                Log.w(TAG, "Unable to set DeviceConfig " + flag[0], t);
            }
        }
        Log.i(TAG, "Applied Now Playing DeviceConfig flags");
        if (context.getSystemService(UserManager.class).isUserUnlocked()) {
            allowCallState(context);
        }
    }

    private static void allowCallState(Context context) {
        PackageManager pm = context.getPackageManager();
        UserHandle user = context.getUser();
        try {
            int flags = pm.getPermissionFlags(Manifest.permission.READ_PHONE_STATE, ASI, user);
            if ((flags & (PackageManager.FLAG_PERMISSION_USER_SET
                    | PackageManager.FLAG_PERMISSION_USER_FIXED)) == 0) {
                pm.grantRuntimePermission(ASI, Manifest.permission.READ_PHONE_STATE, user);
            }
        } catch (Throwable t) {
            Log.w(TAG, "Unable to grant ASI the call state", t);
        }
    }
}
