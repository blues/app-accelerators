# This is a generic packager that builds a ZIP file containing resources, such as
# web pages or python scripts.  The resulting ZIP file contains a Notehub-compliant
# JSON data structure describing metadata that, if the package is uploaded to the
# notehub, will display some of that metadata in the UI.  However, the package
# is simply a ZIP file and so even if this package is used outside the context of
# Notehub it is useful because its version is self-describing, and you can
# always look at what's inside with:
# unzip -p <yourfile.zip> "metadata/info.bin"

# Delete old zip
[ -e kiosk.zip ] && rm kiosk.zip

# Build the metadata so that it appears robustly in the Notehub firmware UI.  Note that the embedded
# firmware info string must be null-terminated, and must be embedded in the ZIP with no compression
# so that it can be extracted from the cleartext within the ZIP file when it's uploaded.
mkdir -p metadata;
echo -n "firmware::info:"$(jq --compact-output ". + {built: now | todateiso8601} + {builder: \""$(whoami)"\"}" kiosk.json) | tr -d '\n' | tr -d '\r'  >metadata/info.bin
printf '\0' >>metadata/info.bin

# Build the ZIP
echo "Building kiosk.zip"
cat kiosk.json
echo ""
zip kiosk.zip -0 metadata/*
zip kiosk.zip resources/*
zip kiosk.zip resources/images/*
zip kiosk.zip resources/images/weather-icons/day/*
zip kiosk.zip resources/images/weather-icons/night/*

# Clean up
rm -rf metadata