# Delete old json file and resources folder (even if it's got folders within in)
[ -e kiosk.json ] && rm kiosk.json
rm -r ../resources/* 2>/dev/null

cp connected-kiosk.json ../kiosk.json
# create a new resources folder
mkdir -p ../resources
mkdir -p ../resources/images/weather-icons/day
mkdir -p ../resources/images/weather-icons/night
cp index.htm ../resources/index.htm
cp data.js ../resources/data.js
cp styles.css ../resources/styles.css
cp -vr images/weather-icons/day/* ../resources/images/weather-icons/day
cp -vr images/weather-icons/night/* ../resources/images/weather-icons/night
cp images/kiosk_daytime_background.webp ../resources/images/kiosk_daytime_background.webp
cp images/kiosk_nighttime_background.webp ../resources/images/kiosk_nighttime_background.webp
pushd ..
./package.sh
popd
cp ../kiosk.zip kiosk.zip

# clean up
rm ../kiosk.json
rm ../kiosk.zip
rm -r ../resources