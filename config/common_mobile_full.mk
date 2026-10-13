# Inherit common Lineage stuff
$(call inherit-product, vendor/rr/config/common_mobile.mk)

PRODUCT_SIZE := full

#BuildFingerprint spoof to fix RCS/Wallet
# Applies to all devices EXCEPT currently-supported Pixels (they don't need it)
TARGET_ENABLE_FP_OVERRIDE ?= true
ifeq ($(TARGET_ENABLE_FP_OVERRIDE),true)
ifeq ($(filter $(LINEAGE_BUILD), \
    cheetah panther lynx \
    husky shiba akita felix tangorpro \
    tokay caiman komodo comet tegu \
    mustang blazer frankel rango \
    stallion yogi kodiak grizzly cubs \
    ),)
PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildFingerprint=google/bluejay_beta/bluejay:CANARY/ZP11.260918.007/16484274:user/release-keys
endif
endif

# Include GoogleSansFlex font
$(call inherit-product-if-exists, external/google-fonts/google-sans-flex/fonts.mk)

# Apps
PRODUCT_PACKAGES += \
    Camelot \
    Etar \
    Profiles \
    Recorder \
    RRWallpapers \
    Seedvault \
    ViviMusic

ifneq ($(PRODUCT_NO_CAMERA),true)
PRODUCT_PACKAGES += \
    Aperture
endif

ifneq ($(TARGET_EXCLUDES_AUDIOFX),true)
PRODUCT_PACKAGES += \
    AudioFX
endif

# Extra cmdline tools
PRODUCT_PACKAGES += \
    unrar \
    zstd

# Fonts
PRODUCT_PACKAGES += \
    fonts_customization.xml \
    FontGoogleSansFlexOverlay

# Resurrected Remix fonts, themes and weather
$(call inherit-product, vendor/extras/themes/fonts/fonts.mk)
$(call inherit-product, vendor/extras/themes/bootanimation/bootanimation.mk)

PRODUCT_PACKAGES += \
    OmniJaws \
    OmniStyle \
    Stitch

# Floating windows and sidebar, started from the launcher and recents
PRODUCT_PACKAGES += \
    LMOFreeform \
    LMOFreeformSidebar

# UDFPS icons and animations, read by SystemUI
ifeq ($(TARGET_HAS_UDFPS),true)
PRODUCT_PACKAGES += \
    UdfpsAnimations \
    UdfpsIcons
endif

# Include Lineage LatinIME dictionaries
PRODUCT_PACKAGE_OVERLAYS += vendor/rr/overlay/dictionaries
PRODUCT_ENFORCE_RRO_EXCLUDED_OVERLAYS += vendor/rr/overlay/dictionaries
