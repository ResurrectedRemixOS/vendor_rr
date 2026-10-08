# Inherit mobile full common Lineage stuff
$(call inherit-product, vendor/rr/config/common_mobile_full.mk)

# Inherit tablet common Lineage stuff
$(call inherit-product, vendor/rr/config/tablet.mk)

$(call inherit-product, vendor/rr/config/telephony.mk)

# GMS
WITH_GMS ?= true
ifeq ($(WITH_GMS),true)
$(call inherit-product, vendor/gms/gms_full.mk)
endif
