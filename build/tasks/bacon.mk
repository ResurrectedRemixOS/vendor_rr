# Copyright (C) 2017 Unlegacy-Android
# Copyright (C) 2017,2020 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# -----------------------------------------------------------------
# Lineage OTA update package

# Resurrected Remix zip. Falls back to the Lineage name if version.mk did not run.
ifeq ($(RR_DISPLAY_VERSION),)
LINEAGE_TARGET_PACKAGE := $(PRODUCT_OUT)/lineage-$(LINEAGE_VERSION).zip
else
LINEAGE_TARGET_PACKAGE := $(PRODUCT_OUT)/$(RR_DISPLAY_VERSION).zip
endif

SHA256 := prebuilts/build-tools/path/$(HOST_PREBUILT_TAG)/sha256sum

$(LINEAGE_TARGET_PACKAGE): $(INTERNAL_OTA_PACKAGE_TARGET)
	$(hide) ln -f $(INTERNAL_OTA_PACKAGE_TARGET) $(LINEAGE_TARGET_PACKAGE)
	$(hide) $(SHA256) $(LINEAGE_TARGET_PACKAGE) | sed "s|$(PRODUCT_OUT)/||" > $(LINEAGE_TARGET_PACKAGE).sha256sum
	$(hide) python3 vendor/rr/build/tools/changelog.py \
		--out "$(PRODUCT_OUT)/$(TARGET_DEVICE)_changelog.txt" \
		|| echo "changelog: failed, no changelog this build" >&2
	$(hide) python3 vendor/rr/build/tools/createjson.py \
		--zip "$(LINEAGE_TARGET_PACKAGE)" \
		--device "$(TARGET_DEVICE)" \
		--product-out "$(PRODUCT_OUT)" \
		--buildtype "$(LINEAGE_BUILDTYPE)" \
		--variant "$(if $(filter true,$(WITH_GMS)),gapps,vanilla)" \
		--maintainer "$(RR_MAINTAINER)" \
		--url "$(RR_OTA_DOWNLOAD_URL)" \
		|| echo "createjson: failed, no OTA json" >&2
	$(hide) python3 vendor/rr/build/tools/build_banner.py \
		--zip "$(LINEAGE_TARGET_PACKAGE)" \
		--device "$(TARGET_DEVICE)" \
		--model "$(PRODUCT_BRAND) $(PRODUCT_MODEL)" \
		--date "$(LINEAGE_BUILD_DATE)" \
		--buildtype "$(LINEAGE_BUILDTYPE)" \
		--android "$(PLATFORM_VERSION)" \
		--patch "$(PLATFORM_SECURITY_PATCH)" \
		--variant "$(TARGET_BUILD_VARIANT)" \
		--gms "$(WITH_GMS)" \
		|| echo "Package Complete: $(LINEAGE_TARGET_PACKAGE)" >&2

.PHONY: bacon
bacon: $(LINEAGE_TARGET_PACKAGE) $(DEFAULT_GOAL)
