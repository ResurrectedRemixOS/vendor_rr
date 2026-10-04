# Inherit mobile full common Lineage stuff
$(call inherit-product, vendor/rr/config/common_mobile_full.mk)

# Inherit tablet common Lineage stuff
$(call inherit-product, vendor/rr/config/tablet.mk)

$(call inherit-product, vendor/rr/config/wifionly.mk)
