PRODUCT_VERSION_MAJOR = 24
PRODUCT_VERSION_MINOR = 0

ifeq ($(LINEAGE_VERSION_APPEND_TIME_OF_DAY),true)
    LINEAGE_BUILD_DATE := $(shell date -u +%Y%m%d_%H%M%S)
else
    LINEAGE_BUILD_DATE := $(shell date -u +%Y%m%d)
endif

# Set LINEAGE_BUILDTYPE from the env RELEASE_TYPE, for jenkins compat

ifndef LINEAGE_BUILDTYPE
    ifdef RELEASE_TYPE
        # Starting with "LINEAGE_" is optional
        RELEASE_TYPE := $(shell echo $(RELEASE_TYPE) | sed -e 's|^LINEAGE_||g')
        LINEAGE_BUILDTYPE := $(RELEASE_TYPE)
    else
        # Resurrected Remix builds are official unless RELEASE_TYPE says otherwise.
        LINEAGE_BUILDTYPE := OFFICIAL
    endif
endif

# Filter out random types, so it'll reset to UNOFFICIAL
ifeq ($(filter OFFICIAL RELEASE NIGHTLY SNAPSHOT EXPERIMENTAL,$(LINEAGE_BUILDTYPE)),)
    LINEAGE_BUILDTYPE := UNOFFICIAL
    LINEAGE_EXTRAVERSION :=
endif

ifeq ($(LINEAGE_BUILDTYPE), UNOFFICIAL)
    ifneq ($(TARGET_UNOFFICIAL_BUILD_ID),)
        LINEAGE_EXTRAVERSION := -$(TARGET_UNOFFICIAL_BUILD_ID)
    endif
endif

LINEAGE_VERSION_SUFFIX := $(LINEAGE_BUILD_DATE)-$(LINEAGE_BUILDTYPE)$(LINEAGE_EXTRAVERSION)-$(LINEAGE_BUILD)

# Platform version. Lineage's build still keys off 24.0.
LINEAGE_VERSION := $(PRODUCT_VERSION_MAJOR).$(PRODUCT_VERSION_MINOR)-$(LINEAGE_VERSION_SUFFIX)

# RR_VERSION only feeds Updater's version check; nothing user-facing shows it.
# About phone reads ro.lineage.version, so that property carries this string.
RR_VERSION := 9.0
RR_DISPLAY_VERSION := ResurrectedRemix-$(LINEAGE_VERSION_SUFFIX)
LINEAGE_DISPLAY_VERSION := $(RR_DISPLAY_VERSION)

PRODUCT_PRODUCT_PROPERTIES += \
    ro.lineage.version=$(RR_DISPLAY_VERSION) \
    ro.lineage.display.version=$(RR_DISPLAY_VERSION) \
    ro.lineage.build.version=$(PRODUCT_VERSION_MAJOR).$(PRODUCT_VERSION_MINOR) \
    ro.lineage.releasetype=$(LINEAGE_BUILDTYPE) \
    ro.rr.version=$(RR_VERSION) \
    ro.rr.display.version=$(RR_DISPLAY_VERSION) \
    ro.rr.device=$(LINEAGE_BUILD) \
    ro.rr.build.variant=$(if $(filter true,$(WITH_GMS)),gapps,vanilla)

# Shown in About phone > Android version and in the OTA json. Set it in the
# device's rr_<device>.mk before the vendor/rr inherit. No spaces.
RR_MAINTAINER ?=

# OTA json download link. {device}, {build} (the zip name without .zip) and {filename} expand.
RR_OTA_DOWNLOAD_URL ?= https://dl.resurrectedremix.com/{device}/{build}/{filename}
ifneq ($(RR_MAINTAINER),)
PRODUCT_PRODUCT_PROPERTIES += \
    ro.rr.maintainer=$(RR_MAINTAINER)
endif
