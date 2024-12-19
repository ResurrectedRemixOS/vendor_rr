# Inherit common Lineage stuff
$(call inherit-product, vendor/rr/config/common_mobile.mk)

PRODUCT_SIZE := full

# Include GoogleSansFlex font
$(call inherit-product-if-exists, external/google-fonts/google-sans-flex/fonts.mk)

# Apps
PRODUCT_PACKAGES += \
    Camelot \
    Etar \
    GameSpace \
    Profiles \
    Recorder \
    ViviMusic

ifneq ($(PRODUCT_NO_CAMERA),true)
PRODUCT_PACKAGES += \
    Aperture
endif

ifneq ($(TARGET_EXCLUDES_AUDIOFX),true)
PRODUCT_PACKAGES += \
    AudioFX
endif

# Face unlock
TARGET_FACE_UNLOCK_SUPPORTED ?= $(TARGET_SUPPORTS_64_BIT_APPS)
ifeq ($(TARGET_FACE_UNLOCK_SUPPORTED),true)
ifeq ($(filter $(LINEAGE_BUILD), \
    cheetah panther lynx \
    husky shiba akita felix tangorpro \
    tokay caiman komodo comet tegu \
    mustang blazer frankel rango \
    stallion yogi kodiak grizzly cubs \
    ),)
PRODUCT_PACKAGES += \
    FaceUnlock

PRODUCT_SYSTEM_EXT_PROPERTIES += \
    ro.face.sense_service=true

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.biometrics.face.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/permissions/android.hardware.biometrics.face.xml
endif
endif

# Extra cmdline tools
PRODUCT_PACKAGES += \
    unrar \
    zstd

# Fonts
PRODUCT_PACKAGES += \
    FontGoogleSansFlexOverlay

# Resurrected Remix themes, weather and screenshots
$(call inherit-product, vendor/extras/themes/themes.mk)

PRODUCT_PACKAGES += \
    OmniJaws \
    Stitch

# Floating windows and sidebar, started from the launcher and recents
PRODUCT_PACKAGES += \
    LMOFreeform \
    LMOFreeformSidebar

# Include Lineage LatinIME dictionaries
PRODUCT_PACKAGE_OVERLAYS += vendor/rr/overlay/dictionaries
PRODUCT_ENFORCE_RRO_EXCLUDED_OVERLAYS += vendor/rr/overlay/dictionaries
