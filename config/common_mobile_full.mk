# Inherit common Lineage stuff
$(call inherit-product, vendor/rr/config/common_mobile.mk)

PRODUCT_SIZE := full

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
$(call inherit-product, packages/overlays/Resurrection/fonts/fonts.mk)

PRODUCT_PACKAGES += \
    OmniJaws \
    OmniStyle

# Include Lineage LatinIME dictionaries
PRODUCT_PACKAGE_OVERLAYS += vendor/rr/overlay/dictionaries
PRODUCT_ENFORCE_RRO_EXCLUDED_OVERLAYS += vendor/rr/overlay/dictionaries
