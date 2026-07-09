sed -i '' 's/#define I2S_DOUT_PIN           6/#define I2S_DOUT_PIN           8/g' Board.h
sed -i '' 's/#define I2S_MIC_SCK_PIN        43/#define I2S_MIC_SCK_PIN        5/g' Board.h
sed -i '' 's/#define I2S_MIC_WS_PIN         44/#define I2S_MIC_WS_PIN         7/g' Board.h
sed -i '' 's/#define I2S_MIC_SD_PIN         14/#define I2S_MIC_SD_PIN         6/g' Board.h
