# Pixel Setup props shared by the booting Rising and Evolution trees.
PRODUCT_PRODUCT_PROPERTIES += \
    ro.setupwizard.enterprise_mode=1 \
    ro.setupwizard.esim_cid_ignore=00000001 \
    setupwizard.feature.baseline_setupwizard_enabled=true \
    setupwizard.feature.day_night_mode_enabled=true \
    setupwizard.feature.portal_notification=true \
    setupwizard.feature.enable_quick_start_flow=true \
    setupwizard.feature.enable_restore_anytime=true \
    setupwizard.feature.enable_wifi_tracker=true \
    setupwizard.feature.lifecycle_refactoring=true \
    setupwizard.feature.notification_refactoring=true \
    setupwizard.feature.show_pai_screen_in_main_flow.carrier1839=false \
    setupwizard.feature.show_pixel_tos=true \
    setupwizard.feature.show_support_link_in_deferred_setup=false \
    setupwizard.feature.skip_button_use_mobile_data.carrier1839=true \
    setupwizard.personal_safety_suw_enabled=true \
    setupwizard.theme=glif_expressive \
    setupwizard.feature.default_locale_enhancement_enabled=true \
    setupwizard.feature.device_info_icon_enabled=true \
    setupwizard.feature.provisioning_profile_mode=true \
    setupwizard.feature.enable_gil= \
    setupwizard.feature.enable_gil_logging=true \
    setupwizard.feature.enable_minors_setup_flow=true \
    setupwizard.feature.enable_parental_notice_activity=true \
    setupwizard.feature.enable_parental_setup=true \
    setupwizard.feature.enhanced_setup_design_metrics=true \
    setupwizard.feature.is_suw_onboarding_contract_enabled=true \
    setupwizard.feature.joined_up_loading=true \
    setupwizard.feature.locale_agnostic_enabled=true

ifeq ($(PRODUCT_CHARACTERISTICS),tablet)
PRODUCT_PRODUCT_PROPERTIES += ro.setupwizard.rotation_locked=false
else
PRODUCT_PRODUCT_PROPERTIES += ro.setupwizard.rotation_locked=true
endif

PRODUCT_PACKAGES += \
    PixelSetupWizardOverlayExpressive
