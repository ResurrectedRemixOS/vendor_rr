$(call inherit-product, $(SRC_TARGET_DIR)/product/large_screen_foldable_common.mk)

# Inherit mobile full common Lineage stuff
$(call inherit-product, vendor/rr/config/common_mobile_full.mk)

# Enable support of one-handed mode
PRODUCT_PRODUCT_PROPERTIES += \
    ro.support_one_handed_mode?=true

$(call inherit-product, vendor/rr/config/telephony.mk)

PRODUCT_PACKAGE_OVERLAYS += vendor/rr/overlay/foldable_book

# GMS
WITH_GMS ?= true
ifeq ($(WITH_GMS),true)
$(call inherit-product, vendor/gms/gms_full.mk)
$(call inherit-product, vendor/rr/config/gms_setup.mk)
endif
