import re

file_path = "ClassPetUI.cpp"
with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# Remove calls to initCalendarScreen, etc.
content = re.sub(r'\s*initCalendarScreen\(\);\s*initListScreen\(\);\s*initAlarmScreen\(\);\s*initOwnerScreen\(\);', '', content)

with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)

file_path = "ClassPetUI.h"
with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# Remove declarations of initCalendarScreen, etc.
content = re.sub(r'\s*void initCalendarScreen\(\);\s*void initListScreen\(\);\s*void initAlarmScreen\(\);\s*void initOwnerScreen\(\);', '', content)
content = re.sub(r'\s*void initMenuScreen\(\);', '', content)
content = re.sub(r'\s*void showCalendar\(\);\s*void showList\(\);\s*void showAlarm\(\);\s*void showOwnerMemory\(\);', '', content)

with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)

