sed -i '' 's/#include <SD.h>/#include <SD_MMC.h>/g' Storage.cpp
sed -i '' 's/#include <SD.h>/#include <SD_MMC.h>/g' ESP32Audio.cpp
sed -i '' 's/#include <SD.h>/#include <SD_MMC.h>/g' ApiClient.cpp
sed -i '' 's/SD\.begin/SD_MMC\.begin/g' Storage.cpp
sed -i '' 's/SD\.cardType/SD_MMC\.cardType/g' Storage.cpp
sed -i '' 's/SD\.cardSize/SD_MMC\.cardSize/g' Storage.cpp
sed -i '' 's/SD\.open/SD_MMC\.open/g' ESP32Audio.cpp
sed -i '' 's/SD\.open/SD_MMC\.open/g' ApiClient.cpp
sed -i '' 's/SD\.cardType/SD_MMC\.cardType/g' ESP32Audio.cpp
